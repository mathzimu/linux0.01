#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <asm/segment.h>

/* System time (seconds since epoch-ish boot).  sys_stime() can set it. */
unsigned long boot_time = 0;

int sys_time(unsigned long *tloc)
{
    int i = boot_time + jiffies / HZ;

    /* Match Linux 0.01 semantics: store the time at *tloc when given. */
    if (tloc)
        put_fs_long(i, tloc);
    return i;
}

int sys_stime(unsigned long *tptr)
{
    boot_time = get_fs_long(tptr) - jiffies / HZ;
    return 0;
}

/* Change the current working directory.  Relative paths are resolved
   against the *old* pwd (chdir("sub") inside /docs -> /docs/sub). */
int sys_chdir(const char *filename)
{
    struct m_inode *inode;

    inode = namei(filename);
    if (!inode)
        return -1;
    if (!(inode->i_mode & 0x4000)) {
        iput(inode);
        return -1;
    }
    /* cd'ing into a directory requires the x bit: without it the
       directory cannot be searched, so it cannot become the cwd. */
    if (!permission(inode, MAY_EXEC)) {
        iput(inode);
        return -1;
    }
    if (current->pwd)
        iput(current->pwd);
    current->pwd = inode;
    return 0;
}

long sys_write(unsigned int fd, const char *buf, unsigned long count)
{
    long result;
    char c;
    struct file *f;

    if (fd >= NR_OPEN)
        return -1;

    f = current->filp[fd];

    /* No descriptor, or the console (f_inode == NULL): write to the tty.
       fds 0..2 are pre-opened on the console for every task, but the
       check stays tolerant so a task that closed them still behaves. */
    if (!f || !f->f_inode) {
        unsigned long i;

        if (fd == 0)
            return -1;                  /* stdin is not writable */

        for (i = 0; i < count; i++) {
            c = get_fs_byte(buf + i);
            if (c == '\n')
                tty_write(&tty_table[0], "\r", 1);
            tty_write(&tty_table[0], &c, 1);
        }
        return count;
    }

    if (f->f_mode == 0)
        return -1;
    if (f->f_inode->i_pipe)
        return write_pipe(f->f_inode, (char *)buf, (int)count);

    result = file_write(f->f_inode, f, buf, count);
    if (result > 0)
        f->f_pos += result;
    return result;
}

