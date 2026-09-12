/* sh — a Ring3 user-mode shell (M2-2).
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
 * Builtins: cd, pwd, exit, help.  Anything else is looked up as
 * /bin/<name> and run as a child process.
 */

#include "lib.h"

#define LINE_MAX 128
#define ARG_MAX  16

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

static int parse_args(char *line, char **argv, int max)
{
    int argc = 0;

    while (*line && argc < max - 1) {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
    }
    argv[argc] = NULL;
    return argc;
}

/* Run /bin/<argv[0]> as a child and wait for it. */
static int run_program(char **argv)
{
    char path[64];
    int pid;
    unsigned long code = 0;
    int i = 0;

    path[i++] = '/';
    path[i++] = 'b';
    path[i++] = 'i';
    path[i++] = 'n';
    path[i++] = '/';
    {
        const char *s = argv[0];
        while (*s && i < (int)sizeof(path) - 1)
            path[i++] = *s++;
    }
    path[i] = '\0';

    pid = fork();
    if (pid < 0) {
        printf("sh: fork failed\n");
        return -1;
    }
    if (pid == 0) {
        /* Child: replace ourselves with the program.  If execve fails,
           say so and exit non-zero so the parent can report it. */
        execve(path, argv, NULL);
        printf("sh: %s: cannot execute\n", path);
        exit(127);
    }

    waitpid(pid, &code, 0);
    return (int)code;
}

int main(int argc, char *argv[])
{
    char line[LINE_MAX];
    char *args[ARG_MAX];
    int n;

    printf("sh: user-mode shell (Ring3), pid=%d\n", getpid());
    printf("sh: builtins: cd pwd exit help; other names run /bin/<name>\n");

    for (;;) {
        write(1, "$ ", 2);

        n = read_line(line, sizeof(line));
        if (n < 0) {
            printf("\nsh: end of input, exiting\n");
            break;
        }
        if (n == 0)
            continue;

        if (parse_args(line, args, ARG_MAX) == 0)
            continue;

        if (strcmp(args[0], "exit") == 0)
            break;

        if (strcmp(args[0], "help") == 0) {
            printf("sh: cd <dir>  pwd  exit  help  <program> [args...]\n");
            continue;
        }

        if (strcmp(args[0], "pwd") == 0) {
            /* No getcwd() in this kernel: prove chdir() worked by
               listing the current directory instead. */
            printf("sh: cwd listing follows\n");
            args[0] = "ls";
            run_program(args);
            continue;
        }

        if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                printf("sh: usage: cd <dir>\n");
                continue;
            }
            if (chdir(args[1]) < 0)
                printf("sh: cd: %s: no such directory\n", args[1]);
            continue;
        }

        n = run_program(args);
        if (n != 0)
            printf("sh: %s exited with %d\n", args[0], n);
    }

    return 0;
}
