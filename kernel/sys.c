#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <string.h>
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

int sys_lseek(unsigned int fd, long offset, int origin)
{
    struct file *f;

    if (fd >= NR_OPEN || !current->filp[fd])
        return -1;
    f = current->filp[fd];

    switch (origin) {
    case 0:                       /* SEEK_SET */
        f->f_pos = offset;
        break;
    case 1:                       /* SEEK_CUR */
        f->f_pos += offset;
        break;
    case 2:                       /* SEEK_END */
        f->f_pos = f->f_inode->i_size + offset;
        break;
    default:
        return -1;
    }
    return f->f_pos;
}

int sys_dup(unsigned int fildes)
{
    int fd;

    if (fildes >= NR_OPEN || !current->filp[fildes])
        return -1;

    for (fd = 0; fd < NR_OPEN; fd++)
        if (!current->filp[fd])
            break;
    if (fd >= NR_OPEN)
        return -1;

    current->filp[fd] = current->filp[fildes];
    current->filp[fd]->f_count++;
    return fd;
}

int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
    struct file *f;

    if (oldfd >= NR_OPEN || !current->filp[oldfd])
        return -1;
    if (newfd >= NR_OPEN)
        return -1;
    if (oldfd == newfd)
        return newfd;

    /* close the target slot first, then share the source */
    if (current->filp[newfd]) {
        f = current->filp[newfd];
        f->f_count--;
        if (f->f_count == 0)
            iput(f->f_inode);
    }
    current->filp[newfd] = current->filp[oldfd];
    current->filp[newfd]->f_count++;
    return newfd;
}

/* Create a regular file (mode carries S_IFREG). */
int sys_mknod(const char *filename, int mode)
{
    char dirpath[64], name[15];
    struct m_inode *dir, *inode;
    unsigned short ino;
    int namelen;

    if (split_path(filename, dirpath, name) < 0)
        return -1;
    namelen = (int)strlen(name);

    dir = namei(dirpath);
    if (!dir)
        return -1;
    if (!(dir->i_mode & S_IFDIR)) {
        iput(dir);
        return -1;
    }
    if (dir_lookup(dir, name, namelen, &ino) == 0) {
        iput(dir);
        return -1;              /* already exists */
    }

    inode = new_inode(dir->i_dev);
    if (!inode) {
        iput(dir);
        return -1;
    }
    ino = inode->i_num;

    if (dir_add_entry(dir, name, namelen, ino) < 0) {
        free_inode(inode);
        iput(inode);
        iput(dir);
        return -1;
    }

    inode->i_mode = mode;
    inode->i_uid = 0;
    inode->i_size = 0;
    inode->i_mtime = 0;
    inode->i_gid = 0;
    inode->i_nlinks = 1;
    inode->i_dirt = 1;

    iput(dir);
    iput(inode);
    return 0;
}

/* Create a directory (mode carries S_IFDIR); gets a data zone with
   '.' and '..' entries, like real minix. */
