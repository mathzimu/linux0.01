#ifndef _TTY_H
#define _TTY_H

#define TTY_BUF_SIZE 1024

struct tty_struct {
    char read_buf[TTY_BUF_SIZE];
    int read_head;
    int read_tail;
    int read_cnt;
    char write_buf[TTY_BUF_SIZE];
    int write_head;
    int write_tail;
    int write_cnt;
    unsigned long flags;
    struct task_struct *read_waiter;
    struct task_struct *write_waiter;
};

extern struct tty_struct tty_table[1];

void tty_init(void);
void tty_read(struct tty_struct *tty, char *buf, int nr);
void tty_write(struct tty_struct *tty, const char *buf, int nr);
void kbd_interrupt_handler(int scancode);
void serial_init(void);
void serial_putc(char c);

#endif
