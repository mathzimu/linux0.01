#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <string.h>
#include <unistd.h>
#include <asm/segment.h>
#include <asm/system.h>

extern int sys_exit(int ret);

#define SHELL_BUF_SIZE 256
#define MAX_ARGS 16

extern void con_init(void);

static void print_prompt(void)
{
    printk("$ ");
}

static int read_line(char *buf, int size)
{
    int i = 0;
    char c;

    while (i < size - 1) {
        if (tty_table[0].read_cnt == 0) {
            current->state = TASK_INTERRUPTIBLE;
            tty_table[0].read_waiter = current;
            if (tty_table[0].read_cnt == 0)
                schedule();
            tty_table[0].read_waiter = NULL;
            current->state = TASK_RUNNING;
            continue;
        }

        c = tty_table[0].read_buf[tty_table[0].read_tail];
        tty_table[0].read_tail = (tty_table[0].read_tail + 1) % TTY_BUF_SIZE;
        tty_table[0].read_cnt--;

        if (c == '\n') {
            buf[i] = '\0';
            printk("\n");
            return i;
        }
        if (c == '\b') {
            if (i > 0) {
                i--;
                tty_write(&tty_table[0], "\b \b", 3);
            }
            continue;
        }

        buf[i++] = c;
        tty_write(&tty_table[0], &c, 1);
    }

    buf[i] = '\0';
    return i;
}

static int parse_args(char *cmd, char **argv, int max_args)
{
    int argc = 0;
    int in_word = 0;

    while (*cmd && argc < max_args - 1) {
        if (*cmd == ' ' || *cmd == '\t') {
            *cmd = '\0';
            in_word = 0;
        } else {
            if (!in_word) {
                argv[argc++] = cmd;
                in_word = 1;
            }
        }
        cmd++;
    }

    argv[argc] = NULL;
    return argc;
}

static void cmd_echo(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        printk("%s", argv[i]);
        if (i < argc - 1) printk(" ");
    }
    printk("\n");
}

static void cmd_help(int argc, char **argv)
{
    printk("Minimal Linux 0.01 Shell\n");
    printk("Available commands:\n");
    printk("  echo    - Echo text\n");
    printk("  help    - Show this help\n");
    printk("  ps      - List processes\n");
    printk("  clear   - Clear screen\n");
    printk("  exit    - Exit shell\n");
    printk("  pid     - getpid() via int 0x80\n");
    printk("  time    - uptime via sys_time\n");
    printk("  sys     - syscall path demo (getpid/time/write/open/close)\n");
    printk("  spawn   - fork() demo: two children print and exit\n");
    printk("  sig     - pause/kill demo: SIGINT kills a paused child\n");
    printk("  ls      - list a directory (via open/read/close)\n");
    printk("  cat     - print a file (via open/read/close)\n");
    printk("  sync    - write back dirty buffers/inodes\n");
    printk("  wtest   - write a file [path] through the write path\n");
    printk("  touch   - create a file (sys_mknod)\n");
    printk("  mkdir   - create a directory (sys_mkdir)\n");
    printk("  ppid    - getppid() demo\n");
    printk("  fdtest  - dup() demo: fds share the file offset\n");
    printk("  seektest- lseek() demo: SEEK_SET / SEEK_END\n");
}

static void cmd_ps(int argc, char **argv)
{
    int i;
    printk("PID\tSTATE\tCOUNTER\n");

    for (i = 0; i < NR_TASKS; i++) {
        if (task[i] == NULL) continue;
        printk("%d\t", task[i]->pid);
        printk("%d\t", task[i]->state);
        printk("%d\n", task[i]->counter);
    }
}

/* --- syscall-path test commands (all through int 0x80) --- */

static void cmd_pid(int argc, char **argv)
{
    printk("getpid() = %d\n", getpid());
}

static void cmd_time(int argc, char **argv)
{
    unsigned long t = 0;
    printk("time() = %d (uptime seconds)\n", time(&t));
    if (t != 0)
        printk("*tloc = %d\n", t);
}

