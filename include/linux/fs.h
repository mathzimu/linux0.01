#ifndef _FS_H
#define _FS_H

#include <memlayout.h>

#define SUPER_MAGIC 0x137F
#define BLOCK_SIZE 1024
#define NR_FILE 64
#define NR_INODE 64
#define NR_SUPER 8

/* Buffer cache size.  The cache occupies the window between
 * BUFFER_CACHE_FLOOR and BUFFER_CACHE_TOP (include/linux/memmap.h) - i.e.
 * below the user stack and above the heap and child-stack region - so it
 * can never share a page with user data.
 *
 * The count is derived from the window rather than hand-picked, and it
 * must be a power of two because getblk()/brelse() index hash_table[]
 * with `& (NR_BUFFERS - 1)`.  The window is 0xA0000 bytes, which holds
 * exactly NR_BUFFERS_MAX = 256 buffers (256KB of data plus 8KB of heads).
 * scripts/check-layout.py charges a conservative 64 bytes per
 * buffer_head and fails the change if the count no longer fits. */
#define NR_BUFFERS_MAX      256
#define NR_BUFFERS_WINDOW   (BUFFER_CACHE_TOP - BUFFER_CACHE_FLOOR)
#define NR_BUFFERS_FOR_WINDOW (NR_BUFFERS_WINDOW / (BLOCK_SIZE + 64))
#define NR_BUFFERS \
    ((NR_BUFFERS_FOR_WINDOW >= NR_BUFFERS_MAX) ? NR_BUFFERS_MAX : 64)

#define READ 0
#define WRITE 1
#define READA 2

/* This header is now pulled in very early (include/linux/memmap.h needs
 * BLOCK_SIZE and struct buffer_head), so the types it only points at
 * have to be forward declared here instead of relying on the include
 * order. */
struct task_struct;
struct file;

/* minix v1 directory entry (16 bytes: inode + 14-char name) */
struct minix_dir_entry {
    unsigned short inode;
    char name[14];
};

/* mode bits used by mknod/mkdir */
#define S_IFMT  00170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

/* Access masks for permission() — Linux 0.01's fs/namei.c uses the same
 * numbering (MAY_EXEC 1, MAY_WRITE 2, MAY_READ 4). */
#define MAY_EXEC  1
#define MAY_WRITE 2
#define MAY_READ  4

struct buffer_head {
    char *b_data;
    unsigned long b_blocknr;
    unsigned short b_dev;
    unsigned char b_uptodate;
    unsigned char b_dirt;
    unsigned char b_count;
    unsigned char b_lock;
    struct task_struct *b_wait;
    struct buffer_head *b_prev;
    struct buffer_head *b_next;
    struct buffer_head *b_prev_free;
    struct buffer_head *b_next_free;
};

struct d_inode {
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_time;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
};

struct m_inode {
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_mtime;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
    struct task_struct *i_wait;
    unsigned short i_dev;
    unsigned long i_num;
    unsigned char i_count;
    unsigned char i_lock;
    unsigned char i_dirt;
    unsigned char i_pipe;
    unsigned char i_mount;
    unsigned char i_seek;
    unsigned char i_update;
};

/* Open file description.  It lives here (not in sched.h) because fs.h is
 * now included very early by include/linux/memmap.h, and file_table[]
 * needs the complete type. */
struct file {
    unsigned short f_mode;
    unsigned short f_flags;
    unsigned short f_count;
    struct m_inode *f_inode;
    unsigned long f_pos;
};

struct super_block {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
    struct m_inode *s_imount;
    unsigned short s_dev;
    unsigned short s_time;
};

void buffer_init(long buffer_end);
struct buffer_head *getblk(int dev, int block);
struct buffer_head *bread(int dev, int block);
void brelse(struct buffer_head *buf);
int sys_open(const char *filename, int flag, int mode);
int sys_close(unsigned int fd);
long sys_read(unsigned int fd, char *buf, unsigned long count);
long sys_write(unsigned int fd, const char *buf, unsigned long count);

struct m_inode *iget(int dev, int nr);
struct m_inode *get_empty_inode(void);
int read_pipe(struct m_inode *inode, char *buf, int count);
int write_pipe(struct m_inode *inode, char *buf, int count);
int sys_pipe(unsigned long *fildes);
void iput(struct m_inode *inode);
struct m_inode *namei(const char *pathname);

/* Permission checking (fs/namei.c).  The kernel used to store i_mode,
 * i_uid and i_gid without ever consulting them: every task is uid 0 in
 * the default setup, so "who may do what" simply never came up.  These
 * are the Linux 0.01 rules, including root's blanket override. */
int permission(struct m_inode *inode, int mask);
int suser(void);
void sync_inodes(int dev);
int dir_lookup(struct m_inode *dir, const char *name, int namelen,
               unsigned short *ino_out);
int dir_add_entry(struct m_inode *dir, const char *name, int namelen,
                  unsigned short ino);
int dir_remove_entry(struct m_inode *dir, const char *name, int namelen);
int dir_is_empty(struct m_inode *dir);
void free_dir_zones(struct m_inode *inode);
int split_path(const char *path, char *dirpath, char *name);
void truncate_inode(struct m_inode *inode);

int file_read(struct m_inode *inode, struct file *filp, char *buf, int count);
int file_write(struct m_inode *inode, struct file *filp, const char *buf, int count);
void free_inode(struct m_inode *inode);
struct m_inode *new_inode(int dev);
int new_block(int dev);
void free_block(int dev, int block);
struct super_block *get_super(int dev);
void sleep_on(struct task_struct **p);
void wake_up(struct task_struct **p);
void ll_rw_block(int rw, struct buffer_head *bh);
void wait_on_buffer(struct buffer_head *bh);
int sync_dev(int dev);

extern struct file file_table[NR_FILE];
extern struct m_inode inode_table[NR_INODE];
extern struct super_block super_block[NR_SUPER];

#endif
