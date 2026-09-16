/* sh — a Ring3 user-mode shell (M2-2, pipelines added in M4/C1).
 *
 * Until now the only shell was init/shell.c, which runs in Ring 0: it
 * drove the machine with direct printk/fork calls and never exercised
 * the user side of the system call boundary.  This one is a normal
 * ELF32 program on the MINIX filesystem that reaches the kernel only
 * through int 0x80, which is what makes it a useful test of the whole
 * user-mode path (fork/waitpid/execve/read/write/open/chdir).
 *
 * Usage:  make prog NAME=sh    then   exec /bin/sh
 *
 * Grammar (deliberately small, but the real thing):
 *
 *     line   := pipeline
 *     pipeline := command ( '|' command )*
 *     command  := word+ redirection*
 *     redirection := '<' file | '>' file | '>>' file
 *
 * Everything is done with the syscalls the kernel already had —
 * pipe(42) and dup2(63) have been implemented since the 0.01 alignment
 * work, but nothing had ever used them to connect two processes, so
 * "|" and ">" are where they finally earn their keep:
 *
 *     echo hi > /f          cat /f            wc < /f
 *     cat /hello.txt | wc   cat /a >> /b      help | wc
 *
 * Builtins: cd, pwd, echo, exit, help.  A builtin used on its own runs
 * in the shell process (so cd persists) with its redirections applied
 * and restored; a builtin inside a pipeline runs in the forked child,
 * which is why `echo hi | wc` works.
 */

#include "lib.h"

#define LINE_MAX 128
#define ARG_MAX  16
#define MAX_CMDS 4

struct cmd {
    char *argv[ARG_MAX];
    int argc;
    char *in;                  /* < file            */
    char *out;                 /* > or >> file      */
    int append;                /* >> instead of >   */
};

static int want_exit;
static int exit_code;

/* Read one line from stdin into buf, echoing and handling backspace.
   Returns the length, or -1 at end of input.  The kernel's tty layer
   does not do line editing for Ring3 readers, so this does. */
static int read_line(char *buf, int size)
{
    int i = 0;
    char c;

    for (;;) {
        if (read(0, &c, 1) != 1)
            return i ? i : -1;

        if (c == '\n') {
            write(1, "\n", 1);
            buf[i] = '\0';
            return i;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                write(1, "\b \b", 3);
            }
            continue;
        }
        if (i < size - 1) {
            buf[i++] = c;
            write(1, &c, 1);       /* echo */
        }
    }
}

/* Put spaces around the metacharacters so a plain whitespace split can
   tokenise "cat<a|b" the same way it tokenises "cat < a | b". */
static void normalize(const char *in, char *out, int outsz)
{
    int i = 0;

    while (*in && i < outsz - 4) {
        char c = *in++;

        if (c == '<' || c == '>' || c == '|') {
            out[i++] = ' ';
            out[i++] = c;
            if (c == '>' && *in == '>') {
                out[i++] = '>';
                in++;
            }
            out[i++] = ' ';
        } else {
            out[i++] = c;
        }
    }
    out[i] = '\0';
}

/* Split `line` into commands.  Returns the number of commands (0 for an
   empty line), or -1 on a syntax error.

   The token buffer is deliberately STATIC: every argv[] and redirection
   filename points into it, and those pointers have to stay valid after
   this function returns (they are used by run_pipeline, which may run
   after more stack activity).  A local buffer here dangles into a dead
   frame — the first version did exactly that and the command name came
   out as garbage, which is what `sh: echo???? exited with 127` was. */
static char norm[LINE_MAX];

