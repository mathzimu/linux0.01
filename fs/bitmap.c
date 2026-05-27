#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>

void free_block(int dev, int block)
{
    struct super_block *sb;
    struct buffer_head *bh;
    unsigned int bit;

    sb = get_super(dev);
    if (!sb) return;

    if (block < sb->s_firstdatazone || block >= sb->s_nzones)
        return;

    bit = block - sb->s_firstdatazone + 1;
    bh = bread(dev, 2 + sb->s_imap_blocks + (bit / (BLOCK_SIZE * 8)));
    if (!bh) return;

    bit &= (BLOCK_SIZE * 8) - 1;
    if (((char *)bh->b_data)[bit / 8] & (1 << (bit % 8))) {
        ((char *)bh->b_data)[bit / 8] &= ~(1 << (bit % 8));
        bh->b_dirt = 1;
    }

    brelse(bh);
}

int new_block(int dev)
{
    struct buffer_head *bh;
    struct super_block *sb;
    int i, j, bit;

    sb = get_super(dev);
    if (!sb) return 0;

    for (i = 0; i < sb->s_zmap_blocks; i++) {
        bh = bread(dev, 2 + sb->s_imap_blocks + i);
        if (!bh) continue;

        for (j = 0; j < BLOCK_SIZE * 8; j++) {
            if (!(((char *)bh->b_data)[j / 8] & (1 << (j % 8)))) {
                ((char *)bh->b_data)[j / 8] |= (1 << (j % 8));
                bh->b_dirt = 1;
                brelse(bh);
                return sb->s_firstdatazone + i * (BLOCK_SIZE * 8) + j;
            }
        }
        brelse(bh);
    }

    return 0;
}

void free_inode(struct m_inode *inode)
{
    if (!inode) return;
    if (!inode->i_dev) return;

    struct super_block *sb = get_super(inode->i_dev);
    if (!sb) return;

    int block = 2 + (inode->i_num - 1) / (BLOCK_SIZE * 8 / sizeof(short));
    int bit = (inode->i_num - 1) & ((BLOCK_SIZE * 8) - 1);

    struct buffer_head *bh = bread(inode->i_dev, block);
    if (!bh) return;

    ((char *)bh->b_data)[bit / 8] &= ~(1 << (bit % 8));
    bh->b_dirt = 1;
    brelse(bh);
}

struct m_inode *new_inode(int dev)
{
    return NULL;
}
