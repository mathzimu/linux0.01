#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/segment.h>

int file_read(struct m_inode *inode, struct file *filp, char *buf, int count)
{
    int result = 0;
    int zone_offset;
    int holesize;
    unsigned long pos;

    pos = filp->f_pos;

    while (count > 0 && pos < inode->i_size) {
        zone_offset = pos / BLOCK_SIZE;
        holesize = 0;

        int block = 0;

        if (zone_offset < 7) {
            block = inode->i_zone[zone_offset];
        } else {
            struct buffer_head *bh;
            unsigned short indblk = inode->i_zone[7];
            if (!indblk) { block = 0; }
            else {
                bh = bread(inode->i_dev, indblk);
                if (!bh) break;
                block = ((unsigned short *)bh->b_data)[zone_offset - 7];
                brelse(bh);
            }
        }

        if (!block) {
            holesize = BLOCK_SIZE - (pos % BLOCK_SIZE);
            if (holesize > count) holesize = count;
            if (holesize > (int)(inode->i_size - pos)) holesize = inode->i_size - pos;
            {
                int k;
                for (k = 0; k < holesize; k++) put_fs_byte(0, buf + result + k);
            }
            pos += holesize;
            result += holesize;
            count -= holesize;
            continue;
        }

        struct buffer_head *bh = bread(inode->i_dev, block);
        if (!bh) break;

        int offset = pos % BLOCK_SIZE;
        int size = BLOCK_SIZE - offset;
        if (size > count) size = count;
        if (size > (int)(inode->i_size - pos)) size = inode->i_size - pos;

        {
            int k;
            char *src = bh->b_data + offset;
            for (k = 0; k < size; k++) {
                put_fs_byte(src[k], buf + result + k);
            }
        }
        pos += size;
        result += size;
        count -= size;

        brelse(bh);
    }

    return result;
}

int file_write(struct m_inode *inode, struct file *filp, const char *buf, int count)
{
    int result = 0;
    int zone_offset;
    unsigned long pos;

    if (!inode || !filp || !buf) return 0;

    pos = filp->f_pos;

    while (count > 0) {
        zone_offset = pos / BLOCK_SIZE;

        int block = 0;

        if (zone_offset < 7) {
            if (!inode->i_zone[zone_offset]) {
                inode->i_zone[zone_offset] = new_block(inode->i_dev);
                if (!inode->i_zone[zone_offset]) break;
                inode->i_dirt = 1;
            }
            block = inode->i_zone[zone_offset];
        } else {
            struct buffer_head *ibh;
            unsigned short indblk = inode->i_zone[7];
            if (!indblk) {
                indblk = new_block(inode->i_dev);
                if (!indblk) break;
                inode->i_zone[7] = indblk;
                inode->i_dirt = 1;
            }
            ibh = bread(inode->i_dev, indblk);
            if (!ibh) break;

            unsigned short *ind = (unsigned short *)ibh->b_data;
            if (!ind[zone_offset - 7]) {
                ind[zone_offset - 7] = new_block(inode->i_dev);
                if (!ind[zone_offset - 7]) {
                    brelse(ibh);
                    break;
                }
                ibh->b_dirt = 1;
            }
            block = ind[zone_offset - 7];
            brelse(ibh);
        }

        if (!block) break;

        struct buffer_head *bh = bread(inode->i_dev, block);
        if (!bh) break;

        int offset = pos % BLOCK_SIZE;
        int size = BLOCK_SIZE - offset;
        if (size > count) size = count;

        char *dst = bh->b_data + offset;
        for (int k = 0; k < size; k++)
            dst[k] = get_fs_byte(buf + result + k);

        bh->b_dirt = 1;
        brelse(bh);

        pos += size;
        result += size;
        count -= size;

        if (pos > inode->i_size) {
            inode->i_size = pos;
            inode->i_dirt = 1;
        }
    }

    return result;
}