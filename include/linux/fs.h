#ifndef _FS_H
#define _FS_H

#define SUPER_MAGIC 0x137F
#define BLOCK_SIZE 1024
#define NR_BUFFERS 512
#define NR_FILE 64
#define NR_INODE 64
#define NR_SUPER 8

#define READ 0
#define WRITE 1
#define READA 2

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
int sys_open(const char *filename, int flag);
int sys_close(unsigned int fd);
int sys_read(unsigned int fd, char *buf, unsigned long count);
int sys_write(unsigned int fd, const char *buf, unsigned long count);

struct m_inode *iget(int dev, int nr);
void iput(struct m_inode *inode);
struct m_inode *namei(const char *pathname);

int file_read(struct m_inode *inode, struct file *filp, char *buf, int count);
int file_write(struct m_inode *inode, struct file *filp, const char *buf, int count);
void free_inode(struct m_inode *inode);
struct super_block *get_super(int dev);
void sleep_on(struct task_struct **p);
void wake_up(struct task_struct **p);
void ll_rw_block(int rw, struct buffer_head *bh);
void wait_on_buffer(struct buffer_head *bh);

extern struct file file_table[NR_FILE];
extern struct m_inode inode_table[NR_INODE];
extern struct super_block super_block[NR_SUPER];

#endif