static void cmd_sys(int argc, char **argv)
{
    const char *msg = "sys: write() works\n";
    int fd, r;

    printk("sys: getpid() = %d\n", getpid());
    printk("sys: time()   = %d\n", time(NULL));

    r = write(1, msg, strlen(msg));
    printk("sys: write(1, msg, %d) = %d\n", strlen(msg), r);

    fd = open("/no/such/file", 0);
    printk("sys: open(\"/no/such/file\") = %d\n", fd);
    if (fd >= 0) close(fd);

    r = close(-1);
    printk("sys: close(-1) = %d\n", r);
}

static void cmd_spawn(int argc, char **argv)
{
    int i;

    for (i = 0; i < 2; i++) {
        int pid = fork();
        if (pid < 0) {
            printk("spawn: fork() failed\n");
            break;
        }
        if (pid == 0) {
            /* child: report and die */
            printk("[child] getpid=%d time=%d, exiting\n",
                   getpid(), time(NULL));
            exit(0);
        }
        printk("[parent] fork #%d -> pid %d\n", i + 1, pid);
    }
    printk("[parent] spawn done\n");
}

static void cmd_sig(int argc, char **argv)
{
    int pid = fork();

    if (pid < 0) {
        printk("sig: fork() failed\n");
        return;
    }
    if (pid == 0) {
        /* child: pause until the parent SIGINTs us; the signal is
           delivered at the syscall return, default action kills us */
        printk("[child] pid=%d pausing...\n", getpid());
        pause();
        printk("[child] woke up (BUG: survived SIGINT)\n");
        exit(0);
    }
    printk("[parent] kill(%d, SIGINT)\n", pid);
    if (kill(pid, 2) == 0)
        printk("[parent] kill() ok\n");
    else
        printk("[parent] kill() failed\n");
    printk("[parent] sig done\n");
}

/* --- filesystem commands (open/read/close go through int 0x80) --- */

static void cmd_ls(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    char buf[16];                 /* one minix dir entry */
    int fd, n;

    fd = open(path, 0);           /* O_RDONLY */
    if (fd < 0) {
        printk("ls: %s: no such file\n", path);
        return;
    }
    if (!(current->filp[fd]->f_inode->i_mode & 0x4000)) {
        printk("ls: %s: not a directory\n", path);
        close(fd);
        return;
    }

    while ((n = read(fd, buf, 16)) == 16) {
        unsigned short ino = ((unsigned short *)buf)[0];
        char name[15];
        int k;
        if (!ino)
            continue;             /* free entry */
        for (k = 0; k < 14 && buf[2 + k]; k++)
            name[k] = buf[2 + k];
        name[k] = '\0';
        printk("%d  %s\n", ino, name);
    }
    close(fd);
}

static void cmd_cat(int argc, char **argv)
{
    char buf[256];
    int fd, n, i;

    if (argc < 2) {
        printk("cat: usage: cat <file>\n");
        return;
    }
    fd = open(argv[1], 0);
    if (fd < 0) {
        printk("cat: %s: no such file\n", argv[1]);
        return;
    }
    if (current->filp[fd]->f_inode->i_mode & 0x4000) {
        printk("cat: %s: is a directory\n", argv[1]);
        close(fd);
        return;
    }

    while ((n = read(fd, buf, 256)) > 0) {
        for (i = 0; i < n; i++)
            printk("%c", buf[i]);
    }
    printk("\n");
    close(fd);
}

static void cmd_sync(int argc, char **argv)
{
    if (sync() == 0)
        printk("sync: all dirty buffers written\n");
    else
        printk("sync: failed\n");
}

static void cmd_writetest(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/hello.txt";
    const char *msg = "Minimal Linux 0.01 write path works!\n";
    int fd, n;

    fd = open(path, 1);           /* O_WRONLY */
    if (fd < 0) {
        printk("wtest: open(%s, write) failed\n", path);
        return;
    }
    n = write(fd, msg, strlen(msg));
    printk("wtest: wrote %d bytes to %s\n", n, path);
    close(fd);
    sync();
    printk("wtest: synced. 'cat %s' should show new text\n", path);
}

static void cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        printk("touch: usage: touch <file>\n");
        return;
    }
    if (mknod(argv[1], S_IFREG | 0644) == 0)
        printk("touch: created %s\n", argv[1]);
    else
        printk("touch: failed (exists / bad path?)\n");
}

