/* Pipes — ported from 1991 Linux 0.01 (fs/pipe.c).
   A pipe is an inode with i_pipe set; its buffer is a single free page
   (address stored in i_size).  i_zone[0]/i_zone[1] hold the write
   (head) and read (tail) offsets, 0..PAGE_SIZE-1.  read_pipe blocks on
   an empty pipe via sleep_on(), write_pipe on a full one; when the
   other end is closed (i_count != 2) reads see EOF and writes raise
   SIGPIPE. */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/segment.h>

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode) - PIPE_TAIL(inode)) & (PAGE_SIZE - 1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode) == PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode) == (PAGE_SIZE - 1))
#define INC_PIPE(p) ((p) = ((p) + 1) & (PAGE_SIZE - 1))

int read_pipe(struct m_inode *inode, char *buf, int count)
{
    char *b = buf;

    while (PIPE_EMPTY(*inode)) {
        wake_up(&inode->i_wait);
        if (inode->i_count != 2)        /* any writers left? */
            return 0;                   /* EOF */
        sleep_on(&inode->i_wait);
    }
    while (count > 0 && !PIPE_EMPTY(*inode)) {
        count--;
        put_fs_byte(((char *)inode->i_size)[PIPE_TAIL(*inode)], b++);
        INC_PIPE(PIPE_TAIL(*inode));
    }
    wake_up(&inode->i_wait);
    return b - buf;
}

int write_pipe(struct m_inode *inode, char *buf, int count)
{
    char *b = buf;

    wake_up(&inode->i_wait);
    if (inode->i_count != 2) {          /* no readers */
        current->signal |= (1 << (SIGPIPE - 1));
        return -1;
    }
    while (count-- > 0) {
        while (PIPE_FULL(*inode)) {
            wake_up(&inode->i_wait);
            if (inode->i_count != 2) {
                current->signal |= (1 << (SIGPIPE - 1));
                return b - buf;
            }
            sleep_on(&inode->i_wait);
        }
        ((char *)inode->i_size)[PIPE_HEAD(*inode)] = get_fs_byte(b++);
        INC_PIPE(PIPE_HEAD(*inode));
        wake_up(&inode->i_wait);
    }
    wake_up(&inode->i_wait);
    return b - buf;
}

static struct m_inode *get_pipe_inode(void)
{
    struct m_inode *inode;

    if (!(inode = get_empty_inode()))
        return NULL;
    if (!(inode->i_size = get_free_page())) {
        inode->i_count = 0;
        return NULL;
    }
    inode->i_count = 2;                 /* readers + writers */
    PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
    inode->i_pipe = 1;
    return inode;
}

int sys_pipe(unsigned long *fildes)
{
    struct m_inode *inode;
    struct file *f[2];
    int fd[2];
    int i, j;

    j = 0;
    for (i = 0; j < 2 && i < NR_FILE; i++)
        if (!file_table[i].f_count)
            (f[j++] = i + file_table)->f_count++;
    if (j == 1)
        f[0]->f_count = 0;
    if (j < 2)
        return -1;

    j = 0;
    for (i = 3; j < 2 && i < NR_OPEN; i++)   /* skip std fds */
        if (!current->filp[i]) {
            fd[j] = i;
            current->filp[i] = f[j];
            j++;
        }
    if (j == 1)
        current->filp[fd[0]] = NULL;
    if (j < 2) {
        f[0]->f_count = f[1]->f_count = 0;
        return -1;
    }

    if (!(inode = get_pipe_inode())) {
        current->filp[fd[0]] = current->filp[fd[1]] = NULL;
        f[0]->f_count = f[1]->f_count = 0;
        return -1;
    }
    f[0]->f_inode = f[1]->f_inode = inode;
    f[0]->f_pos = f[1]->f_pos = 0;
    f[0]->f_mode = 1;                   /* read end */
    f[1]->f_mode = 2;                   /* write end */
    put_fs_long(fd[0], 0 + fildes);
    put_fs_long(fd[1], 1 + fildes);
    return 0;
}
