#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_time(unsigned long *tloc)
{
    int i = jiffies / HZ;

    /* Match Linux 0.01 semantics: store the time at *tloc when given. */
    if (tloc)
        put_fs_long(i, tloc);
    return i;
}

long sys_write(unsigned int fd, const char *buf, unsigned long count)
{
    long result;
    char c;

    if (fd == 1 || fd == 2) {
        unsigned long i;
        for (i = 0; i < count; i++) {
            c = get_fs_byte(buf + i);
            if (c == '\n')
                tty_write(&tty_table[0], "\r", 1);
            tty_write(&tty_table[0], &c, 1);
        }
        return count;
    }

    if (fd == 0)
        return -1;

    if (fd >= NR_OPEN || !current->filp[fd])
        return -1;

    struct file *f = current->filp[fd];
    if (f->f_mode == 0)
        return -1;

    result = file_write(f->f_inode, f, buf, count);
    if (result > 0)
        f->f_pos += result;
    return result;
}

long sys_read(unsigned int fd, char *buf, unsigned long count)
{
    long result;
    char c;

    if (fd == 0) {
        unsigned long i;
        for (i = 0; i < count; i++) {
            if (tty_table[0].read_cnt == 0) {
                if (i) break;
                current->state = TASK_INTERRUPTIBLE;
                tty_table[0].read_waiter = current;
                if (tty_table[0].read_cnt == 0)
                    schedule();
                tty_table[0].read_waiter = NULL;
                current->state = TASK_RUNNING;
                continue;  // Don't i--, just retry
            }
            c = tty_table[0].read_buf[tty_table[0].read_tail];
            tty_table[0].read_tail = (tty_table[0].read_tail + 1) % TTY_BUF_SIZE;
            tty_table[0].read_cnt--;
            put_fs_byte(c, buf + i);
            if (c == '\n') {
                i++;
                break;
            }
        }
        current->state = TASK_RUNNING;
        return i;
    }

    if (fd >= NR_OPEN || !current->filp[fd])
        return -1;

    struct file *f = current->filp[fd];
    if (f->f_mode == 1)
        return -1;

    result = file_read(f->f_inode, f, buf, count);
    if (result > 0)
        f->f_pos += result;
    return result;
}

int sys_open(const char *filename, int flag)
{
    int i, fd;
    struct file *f;
    struct m_inode *inode;

    /* fds 0/1/2 are reserved for stdin/stdout/stderr (the tty), so
       the first real file gets fd 3 — matching Unix convention. */
    for (fd = 3; fd < NR_OPEN; fd++) {
        if (!current->filp[fd]) break;
    }
    if (fd >= NR_OPEN) return -1;

    for (i = 0; i < NR_FILE; i++) {
        if (!file_table[i].f_count) break;
    }
    if (i >= NR_FILE) return -1;

    inode = namei(filename);
    if (!inode) return -1;

    f = &file_table[i];
    f->f_mode = flag;
    f->f_flags = 0;
    f->f_count = 1;
    f->f_inode = inode;
    f->f_pos = 0;

    current->filp[fd] = f;
    return fd;
}

int sys_close(unsigned int fd)
{
    struct file *f;

    if (fd >= NR_OPEN) return -1;
    f = current->filp[fd];
    if (!f) return -1;

    current->filp[fd] = NULL;
    f->f_count--;
    if (f->f_count == 0) {
        iput(f->f_inode);
    }
    return 0;
}

int sys_kill(int pid, int sig)
{
    struct task_struct *p;

    /* pid equals the task[] index: task[0]=init(pid 0), children get
       pid = slot index (see sys_fork). */
    if (pid < 0 || pid >= NR_TASKS)
        return -1;
    p = task[pid];
    if (!p)
        return -1;
    if (sig == 0)
        return 0;                    /* existence check only */
    if (sig < 1 || sig >= 32)
        return -1;

    p->signal |= (1 << sig);
    /* wake a task that is sleeping interruptibly (e.g. in sys_pause) */
    if (p->state == TASK_INTERRUPTIBLE)
        p->state = TASK_RUNNING;
    return 0;
}

int sys_sync(void)
{
    int i;

    for (i = 0; i < NR_SUPER; i++) {
        if (super_block[i].s_dev)
            sync_dev(super_block[i].s_dev);
    }
    return 0;
}