static void cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        printk("mkdir: usage: mkdir <dir>\n");
        return;
    }
    if (mkdir(argv[1], S_IFDIR | 0755) == 0)
        printk("mkdir: created %s\n", argv[1]);
    else
        printk("mkdir: failed (exists / bad path?)\n");
}

static void cmd_ppid(int argc, char **argv)
{
    printk("getpid()=%d getppid()=%d\n", getpid(), getppid());
}

static void cmd_fdtest(int argc, char **argv)
{
    int fd, d;

    fd = open("/readme.txt", 0);
    if (fd < 0) {
        printk("fdtest: open failed\n");
        return;
    }
    d = dup(fd);
    printk("fdtest: open=%d dup=%d\n", fd, d);

    if (lseek(fd, 20, SEEK_SET) != 20)
        printk("fdtest: lseek failed\n");
    else
        printk("fdtest: lseek(fd,20) -> dup's offset = %d (shared f_pos)\n",
               lseek(d, 0, SEEK_CUR));

    close(d);
    close(fd);
}

static void cmd_seektest(int argc, char **argv)
{
    int fd, n;
    char buf[17];

    fd = open("/hello.txt", 0);
    if (fd < 0) {
        printk("seektest: open failed\n");
        return;
    }
    n = lseek(fd, 4, SEEK_SET);           /* skip "Hell" */
    if (n != 4) {
        printk("seektest: lseek failed (%d)\n", n);
        close(fd);
        return;
    }
    n = read(fd, buf, 10);
    buf[n] = '\0';
    printk("seektest: @4 read %d: \"%s\"\n", n, buf);

    n = lseek(fd, -4, SEEK_END);          /* last 4 bytes */
    n = read(fd, buf, 10);
    buf[n] = '\0';
    printk("seektest: @end-4: \"%s\"\n", buf);

    close(fd);
}

void shell_main(void)
{
    char buf[SHELL_BUF_SIZE];
    char *argv[MAX_ARGS + 1];
    int argc;

    printk("Minimal Linux 0.01 Equivalent Kernel\n");
    printk("Type 'help' for commands\n");

    while (1) {
        print_prompt();

        if (read_line(buf, SHELL_BUF_SIZE) <= 0)
            continue;

        argc = parse_args(buf, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc, argv);
        } else if (strcmp(argv[0], "help") == 0) {
            cmd_help(argc, argv);
        } else if (strcmp(argv[0], "ps") == 0) {
            cmd_ps(argc, argv);
        } else if (strcmp(argv[0], "clear") == 0) {
            con_init();
        } else if (strcmp(argv[0], "pid") == 0) {
            cmd_pid(argc, argv);
        } else if (strcmp(argv[0], "time") == 0) {
            cmd_time(argc, argv);
        } else if (strcmp(argv[0], "sys") == 0) {
            cmd_sys(argc, argv);
        } else if (strcmp(argv[0], "spawn") == 0) {
            cmd_spawn(argc, argv);
        } else if (strcmp(argv[0], "sig") == 0) {
            cmd_sig(argc, argv);
        } else if (strcmp(argv[0], "ls") == 0) {
            cmd_ls(argc, argv);
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc, argv);
        } else if (strcmp(argv[0], "sync") == 0) {
            cmd_sync(argc, argv);
        } else if (strcmp(argv[0], "wtest") == 0) {
            cmd_writetest(argc, argv);
        } else if (strcmp(argv[0], "touch") == 0) {
            cmd_touch(argc, argv);
        } else if (strcmp(argv[0], "mkdir") == 0) {
            cmd_mkdir(argc, argv);
        } else if (strcmp(argv[0], "ppid") == 0) {
            cmd_ppid(argc, argv);
        } else if (strcmp(argv[0], "fdtest") == 0) {
            cmd_fdtest(argc, argv);
        } else if (strcmp(argv[0], "seektest") == 0) {
            cmd_seektest(argc, argv);
        } else if (strcmp(argv[0], "exit") == 0) {
            printk("Goodbye.\n");
            exit(0);
        } else {
            printk("Unknown command: ");
            printk(argv[0]);
            printk("\n");
        }
    }
}
