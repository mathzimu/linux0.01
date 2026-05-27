#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/hdreg.h>

struct super_block super_block[NR_SUPER];
struct m_inode inode_table[NR_INODE];
struct file file_table[NR_FILE];

struct super_block *get_super(int dev)
{
    int i;
    for (i = 0; i < NR_SUPER; i++) {
        if (super_block[i].s_dev == dev)
            return &super_block[i];
    }
    return NULL;
}

static struct minix_superblock {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
} __attribute__((packed));

int sys_setup(void)
{
    struct buffer_head *bh;
    struct minix_superblock *sb;
    int dev = 0x301;

    bh = bread(dev, 1);
    if (!bh) return -1;

    sb = (struct minix_superblock *)bh->b_data;

    if (sb->s_magic != SUPER_MAGIC) {
        printk("MINIX: bad magic 0x%x (expected 0x%x)\n",
               sb->s_magic, SUPER_MAGIC);
        brelse(bh);
        return -1;
    }

    super_block[0].s_dev = dev;
    super_block[0].s_ninodes = sb->s_ninodes;
    super_block[0].s_nzones = sb->s_nzones;
    super_block[0].s_imap_blocks = sb->s_imap_blocks;
    super_block[0].s_zmap_blocks = sb->s_zmap_blocks;
    super_block[0].s_firstdatazone = sb->s_firstdatazone;
    super_block[0].s_log_zone_size = sb->s_log_zone_size;
    super_block[0].s_max_size = sb->s_max_size;
    super_block[0].s_magic = sb->s_magic;

    brelse(bh);
    printk("MINIX: superblock loaded, magic=0x%x\n", sb->s_magic);
    return 0;
}