static int parse_pipeline(char *line, struct cmd *cmds, int maxcmds)
{
    char *p;
    int n = 0;

    normalize(line, norm, sizeof(norm));

    cmds[0].argc = 0;
    cmds[0].in = cmds[0].out = NULL;
    cmds[0].append = 0;

    p = norm;
    while (*p) {
        char *word;

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        word = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = '\0';

        if (strcmp(word, "|") == 0) {
            if (n + 1 >= maxcmds || cmds[n].argc == 0)
                return -1;
            n++;
            cmds[n].argc = 0;
            cmds[n].in = cmds[n].out = NULL;
            cmds[n].append = 0;
            continue;
        }
        if (strcmp(word, "<") == 0 || strcmp(word, ">") == 0 ||
            strcmp(word, ">>") == 0) {
            char *file;

            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                return -1;                  /* redirection without a file */
            file = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';

            if (word[0] == '<') {
                cmds[n].in = file;
            } else {
                cmds[n].out = file;
                cmds[n].append = (word[1] == '>');
            }
            continue;
        }

        if (cmds[n].argc >= ARG_MAX - 1)
            return -1;
        cmds[n].argv[cmds[n].argc++] = word;
    }

    if (cmds[n].argc == 0)
        return n == 0 ? 0 : -1;             /* "a |" or "| a" */
    cmds[n].argv[cmds[n].argc] = NULL;
    return n + 1;
}

/* Open the files a command asked for and point fd 0/1 at them.  Called
   in the child (or around a builtin) so the shell's own fds survive. */
static int apply_redirs(struct cmd *c)
{
    int fd;

    if (c->in) {
        fd = open(c->in, 0, 0);             /* O_RDONLY */
        if (fd < 0) {
            printf("sh: %s: cannot open\n", c->in);
            return -1;
        }
        dup2(fd, 0);
        if (fd != 0)
            close(fd);
    }

    if (c->out) {
        if (c->append) {
            fd = open(c->out, 1, 0);        /* O_WRONLY */
            if (fd >= 0)
                lseek(fd, 0, 2);            /* SEEK_END: no O_APPEND here */
        } else {
            fd = creat(c->out, 0644);
        }
        if (fd < 0) {
            printf("sh: %s: cannot create\n", c->out);
            return -1;
        }
        dup2(fd, 1);
        if (fd != 1)
            close(fd);
    }
    return 0;
}

/* --- builtins ------------------------------------------------------ */

/* --- background jobs (B5.8) -------------------------------------------
 *
 * `cmd &` runs the command in a *child shell*: the parent forks, the
 * child calls the ordinary run_pipeline() and exits, and the parent
 * records the pid and goes straight back to the prompt.  That is how a
 * real shell does it, and it means run_pipeline()/run_one() need no
 * notion of "background" at all.
 *
 * There is no job control (no SIGSTOP/SIGCONT, no process groups wired to
 * the terminal), so a job cannot be suspended or brought to the
 * foreground; the shell only tracks pids, reaps them with WNOHANG before
 * each prompt, and `wait` blocks for them.  The kernel's wait(pid) makes
 * that safe: waiting for a specific pid never steals the status of a
 * foreground command.
 */
#define MAX_BG 8

static int bg_pid[MAX_BG];

static void bg_add(int pid)
{
    int i;

    for (i = 0; i < MAX_BG; i++) {
        if (!bg_pid[i]) {
            bg_pid[i] = pid;
            return;
        }
    }
    printf("sh: too many background jobs (max %d)\n", MAX_BG);
}

/* Reap finished background jobs.  blocking=0 is the "did anything finish
   while I was at the prompt?" sweep; blocking=1 is `wait`. */
static void bg_reap(int blocking, int only_pid)
{
    int i, r;
    unsigned long code;

    for (i = 0; i < MAX_BG; i++) {
        if (!bg_pid[i])
            continue;
        if (only_pid && bg_pid[i] != only_pid)
            continue;

        code = 0;
        r = waitpid(bg_pid[i], &code, blocking ? 0 : WNOHANG);
        if (r == bg_pid[i]) {
            printf("sh: [%d] done (status %lu)\n", bg_pid[i], code);
            bg_pid[i] = 0;
        } else if (r < 0) {
            bg_pid[i] = 0;              /* already reaped / gone */
        }
    }
}