int sys_mkdir(const char *dirname, int mode)
{
    char dirpath[64], name[15];
    struct m_inode *dir, *inode;
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    unsigned short ino;
    int namelen, block;

    if (split_path(dirname, dirpath, name) < 0)
        return -1;
    namelen = (int)strlen(name);

    dir = namei(dirpath);
    if (!dir)
        return -1;
    if (!(dir->i_mode & S_IFDIR)) {
        iput(dir);
        return -1;
    }
    if (dir_lookup(dir, name, namelen, &ino) == 0) {
        iput(dir);
        return -1;
    }

    inode = new_inode(dir->i_dev);
    if (!inode) {
        iput(dir);
        return -1;
    }
    ino = inode->i_num;

    block = new_block(dir->i_dev);
    if (!block) {
        free_inode(inode);
        iput(inode);
        iput(dir);
        return -1;
    }

    bh = bread(dir->i_dev, block);
    if (!bh) {
        free_block(dir->i_dev, block);
        free_inode(inode);
        iput(inode);
        iput(dir);
        return -1;
    }
    de = (struct minix_dir_entry *)bh->b_data;
    de[0].inode = ino;                       /* "." -> self */
    de[0].name[0] = '.';
    de[0].name[1] = '\0';
    de[1].inode = dir->i_num;                /* ".." -> parent */
    de[1].name[0] = '.';
    de[1].name[1] = '.';
    de[1].name[2] = '\0';
    bh->b_dirt = 1;
    brelse(bh);

    inode->i_zone[0] = block;
    inode->i_mode = mode;
    inode->i_uid = 0;
    inode->i_size = 2 * sizeof(struct minix_dir_entry);
    inode->i_mtime = 0;
    inode->i_gid = 0;
    inode->i_nlinks = 2;
    inode->i_dirt = 1;

    if (dir_add_entry(dir, name, namelen, ino) < 0) {
        free_block(dir->i_dev, block);
        free_inode(inode);
        iput(inode);
        iput(dir);
        return -1;
    }
    dir->i_nlinks++;
    dir->i_dirt = 1;

    iput(dir);
    iput(inode);
    return 0;
}
/* Remove a regular file: clear its parent-dir entry and drop its link
   count; at zero links the inode and its data zones are freed. */
int sys_unlink(const char *filename)
{
    char dirpath[64], name[15];
    struct m_inode *dir, *inode;
    unsigned short ino;
    int namelen;

    if (split_path(filename, dirpath, name) < 0)
        return -1;
    namelen = (int)strlen(name);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    dir = namei(dirpath);
    if (!dir)
        return -1;
    if (!(dir->i_mode & S_IFDIR)) {
        iput(dir);
        return -1;
    }
    if (dir_lookup(dir, name, namelen, &ino) < 0) {
        iput(dir);
        return -1;              /* not found */
    }

    inode = iget(dir->i_dev, ino);
    if (!inode) {
        iput(dir);
        return -1;
    }
    if (inode->i_mode & S_IFDIR) {
        iput(inode);            /* use rmdir for directories */
        iput(dir);
        return -1;
    }

    if (dir_remove_entry(dir, name, namelen) < 0) {
        iput(inode);
        iput(dir);
        return -1;
    }

    inode->i_nlinks--;
    if (inode->i_nlinks == 0)
        truncate_inode(inode);  /* free data zones */
    inode->i_dirt = 1;

    iput(inode);
    iput(dir);
    return 0;
}

/* Remove an empty directory: '.'/'..' are checked, the parent link
   count is decremented, and the directory zone + inode are freed. */
int sys_rmdir(const char *dirname)
{
    char dirpath[64], name[15];
    struct m_inode *dir, *inode;
    unsigned short ino;
    int namelen, zone0;

    if (split_path(dirname, dirpath, name) < 0)
        return -1;
    namelen = (int)strlen(name);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    dir = namei(dirpath);
    if (!dir)
        return -1;
    if (!(dir->i_mode & S_IFDIR)) {
        iput(dir);
        return -1;
    }
    if (dir_lookup(dir, name, namelen, &ino) < 0) {
        iput(dir);
        return -1;
    }

    inode = iget(dir->i_dev, ino);
    if (!inode) {
        iput(dir);
        return -1;
    }
    if (!(inode->i_mode & S_IFDIR)) {
        iput(inode);
        iput(dir);
        return -1;
    }
    if (!dir_is_empty(inode)) {
        iput(inode);
        iput(dir);
        return -1;              /* not empty */
    }

    if (dir_remove_entry(dir, name, namelen) < 0) {
        iput(inode);
        iput(dir);
        return -1;
    }

    zone0 = inode->i_zone[0];
    if (zone0)
        free_block(inode->i_dev, zone0);
    inode->i_zone[0] = 0;

    dir->i_nlinks--;
    dir->i_dirt = 1;

    inode->i_nlinks = 0;
    inode->i_dirt = 1;
    free_inode(inode);

    iput(inode);
    iput(dir);
    return 0;
}
