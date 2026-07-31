#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <string.h>
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
        } else if (strcmp(argv[0], "exit") == 0) {
            printk("Goodbye.\n");
            sys_exit(0);
        } else if (strcmp(argv[0], "ls") == 0) {
            printk("ls: not yet implemented (no disk)\n");
        } else if (strcmp(argv[0], "cat") == 0) {
            printk("cat: not yet implemented (no disk)\n");
        } else {
            printk("Unknown command: ");
            printk(argv[0]);
            printk("\n");
        }
    }
}
