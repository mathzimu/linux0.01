#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/segment.h>

static int file_read_zone(struct m_inode *inode, unsigned long *pos,
                          char *buf, int count, int zone_nr)
{
    struct buffer_head *bh;
    int block, offset, size, result = 0;

    while (count > 0 && *pos < inode->i_size) {
        if (zone_nr < 7) {
            block = inode->i_zone[zone_nr];
        } else {
            return 0;
        }

        if (!block) return result;
        bh = bread(inode->i_dev, block);
        if (!bh) return result;

        offset = *pos % BLOCK_SIZE;
        size = BLOCK_SIZE - offset;
        if (size > count) size = count;
        if (size > inode->i_size - *pos) size = inode->i_size - *pos;

        memcpy(buf + result, bh->b_data + offset, size);
        *pos += size;
        result += size;
        count -= size;

        brelse(bh);
    }

    return result;
}

int file_read(struct m_inode *inode, struct file *filp, char *buf, int count)
{
    int result = 0;
    int zone, zone_offset;
    unsigned long pos;

    pos = filp->f_pos;

    while (count > 0 && pos < inode->i_size) {
        zone = pos / (BLOCK_SIZE);
        zone_offset = zone;

        int block = 0;

        if (zone_offset < 7) {
            block = inode->i_zone[zone_offset];
            if (!block) break;
        } else if (zone_offset < 7 + BLOCK_SIZE / 2) {
            struct buffer_head *bh;
            bh = bread(inode->i_dev, inode->i_zone[7]);
            if (!bh) break;
            block = ((unsigned short *)bh->b_data)[zone_offset - 7];
            brelse(bh);
            if (!block) break;
        } else {
            break;
        }

        struct buffer_head *bh = bread(inode->i_dev, block);
        if (!bh) break;

        int offset = pos % BLOCK_SIZE;
        int size = BLOCK_SIZE - offset;
        if (size > count) size = count;
        if (size > inode->i_size - pos) size = inode->i_size - pos;

        memcpy(buf + result, bh->b_data + offset, size);
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
