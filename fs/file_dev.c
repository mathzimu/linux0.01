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
    return 0;
}