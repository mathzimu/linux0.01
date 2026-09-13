#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/fs.h>
#include <asm/system.h>

struct tty_struct tty_table[1];

extern void con_init(void);
extern void con_write(struct tty_struct *tty);
extern void kbd_init(void);

/* The console as an open file.
 *
 * sys_read/sys_write used to special-case fd 0/1/2 and talk to the tty
 * directly, never consulting the file descriptor table.  That made
 * redirection impossible: a shell could dup2() a file onto fd 1 as much
 * as it liked, the kernel still wrote to the screen (and the Ring3
 * shell's `>` simply did not work).
 *
 * Now fds 0..2 are ordinary entries in task->filp[] holding this file.
 * It has no inode — that is the marker that says "the console" — so
 * every path that walks f_inode (file_read/file_write, iput on close,
 * the permission checks) must treat NULL as "not a real file" and take
 * the tty branch instead.  Writing to the console goes through
 * tty_write(), reading comes from the tty ring buffer, exactly as
 * before; the only difference is that the fd table now decides. */
struct file tty_file = {
    2,                             /* f_mode: O_RDWR (the console branch
                                      of sys_read/sys_write ignores it) */
    0,                             /* f_flags                          */
    1,                             /* f_count (fork/exit keep this)    */
    NULL,                          /* f_inode: NULL = console          */
    0                              /* f_pos (meaningless for a tty)    */
};

void tty_init(void)
{
    con_init();
    kbd_init();
    serial_init();
}

void tty_read(struct tty_struct *tty, char *buf, int nr)
{
}

void tty_write(struct tty_struct *tty, const char *buf, int nr)
{
    int i;

    for (i = 0; i < nr; i++) {
        serial_putc(buf[i]);            /* mirror to COM1 for testing */
        if (tty->write_cnt >= TTY_BUF_SIZE) break;
        tty->write_buf[tty->write_head] = buf[i];
        tty->write_head = (tty->write_head + 1) % TTY_BUF_SIZE;
        tty->write_cnt++;
    }

    con_write(tty);
}