static int is_builtin(const char *name)
{
    /* "pwd" is deliberately NOT here: it used to be a builtin that was
       silently rewritten into "ls" (there was no getcwd and no /bin/pwd),
       so `pwd` printed a directory listing.  Now that /bin/pwd exists (it
       resolves the path through ".." - see user/pwd.c), pwd is an ordinary
       program from /bin like any other. */
    return strcmp(name, "cd") == 0 ||
           strcmp(name, "echo") == 0 || strcmp(name, "help") == 0 ||
           strcmp(name, "exit") == 0 || strcmp(name, "wait") == 0 ||
           strcmp(name, "sleep") == 0;
}

/* Run a builtin; returns its exit status.  Runs in whatever process
   calls it, so redirections must already be in place. */
static int builtin_run(struct cmd *c)
{
    int i;

    if (strcmp(c->argv[0], "echo") == 0) {
        for (i = 1; i < c->argc; i++) {
            if (i > 1)
                write(1, " ", 1);
            write(1, c->argv[i], (int)strlen(c->argv[i]));
        }
        write(1, "\n", 1);
        return 0;
    }
    if (strcmp(c->argv[0], "help") == 0) {
        printf("sh: builtins  cd <dir>  pwd  echo <text>  exit  help\n");
        printf("sh: run       <program> [args]      (from /bin)\n");
        printf("sh: redirect  < file   > file   >> file\n");
        printf("sh: pipe      cmd1 | cmd2 | cmd3\n");
        printf("sh: background cmd &   then:  wait [pid]\n");
        printf("sh: sleep <seconds>   (kernel syscall 71)\n");
        return 0;
    }
    if (strcmp(c->argv[0], "wait") == 0) {
        int only = c->argc > 1 ? atoi(c->argv[1]) : 0;

        bg_reap(1, only);               /* blocks until the job finishes */
        return 0;
    }
    if (strcmp(c->argv[0], "sleep") == 0) {
        unsigned long secs = c->argc > 1 ? (unsigned long)atoi(c->argv[1]) : 1;
        int left = sleep(secs);

        printf("sh: slept %lu s, %d left\n", secs, left);
        return 0;
    }
    if (strcmp(c->argv[0], "cd") == 0) {
        if (c->argv[1] == NULL) {
            printf("sh: usage: cd <dir>\n");
            return 1;
        }
        if (chdir(c->argv[1]) < 0) {
            printf("sh: cd: %s: no such directory\n", c->argv[1]);
            return 1;
        }
        return 0;
    }
    if (strcmp(c->argv[0], "exit") == 0) {
        want_exit = 1;
        exit_code = c->argv[1] ? atoi(c->argv[1]) : 0;
        return exit_code;
    }
    /* "pwd" never reaches here: it has no getcwd() to use and is turned
       into "ls" by the callers (see run_one). */
    return 127;
}

/* Build "/bin/<name>" and exec it, or report why not. */
static void exec_program(struct cmd *c)
{
    char path[64];
    int i = 0;
    const char *s = c->argv[0];

    path[i++] = '/';
    path[i++] = 'b';
    path[i++] = 'i';
    path[i++] = 'n';
    path[i++] = '/';
    while (*s && i < (int)sizeof(path) - 1)
        path[i++] = *s++;
    path[i] = '\0';

    execve(path, c->argv, NULL);
    printf("sh: %s: cannot execute\n", path);
    exit(127);
}

/* Run one command: builtin in place, anything else via fork+execve.
   Returns the exit status. */
