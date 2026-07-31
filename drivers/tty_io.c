#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/system.h>

struct tty_struct tty_table[1];

extern void con_init(void);
extern void con_write(struct tty_struct *tty);

void tty_init(void)
{
    con_init();
}

void tty_read(struct tty_struct *tty, char *buf, int nr)
{
}

void tty_write(struct tty_struct *tty, const char *buf, int nr)
{
    int i;

    for (i = 0; i < nr; i++) {
        if (tty->write_cnt >= TTY_BUF_SIZE) break;
        tty->write_buf[tty->write_head] = buf[i];
        tty->write_head = (tty->write_head + 1) % TTY_BUF_SIZE;
        tty->write_cnt++;
    }

    con_write(tty);
}