long sys_read(unsigned int fd, char *buf, unsigned long count)
{
    long result;
    char c;
    struct file *f;

    if (fd >= NR_OPEN)
        return -1;

    f = current->filp[fd];

    /* Console (or an fd nobody opened): read from the tty buffer. */
    if (!f || !f->f_inode) {
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
                /* Retry this pass.  "continue" alone is not enough: a
                   for-loop still runs its i++ step, so the loop would end
                   with i == count and return 1 having read nothing (the
                   caller's buffer keeps whatever it held). */
                i--;
                continue;
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
    if (f->f_inode->i_pipe)
        return read_pipe(f->f_inode, buf, count);
    if (f->f_mode == 1)
        return -1;

    result = file_read(f->f_inode, f, buf, count);
    if (result > 0)
        f->f_pos += result;
    return result;
}

int sys_open(const char *filename, int flag, int mode)
{
    int i, fd;
    struct file *f;
    struct m_inode *inode;

    /* fds 0/1/2 already hold the console (tty_file), so the first file
       opened gets fd 3 — matching Unix convention.  Closing 0/1/2 frees
       the slot, exactly like a normal descriptor. */
    for (fd = 3; fd < NR_OPEN; fd++) {
        if (!current->filp[fd]) break;
    }
    if (fd >= NR_OPEN) return -1;

    for (i = 0; i < NR_FILE; i++) {
        if (!file_table[i].f_count) break;
    }
    if (i >= NR_FILE) return -1;

    inode = namei(filename);
    if (!inode && (flag & O_CREAT)) {
        /* create the file: split parent dir + basename (Linux 0.01
           open_namei logic, simplified) */
        char dirpath[64], name[15];
        struct m_inode *dir;
        unsigned short ino;
        int namelen;

        if (split_path(filename, dirpath, name) < 0)
            return -1;
        dir = namei(dirpath);
        if (!dir) return -1;
        if (!(dir->i_mode & 0x4000)) {
            iput(dir);
            return -1;
        }
        /* Creating an entry modifies the directory, so it needs write
           permission on it — this is what stops a user from creating
           files in someone else's 0755 directory. */
        if (!permission(dir, MAY_WRITE)) {
            iput(dir);
            return -1;
        }
        namelen = (int)strlen(name);
        if (dir_lookup(dir, name, namelen, &ino) == 0) {
            inode = iget(dir->i_dev, ino);     /* already exists */
            iput(dir);
            if (!inode) return -1;
        } else {
            inode = new_inode(dir->i_dev);
            if (!inode) {
                iput(dir);
                return -1;
            }
            mode &= 0777 & ~current->umask;
            inode->i_mode = (unsigned short)(mode | 0x8000);  /* S_IFREG */
            inode->i_uid = current->euid;      /* the file belongs to its creator */
            inode->i_gid = (unsigned char)current->egid;
            inode->i_dirt = 1;
            if (dir_add_entry(dir, name, namelen,
                              (unsigned short)inode->i_num) < 0) {
                free_inode(inode);
                iput(inode);
                iput(dir);
                return -1;
            }
            iput(dir);
        }
    }
    if (!inode) return -1;

    /* Opening an existing file needs the matching triad bit: read for
       O_RDONLY, write for O_WRONLY/O_RDWR.  O_TRUNC is a write. */
    {
        int mask = 0;

        if ((flag & 3) == O_RDONLY)
            mask |= MAY_READ;
        if ((flag & 3) == O_WRONLY || (flag & 3) == O_RDWR)
            mask |= MAY_WRITE;
        if (flag & O_TRUNC)
            mask |= MAY_WRITE;

        if (mask && !permission(inode, mask)) {
            iput(inode);
            return -1;
        }
    }

    if ((flag & O_TRUNC) && !(inode->i_mode & 0x4000))
        truncate_inode(inode);                 /* empty the file */

    f = &file_table[i];
    f->f_mode = flag;
    f->f_flags = 0;
    f->f_count = 1;
    f->f_inode = inode;
    f->f_pos = 0;

    current->filp[fd] = f;
    return fd;
}

int sys_creat(const char *pathname, int mode)
{
    return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

int sys_close(unsigned int fd)
{
    struct file *f;

    if (fd >= NR_OPEN) return -1;
    f = current->filp[fd];
    if (!f) return -1;

    current->filp[fd] = NULL;
    f->f_count--;
    if (f->f_count == 0 && f->f_inode) {    /* NULL inode = the console */
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
        if (f->f_count == 0 && f->f_inode)   /* NULL = console */
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
    if (!permission(dir, MAY_WRITE)) {      /* creating modifies the dir */
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
    inode->i_uid = current->euid;
    inode->i_size = 0;
    inode->i_mtime = 0;
    inode->i_gid = (unsigned char)current->egid;
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
    if (!permission(dir, MAY_WRITE)) {      /* creating modifies the dir */
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
    inode->i_mode = (unsigned short)((mode & 0777) | 0x4000);  /* S_IFDIR */
    inode->i_uid = current->euid;
    inode->i_size = 2 * sizeof(struct minix_dir_entry);
    inode->i_mtime = 0;
    inode->i_gid = (unsigned char)current->egid;
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
    if (!permission(dir, MAY_WRITE)) {      /* removing modifies the dir */
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
    int namelen;

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
    if (!permission(dir, MAY_WRITE)) {      /* removing modifies the dir */
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

    /* free every data zone the (now-empty) directory owns — including any
       indirect block it grew into — so rmdir never leaks blocks */
    free_dir_zones(inode);
    inode->i_dirt = 1;

    dir->i_nlinks--;
    dir->i_dirt = 1;

    inode->i_nlinks = 0;
    inode->i_dirt = 1;
    free_inode(inode);

    iput(inode);
    iput(dir);
    return 0;
}

/* ------------------------------------------------------------------
 * Linux 0.01 alignment (sys_call_table numbers 15..66): stat, chmod,
 * chown, access, umask, uname, utime, uid/gid, alarm, nice, times,
 * process groups, chroot.  Anything 0.01 left as -ENOSYS stays -1.
 * ------------------------------------------------------------------ */

static int cp_stat(struct m_inode *inode, struct stat *statbuf)
{
    struct stat tmp;
    int i;

    tmp.st_dev = inode->i_dev;
    tmp.st_ino = (unsigned short)inode->i_num;
    tmp.st_mode = inode->i_mode;
    tmp.st_nlink = inode->i_nlinks;
    tmp.st_uid = inode->i_uid;
    tmp.st_gid = inode->i_gid;
    tmp.st_rdev = inode->i_zone[0];
    tmp.st_size = inode->i_size;
    tmp.st_atime = inode->i_mtime;
    tmp.st_mtime = inode->i_mtime;
    tmp.st_ctime = inode->i_mtime;
    for (i = 0; i < (int)sizeof(tmp); i++)
        put_fs_byte(((char *)&tmp)[i], &((char *)statbuf)[i]);
    return 0;
}

int sys_stat(const char *filename, struct stat *statbuf)
{
    struct m_inode *inode = namei(filename);
    int r;

    if (!inode)
        return -1;
    r = cp_stat(inode, statbuf);
    iput(inode);
    return r;
}

int sys_fstat(unsigned int fd, struct stat *statbuf)
{
    struct file *f;
    struct m_inode *inode;

    if (fd >= NR_OPEN || !(f = current->filp[fd]) || !(inode = f->f_inode))
        return -1;
    return cp_stat(inode, statbuf);
}

int sys_chmod(const char *filename, int mode)
{
    struct m_inode *inode = namei(filename);

    if (!inode)
        return -1;
    /* Only the owner (or root) may change a file's mode. */
    if (current->euid != inode->i_uid && !suser()) {
        iput(inode);
        return -1;
    }
    inode->i_mode = (unsigned short)((inode->i_mode & 0xF000) | (mode & 0777));
    inode->i_dirt = 1;
    iput(inode);
    return 0;
}

int sys_chown(const char *filename, int uid, int gid)
{
    struct m_inode *inode = namei(filename);

    if (!inode)
        return -1;
    /* Giving a file away is root's privilege only (POSIX). */
    if (!suser()) {
        iput(inode);
        return -1;
    }
    inode->i_uid = (unsigned short)uid;
    inode->i_gid = (unsigned char)gid;
    inode->i_dirt = 1;
    iput(inode);
    return 0;
}

/* access(path, mode): mode is a bitmask of R_OK(4)/W_OK(2)/X_OK(1), or
   F_OK(0) for "does it exist".  Same masks as permission() uses. */
int sys_access(const char *filename, int mode)
{
    struct m_inode *inode = namei(filename);
    int mask;
    int ok;

    if (!inode)
        return -1;

    mask = 0;
    if (mode & 4) mask |= MAY_READ;
    if (mode & 2) mask |= MAY_WRITE;
    if (mode & 1) mask |= MAY_EXEC;

    ok = mask ? permission(inode, mask) : 1;
    iput(inode);
    return ok ? 0 : -1;
}

int sys_utime(const char *filename, unsigned long *times)
{
    struct m_inode *inode = namei(filename);

    (void)times;
    if (!inode)
        return -1;
    /* Owner, or anyone allowed to write the file (Linux 0.01 rule). */
    if (current->euid != inode->i_uid && !permission(inode, MAY_WRITE)) {
        iput(inode);
        return -1;
    }
    inode->i_mtime = jiffies / HZ;
    inode->i_dirt = 1;
    iput(inode);
    return 0;
}

int sys_umask(int mask)
{
    int old = current->umask;

    current->umask = (unsigned short)(mask & 0777);
    return old;
}

int sys_uname(struct utsname *utsbuf)
{
    static const char *src[5] = { "linux", "teaching", "0.0.1",
                                  "#1 (0.01)", "i386" };
    char *dst = (char *)utsbuf;
    int i, k;

    for (i = 0; i < 5; i++) {
        for (k = 0; k < 8; k++)
            put_fs_byte(src[i][k] ? src[i][k] : 0, dst + i * 9 + k);
        put_fs_byte(0, dst + i * 9 + 8);
    }
    return 0;
}

int sys_setuid(int uid)
{
    if (current->euid == 0)
        current->uid = current->euid = current->suid = (unsigned short)uid;
    else if (current->uid == (unsigned short)uid ||
             current->suid == (unsigned short)uid)
        current->euid = (unsigned short)uid;
    else
        return -1;
    return 0;
}

int sys_getuid(void)
{
    return current->uid;
}

int sys_geteuid(void)
{
    return current->euid;
}

int sys_setgid(int gid)
{
    if (current->euid == 0)
        current->gid = current->egid = current->sgid = (unsigned short)gid;
    else if (current->gid == (unsigned short)gid ||
             current->sgid == (unsigned short)gid)
        current->egid = (unsigned short)gid;
    else
        return -1;
    return 0;
}

int sys_getgid(void)
{
    return current->gid;
}

int sys_getegid(void)
{
    return current->egid;
}

int sys_alarm(long seconds)
{
    current->alarm = (seconds > 0) ?
        (unsigned long)jiffies + HZ * (unsigned long)seconds : 0;
    return (int)seconds;
}

int sys_nice(long increment)
{
    if (current->priority - increment > 0)
        current->priority -= increment;
    return 0;
}

int sys_times(struct tms *tbuf)
{
    struct tms tmp;

    tmp.tms_utime = current->utime;
    tmp.tms_stime = current->stime;
    tmp.tms_cutime = current->cutime;
    tmp.tms_cstime = current->cstime;
    put_fs_long(tmp.tms_utime, &tbuf->tms_utime);
    put_fs_long(tmp.tms_stime, &tbuf->tms_stime);
    put_fs_long(tmp.tms_cutime, &tbuf->tms_cutime);
    put_fs_long(tmp.tms_cstime, &tbuf->tms_cstime);
    return (int)(jiffies / HZ);
}

int sys_setpgid(int pid, int pgid)
{
    struct task_struct *p;

    if (pid == 0)
        p = current;
    else if (pid < NR_TASKS && task[pid])
        p = task[pid];
    else
        return -1;
    if (pgid == 0)
        pgid = (int)p->pid;
    p->pgrp = (unsigned long)pgid;
    return 0;
}

int sys_getpgrp(void)
{
    return (int)current->pgrp;
}

int sys_setsid(void)
{
    current->pgrp = current->pid;
    current->session = current->pid;
    current->leader = 1;
    return (int)current->pid;
}

int sys_chroot(const char *filename)
{
    struct m_inode *inode = namei(filename);

    if (!inode)
        return -1;
    if (!(inode->i_mode & S_IFDIR)) {
        iput(inode);
        return -1;
    }
    if (current->root)
        iput(current->root);
    current->root = inode;
    return 0;
}

/* --- stubs: Linux 0.01 left these as -ENOSYS (or not yet ported) --- */

int sys_break(void) { return -1; }
int sys_mount(void) { return -1; }
int sys_umount(void) { return -1; }
int sys_ptrace(void) { return -1; }
int sys_stty(void) { return -1; }
int sys_gtty(void) { return -1; }
int sys_ftime(void) { return -1; }
int sys_prof(void) { return -1; }
int sys_acct(void) { return -1; }
int sys_phys(void) { return -1; }
int sys_lock(void) { return -1; }
int sys_mpx(void) { return -1; }
int sys_ulimit(void) { return -1; }
int sys_ustat(void) { return -1; }
int sys_ioctl(void) { return -1; }

/* Hard link: create a new directory entry for an existing inode.
   Directories cannot be linked (POSIX).  Same device implied. */
int sys_link(const char *oldname, const char *newname)
{
    struct m_inode *oldinode, *dir;
    char dirpath[64], name[15];
    unsigned short ino;
    int namelen;

    oldinode = namei(oldname);
    if (!oldinode)
        return -1;
    if (oldinode->i_mode & S_IFDIR) {
        iput(oldinode);
        return -1;
    }
    if (split_path(newname, dirpath, name) < 0) {
        iput(oldinode);
        return -1;
    }
    dir = namei(dirpath);
    if (!dir) {
        iput(oldinode);
        return -1;
    }
    if (!(dir->i_mode & S_IFDIR)) {
        iput(oldinode);
        iput(dir);
        return -1;
    }
    if (!permission(dir, MAY_WRITE)) {      /* linking modifies the dir */
        iput(oldinode);
        iput(dir);
        return -1;
    }
    namelen = (int)strlen(name);
    if (dir_lookup(dir, name, namelen, &ino) == 0) {   /* target exists */
        iput(oldinode);
        iput(dir);
        return -1;
    }
    if (dir_add_entry(dir, name, namelen,
                      (unsigned short)oldinode->i_num) < 0) {
        iput(oldinode);
        iput(dir);
        return -1;
    }
    oldinode->i_nlinks++;
    oldinode->i_dirt = 1;
    iput(oldinode);
    iput(dir);
    return 0;
}

/* Rename (Linux 0.01 left this -ENOSYS; we implement it).  Same-device
   only: move the entry from the old parent dir to the new one. */
int sys_rename(const char *oldname, const char *newname)
{
    struct m_inode *oldinode, *dir_old, *dir_new;
    char olddir[64], oldbase[15], newdir[64], newbase[15];
    unsigned short ino;
    int n1, n2;

    if (split_path(oldname, olddir, oldbase) < 0)
        return -1;
    if (split_path(newname, newdir, newbase) < 0)
        return -1;
    n1 = (int)strlen(oldbase);
    n2 = (int)strlen(newbase);

    dir_old = namei(olddir);
    if (!dir_old)
        return -1;
    /* Renaming writes both directories: it removes an entry from one and
       (possibly) adds one to the other. */
    if (!permission(dir_old, MAY_WRITE)) {
        iput(dir_old);
        return -1;
    }
    if (dir_lookup(dir_old, oldbase, n1, &ino) < 0) {
        iput(dir_old);
        return -1;
    }
    oldinode = iget(dir_old->i_dev, ino);
    if (!oldinode) {
        iput(dir_old);
        return -1;
    }

    dir_new = namei(newdir);
    if (!dir_new) {
        iput(dir_old);
        iput(oldinode);
        return -1;
    }
    if (dir_new != dir_old && dir_old->i_dev != dir_new->i_dev) {
        iput(oldinode);
        iput(dir_old);
        iput(dir_new);
        return -1;
    }
    if (dir_new != dir_old && !permission(dir_new, MAY_WRITE)) {
        iput(oldinode);
        iput(dir_old);
        iput(dir_new);
        return -1;
    }

    /* target must not exist (unless it is the same file) */
    if (dir_lookup(dir_new, newbase, n2, &ino) == 0 && ino != oldinode->i_num) {
        iput(oldinode);
        iput(dir_old);
        iput(dir_new);
        return -1;
    }

    if (dir_new != dir_old) {
        /* cross-directory: add new entry, then remove the old one */
        if (dir_add_entry(dir_new, newbase, n2,
                          (unsigned short)oldinode->i_num) < 0 ||
            dir_remove_entry(dir_old, oldbase, n1) < 0) {
            iput(oldinode);
            iput(dir_old);
            iput(dir_new);
            return -1;
        }
        dir_old->i_nlinks--;               /* lost a child */
        dir_new->i_nlinks++;
        dir_old->i_dirt = 1;
        dir_new->i_dirt = 1;
    } else {
        /* same directory: swap the name in place */
        if (dir_remove_entry(dir_old, oldbase, n1) < 0 ||
            dir_add_entry(dir_old, newbase, n2,
                          (unsigned short)oldinode->i_num) < 0) {
            iput(oldinode);
            iput(dir_old);
            iput(dir_new);
            return -1;
        }
    }

    iput(oldinode);
    iput(dir_old);
    iput(dir_new);
    return 0;
}
/* --- fcntl (Linux 0.01 fs/fcntl.c) and brk (kernel/sys.c) --- */

static int dupfd(unsigned int fd, unsigned int arg)
{
    if (fd >= NR_OPEN || !current->filp[fd])
        return -1;
    if (arg >= NR_OPEN)
        return -1;
    while (arg < NR_OPEN && current->filp[arg])
        arg++;
    if (arg >= NR_OPEN)
        return -1;
    (current->filp[arg] = current->filp[fd])->f_count++;
    return arg;
}

int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
    struct file *filp;

    if (fd >= NR_OPEN || !(filp = current->filp[fd]))
        return -1;
    switch (cmd) {
    case F_DUPFD:
        return dupfd(fd, (unsigned int)arg);
    case F_GETFD:
        return 0;                     /* no close_on_exec tracking */
    case F_SETFD:
        return 0;
    case F_GETFL:
        return filp->f_flags;
    case F_SETFL:
        filp->f_flags = (unsigned short)arg;
        return 0;
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
    default:
        return -1;
    }
}

int sys_brk(unsigned long end_data_seg)
{
    if (end_data_seg >= current->end_code &&
        end_data_seg < current->start_stack - 16384)
        current->brk = end_data_seg;
    return (int)current->brk;
}

/* ------------------------------------------------------------------
 * execve: load a 32-bit ELF from the MINIX filesystem and run it in
 * Ring3.  LOAD segments are copied to their vaddr (the teaching kernel
 * uses identity paging with U/S=1, so user code lives at its link
 * address); BSS (memsz - filesz) is zeroed; argv is placed on the
 * user stack at USER_STACK_TOP; then iret jumps to the ELF entry point.
 * ------------------------------------------------------------------ */
static unsigned long rd32(const unsigned char *b)
{
    return (unsigned long)b[0] | ((unsigned long)b[1] << 8) |
           ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

static unsigned short rd16(const unsigned char *b)
{
    return (unsigned short)b[0] | ((unsigned short)b[1] << 8);
}

#define ELF_MAGIC 0x464C457F   /* \x7fELF */
#define PT_LOAD 1
/* The linker folds the ELF headers into the first LOAD segment, whose
 * vaddr ends up one page below the program's link address. */
#define ELF_HDR_MAX_SKIP PAGE_SIZE

/* Every fixed user address below comes from include/memlayout.h; see
 * mm/memcheck.c for why they may not be sprinkled around any more. */

int sys_execve(const char *filename, char **argv, char **envp)
{
    unsigned char eh[64], ph[32];
    unsigned long entry, phoff, phnum, phentsize;
    unsigned long max_end = 0;         /* end of the program image */
    unsigned long new_pgdir = 0, old_pgdir;
    unsigned long tail_pa;
    char argv_buf[16][64];
    unsigned long argv_ptr[16];
    int fd, argc = 0, i;

    (void)envp;

    fd = sys_open(filename, 0, 0);
    if (fd < 0) {
        return -1;
    }

    /* Executing a file needs the x bit (the open above already enforced
       the r bit; POSIX would let --x through, this kernel does not —
       see docs/LIMITATIONS.md). */
    if (current->filp[fd] && !permission(current->filp[fd]->f_inode, MAY_EXEC)) {
        sys_close(fd);
        return -1;
    }

    {
        long rr = sys_read(fd, (char *)eh, 52);
        if (rr != 52) {
            goto fail;
        }
    }
    if (rd32(eh) != ELF_MAGIC || rd16(eh + 18) != 3) {  /* EM_386 */
        goto fail;
    }

    entry = rd32(eh + 24);
    phoff = rd32(eh + 28);
    phentsize = rd16(eh + 42);
    phnum = rd16(eh + 44);

    /* The entry point must live in the user program image; anything
       else would iret Ring3 straight into kernel or heap memory.  The
       per-segment "is this address inside the image" check has to wait
       until max_end is known (see below). */
    if (entry < USER_PROG_START || entry >= USER_PROG_END) {
        printk("execve: entry 0x%lx outside user program image "
               "[0x%lx,0x%lx)\n", entry,
               (unsigned long)USER_PROG_START, (unsigned long)USER_PROG_END);
        goto fail;
    }

    /* M3: the new image goes into an address space of its own.  This is
       what makes a Ring3 shell possible — the old image (the parent
       shell's code, or this very image) is not touched at all: the new
       pages are fresh frames, reached by the kernel through the
       identity map while they are being filled. */
    new_pgdir = alloc_user_pgdir();
    if (!new_pgdir) {
        printk("execve: cannot allocate an address space\n");
        goto fail;
    }

    for (i = 0; i < phnum && i < 16; i++) {
        unsigned long p_type, p_offset, p_vaddr, p_filesz, p_memsz;

        if (sys_lseek(fd, (long)(phoff + i * phentsize), 0) < 0)
            goto fail_as;
        if (sys_read(fd, (char *)ph, 32) != 32)
            goto fail_as;

        p_type = rd32(ph);
        if (p_type != PT_LOAD)
            continue;
        p_offset = rd32(ph + 4);
        p_vaddr = rd32(ph + 8);
        p_filesz = rd32(ph + 16);
        p_memsz = rd32(ph + 20);

        /* Nothing to load (ld emits a zero-length PT_LOAD for the header
           page); it would otherwise trip the bounds check below. */
        if (p_memsz == 0)
            continue;

        /* ld maps the ELF headers as the first LOAD segment, one page
           below the link address.  With the user window starting at
           USER_PROG_START that page is simply outside the address space,
           so such a segment is dropped entirely; a segment that merely
           starts below the image base is trimmed to it.  Either way
           nothing is ever mapped outside [USER_PROG_START,
           USER_PROG_END). */
        if (p_vaddr + p_memsz <= USER_PROG_START)
            continue;
        if (p_vaddr < USER_PROG_START) {
            unsigned long skip = USER_PROG_START - p_vaddr;
            if (skip > ELF_HDR_MAX_SKIP) {
                printk("execve: LOAD segment starts at 0x%lx, more than one "
                       "page below the user program image\n", p_vaddr);
                goto fail_as;
            }
            if (skip > p_filesz)
                skip = p_filesz;
            p_offset += skip;
            p_vaddr += skip;
            p_filesz -= skip;
            p_memsz = (p_memsz > skip) ? (p_memsz - skip) : 0;
        }
        if (p_vaddr + p_filesz > USER_PROG_END ||
            p_vaddr + p_memsz > USER_PROG_END ||
            p_vaddr < USER_PROG_START) {
            printk("execve: LOAD segment [0x%lx,+0x%lx) outside user "
                   "program image [0x%lx,0x%lx)\n",
                   p_vaddr, p_memsz,
                   (unsigned long)USER_PROG_START, (unsigned long)USER_PROG_END);
            goto fail_as;
        }

        /* One frame per page the segment covers: the file's bytes are
           read into it, the rest of the page stays zero (that is the
           BSS).  Pages the file does not cover at all — a segment whose
           memsz runs past filesz over one or more whole pages — are
           mapped now but left zero without being read; a process that
           wants them demands them in on first touch (do_no_page). */
        {
            unsigned long va, seg_end = p_vaddr + p_memsz;
            unsigned long file_end = p_vaddr + p_filesz;

            for (va = p_vaddr & ~(PAGE_SIZE - 1); va < seg_end;
                 va += PAGE_SIZE) {
                unsigned long pa, copy_from, copy_len, dst_off;
                unsigned long page_lo = va;
                unsigned long page_hi = va + PAGE_SIZE;

                if (page_hi <= p_vaddr || page_lo >= file_end)
                    continue;                  /* pure BSS page: demand paged */

                pa = alloc_user_page(new_pgdir, va);
                if (!pa) {
                    printk("execve: out of memory loading the image\n");
                    goto fail_as;
                }

                copy_from = (page_lo > p_vaddr) ? page_lo : p_vaddr;
                copy_len = (page_hi < file_end) ? (page_hi - copy_from)
                                                : (file_end - copy_from);
                dst_off = copy_from & (PAGE_SIZE - 1);

                if (copy_len) {
                    if (sys_lseek(fd, (long)(p_offset + (copy_from - p_vaddr)),
                                  0) < 0)
                        goto fail_as;
                    if (sys_read(fd, (char *)(pa + dst_off), copy_len) !=
                        (long)copy_len)
                        goto fail_as;
                }
            }
        }

        if (p_vaddr + p_memsz > max_end)
            max_end = p_vaddr + p_memsz;
    }
    sys_close(fd);
    fd = -1;

    /* --- make sure the whole image fits the user program region --- */
    if (max_end > USER_PROG_END ||
        (max_end > 0 && max_end <= USER_PROG_START)) {
        printk("execve: image ends at 0x%lx, past the user program region "
               "[0x%lx,0x%lx)\n", max_end,
               (unsigned long)USER_PROG_START, (unsigned long)USER_PROG_END);
        goto fail_as;
    }

    /* --- collect argv (user pointers, read through the OLD address
       space, which is still the current one) --- */
    for (i = 0; i < 15; i++) {
        unsigned long p = get_fs_long((unsigned long *)argv + i);
        if (!p)
            break;
        argv_ptr[i] = p;
        argc++;
    }

    /* --- copy each argument string into kernel buffers --- */
    for (i = 0; i < argc; i++) {
        unsigned long p = argv_ptr[i];
        int k = 0;
        while (k < 63) {
            char c = get_fs_byte((char *)(p + k));
            argv_buf[i][k] = c;
            if (!c)
                break;
            k++;
        }
        argv_buf[i][k] = '\0';
    }

    /* --- build the user stack tail page ABOVE USER_STACK_TOP (the user
       stack grows DOWN from it, so nothing here gets clobbered by the
       program's own call frames):
         +0x004 argc, +0x008 argv-array pointer, +0x00C+ argv[]
         strings packed down from USER_ARGV_STR_TOP --- */
    {
        unsigned long tail_va = USER_STACK_TOP;
        char *sp;
        unsigned long *uargv;
        unsigned long frame_end;

        tail_pa = alloc_user_page(new_pgdir, tail_va);
        if (!tail_pa) {
            printk("execve: out of memory for the argv page\n");
            goto fail_as;
        }

        /* The page is a fresh frame reached by the kernel at its own
           physical address; user addresses inside it are just an
           offset away. */
#define TAIL_PTR(va) ((char *)(tail_pa + ((va) - USER_STACK_TOP)))

        sp = (char *)TAIL_PTR(USER_ARGV_STR_TOP - 1);
        uargv = (unsigned long *)TAIL_PTR(USER_ARGV_ADDR);
        /* Strings must not grow down into the argv[] array. */
        frame_end = USER_ARGV_ADDR + (unsigned long)(argc + 1) *
                    sizeof(unsigned long);

        for (i = argc - 1; i >= 0; i--) {
            int len = 0;
            while (argv_buf[i][len])
                len++;
            sp -= len + 1;
            if ((unsigned long)sp < (unsigned long)TAIL_PTR(frame_end)) {
                printk("execve: argv block overflows the stack page\n");
                goto fail_as;
            }
            memcpy(sp, argv_buf[i], len + 1);
            argv_ptr[i] = USER_STACK_TOP + (unsigned long)(sp -
                            (char *)TAIL_PTR(USER_STACK_TOP));
        }
        for (i = 0; i < argc; i++)
            uargv[i] = argv_ptr[i];
        uargv[argc] = 0;

        *(unsigned long *)TAIL_PTR(USER_ARGC_ADDR) = (unsigned long)argc;
        /* The pointer goes to its own slot, NOT to the array's address:
           USER_ARGV_ADDR is the array itself, and storing the pointer
           there would overwrite argv[0] - which is exactly what happened
           while the two were the same constant. */
        *(unsigned long *)TAIL_PTR(USER_ARGV_PTR_ADDR) =
            (unsigned long)USER_ARGV_ADDR;
#undef TAIL_PTR
    }

    /* --- switch to the new address space and iret into Ring3 --- */
    old_pgdir = current->pg_dir;
    current->pg_dir = new_pgdir;
    current->tss.cr3 = new_pgdir;
    current->start_code = USER_PROG_START;
    current->end_code = max_end;
    current->end_data = max_end;
    current->brk = USER_HEAP_START;
    current->start_stack = USER_STACK_TOP;

    /* The old image (or, for a freshly exec'd child, the fork()ed copy
       of the parent's) can go now: nothing below uses it any more, and
       the tail/argv writes above went into the NEW space. */
    if (old_pgdir && old_pgdir != kernel_pg_dir)
        free_user_space(old_pgdir);

    write_cr3(new_pgdir);

    {
        unsigned long stack = (unsigned long)USER_STACK_TOP;
        unsigned long ss_sel = 0x23;
        unsigned long cs_sel = 0x1b;
        __asm__ volatile(
            "pushl %2\n\t"          /* ss = USER_DS */
            "pushl %0\n\t"          /* user esp */
            "pushfl\n\t"
            "pushl %3\n\t"          /* cs = USER_CS */
            "pushl %1\n\t"          /* eip = ELF entry */
            "iret\n\t"
            :
            : "r"(stack), "r"(entry), "r"(ss_sel), "r"(cs_sel)
            : "memory");
    }

    /* never reached */
    return 0;

fail_as:
    if (fd >= 0)
        sys_close(fd);
    if (new_pgdir)
        free_user_space(new_pgdir);
    return -1;

fail:
    sys_close(fd);
    return -1;
}
