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

/* ------------------------------------------------------------------
 * execve: load a 32-bit ELF from the MINIX filesystem and run it in
 * Ring3.  LOAD segments are copied to their vaddr (the teaching kernel
 * uses identity paging with U/S=1, so user code lives at its link
 * address); BSS (memsz - filesz) is zeroed; argv is placed on a user
 * stack at 0x3FF000; then iret jumps to the ELF entry point.
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

#define USER_STACK_TOP 0x3FF000UL
#define ELF_MAGIC 0x464C457F   /* \x7fELF */
#define PT_LOAD 1

int sys_execve(const char *filename, char **argv, char **envp)
{
    unsigned char eh[64], ph[32];
    unsigned long entry, phoff, phnum, phentsize;
    char argv_buf[16][64];
    unsigned long argv_ptr[16];
    int fd, argc = 0, i;

    (void)envp;

    fd = sys_open(filename, 0);
    if (fd < 0) {
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

    for (i = 0; i < phnum && i < 16; i++) {
        unsigned long p_type, p_offset, p_vaddr, p_filesz, p_memsz;

        if (sys_lseek(fd, (long)(phoff + i * phentsize), 0) < 0)
            goto fail;
        if (sys_read(fd, (char *)ph, 32) != 32)
            goto fail;

        p_type = rd32(ph);
        if (p_type != PT_LOAD)
            continue;
        p_offset = rd32(ph + 4);
        p_vaddr = rd32(ph + 8);
        p_filesz = rd32(ph + 16);
        p_memsz = rd32(ph + 20);

        if (p_filesz) {
            if (sys_lseek(fd, (long)p_offset, 0) < 0)
                goto fail;
            if (sys_read(fd, (char *)p_vaddr, p_filesz) != (long)p_filesz)
                goto fail;
        }
        if (p_memsz > p_filesz)
            memset((void *)(p_vaddr + p_filesz), 0, p_memsz - p_filesz);
    }
    sys_close(fd);

    /* --- collect argv (user pointers) --- */
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

    /* --- build the user stack area ABOVE 0x3FF000 (the user stack
       grows DOWN from 0x3FF000, so nothing here gets clobbered by the
       program's own call frames):
         0x3FF004 argc, 0x3FF008 argv-array pointer, 0x3FF00C+ argv[]
         strings packed down from 0x400000 --- */
    {
        char *sp = (char *)(0x400000 - 1);
        unsigned long *uargv = (unsigned long *)(USER_STACK_TOP + 0xC);

        for (i = argc - 1; i >= 0; i--) {
            int len = 0;
            while (argv_buf[i][len])
                len++;
            sp -= len + 1;
            memcpy(sp, argv_buf[i], len + 1);
            argv_ptr[i] = (unsigned long)sp;
        }
        for (i = 0; i < argc; i++)
            uargv[i] = argv_ptr[i];
        uargv[argc] = 0;

        *(unsigned long *)(USER_STACK_TOP + 4) = (unsigned long)argc;
        *(unsigned long *)(USER_STACK_TOP + 8) = (unsigned long)uargv;

        /* --- iret into Ring3 at the ELF entry point --- */
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
    }

    /* never reached */
    return 0;

fail:
    sys_close(fd);
    return -1;
}