static int run_one(struct cmd *c)
{
    int pid;
    unsigned long code = 0;

    if (is_builtin(c->argv[0]) && strcmp(c->argv[0], "pwd") != 0) {
        int save0 = -1, save1 = -1, status;

        if (c->in || c->out) {
            save0 = dup(0);                 /* keep the shell's tty */
            save1 = dup(1);
            if (apply_redirs(c) < 0) {
                if (save0 >= 0) { dup2(save0, 0); close(save0); }
                if (save1 >= 0) { dup2(save1, 1); close(save1); }
                return 1;
            }
        }
        status = builtin_run(c);
        if (save0 >= 0) { dup2(save0, 0); close(save0); }
        if (save1 >= 0) { dup2(save1, 1); close(save1); }
        return status;
    }

    pid = fork();
    if (pid < 0) {
        printf("sh: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        if (apply_redirs(c) < 0)
            exit(1);
        if (is_builtin(c->argv[0]))
            exit(builtin_run(c));           /* e.g. echo inside a pipeline */
        exec_program(c);
        exit(1);            /* execve failed: this was a child, so stop here */
    }

    waitpid(pid, &code, 0);
    return (int)code;
}

/* Run a whole pipeline, wiring each stage to the next with pipe(2). */
static int run_pipeline(struct cmd *cmds, int ncmds)
{
    int pids[MAX_CMDS];
    int i, prev = -1;
    int status = 0;

    if (ncmds == 1)
        return run_one(&cmds[0]);

    for (i = 0; i < ncmds; i++) {
        unsigned long fds[2];
        int pid;

        if (i < ncmds - 1) {
            if (pipe(fds) < 0) {
                printf("sh: pipe failed\n");
                break;
            }
        }

        pid = fork();
        if (pid < 0) {
            printf("sh: fork failed\n");
            if (i < ncmds - 1) {
                close((int)fds[0]);
                close((int)fds[1]);
            }
            break;
        }

        if (pid == 0) {
            /* child: stdin from the previous stage, stdout to the next */
            if (prev >= 0) {
                dup2(prev, 0);
                close(prev);
            }
            if (i < ncmds - 1) {
                close((int)fds[0]);
                dup2((int)fds[1], 1);
                close((int)fds[1]);
            }
            if (apply_redirs(&cmds[i]) < 0)
                exit(1);
            if (is_builtin(cmds[i].argv[0]) &&
                strcmp(cmds[i].argv[0], "pwd") != 0)
                exit(builtin_run(&cmds[i]));
            exec_program(&cmds[i]);
            exit(1);        /* execve failed: never continue as a second shell */
        }

        pids[i] = pid;
        if (i < ncmds - 1)
            close((int)fds[1]);
        if (prev >= 0)
            close(prev);
        prev = (i < ncmds - 1) ? (int)fds[0] : -1;
    }

    if (prev >= 0)
        close(prev);

    for (i = 0; i < ncmds; i++) {
        unsigned long code = 0;

        waitpid(pids[i], &code, 0);
        status = (int)code;                 /* last stage wins */
    }
    return status;
}

int main(int argc, char *argv[])
{
    char line[LINE_MAX];
    struct cmd cmds[MAX_CMDS];
    int n, status, background, i;

    printf("sh: user-mode shell (Ring3), pid=%d\n", getpid());
    printf("sh: builtins: cd echo exit help wait sleep; other names run /bin/<name>\n");
    printf("sh: supports < > >> | and & (type 'help')\n");

    for (;;) {
        bg_reap(0, 0);                  /* report jobs that finished */
        write(1, "$ ", 2);

        n = read_line(line, sizeof(line));
        if (n < 0) {
            printf("\nsh: end of input, exiting\n");
            break;
        }
        if (n == 0)
            continue;

        /* A trailing '&' (spaces allowed after it) means background.  It
           is handled here, before parsing, so the parser stays a plain
           "words, redirections and pipes" parser. */
        background = 0;
        for (i = n - 1; i >= 0 && (line[i] == ' ' || line[i] == '\t'); i--)
            ;
        if (i >= 0 && line[i] == '&') {
            background = 1;
            line[i] = ' ';
        }

        n = parse_pipeline(line, cmds, MAX_CMDS);
        if (n < 0) {
            printf("sh: syntax error\n");
            continue;
        }
        if (n == 0)
            continue;

        if (background) {
            int pid = fork();

            if (pid < 0) {
                printf("sh: fork failed\n");
                continue;
            }
            if (pid == 0) {
                exit(run_pipeline(cmds, n));   /* the child shell */
            }
            bg_add(pid);
            printf("sh: [%d] running in background\n", pid);
            continue;
        }

        status = run_pipeline(cmds, n);
        if (want_exit)
            break;
        if (status != 0)
            printf("sh: %s exited with %d\n", cmds[n - 1].argv[0], status);
    }

    return exit_code;
}
