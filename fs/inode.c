#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/system.h>

extern struct super_block super_block[NR_SUPER];

static void read_inode(struct m_inode *inode)
{
    struct super_block *sb;
    struct buffer_head *bh;
    struct d_inode *di;
    int block, offset, i;

    if (!inode || !inode->i_dev) return;

    sb = get_super(inode->i_dev);
    if (!sb) return;

    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
            (inode->i_num - 1) / (BLOCK_SIZE / sizeof(struct d_inode));

    bh = bread(inode->i_dev, block);
    if (!bh) {
        inode->i_dev = 0;
        inode->i_count = 0;
        return;
    }

    offset = (inode->i_num - 1) % (BLOCK_SIZE / sizeof(struct d_inode));
    di = (struct d_inode *)bh->b_data + offset;

    inode->i_mode = di->i_mode;
    inode->i_uid = di->i_uid;
    inode->i_size = di->i_size;
    inode->i_mtime = di->i_time;
    inode->i_gid = di->i_gid;
    inode->i_nlinks = di->i_nlinks;
    for (i = 0; i < 9; i++)
        inode->i_zone[i] = di->i_zone[i];

    brelse(bh);
}

static void write_inode(struct m_inode *inode)
{
    struct super_block *sb;
    struct buffer_head *bh;
    struct d_inode *di;
    int block, offset, i;

    if (!inode || !inode->i_dev || !inode->i_dirt)
        return;

    sb = get_super(inode->i_dev);
    if (!sb) return;

    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
            (inode->i_num - 1) / (BLOCK_SIZE / sizeof(struct d_inode));

    bh = bread(inode->i_dev, block);
    if (!bh) return;

    offset = (inode->i_num - 1) % (BLOCK_SIZE / sizeof(struct d_inode));
    di = (struct d_inode *)bh->b_data + offset;

    di->i_mode = inode->i_mode;
    di->i_uid = inode->i_uid;
    di->i_size = inode->i_size;
    di->i_time = inode->i_mtime;
    di->i_gid = inode->i_gid;
    di->i_nlinks = inode->i_nlinks;
    for (i = 0; i < 9; i++)
        di->i_zone[i] = inode->i_zone[i];

    bh->b_dirt = 1;
    ll_rw_block(WRITE, bh);
    brelse(bh);
    inode->i_dirt = 0;
}

void sync_inodes(int dev)
{
    int i;

    for (i = 0; i < NR_INODE; i++) {
        struct m_inode *inode = &inode_table[i];
        if (inode->i_dev == dev && inode->i_dirt)
            write_inode(inode);
    }
}

/* Release every data zone referenced by inode (7 direct + 1 single
   indirect); used when an inode's link count reaches zero. */
void truncate_inode(struct m_inode *inode)
{
    int i;

    if (!inode)
        return;

    for (i = 0; i < 7; i++) {
        if (inode->i_zone[i]) {
            free_block(inode->i_dev, inode->i_zone[i]);
            inode->i_zone[i] = 0;
        }
    }

    if (inode->i_zone[7]) {
        struct buffer_head *bh = bread(inode->i_dev, inode->i_zone[7]);
        if (bh) {
            unsigned short *ind = (unsigned short *)bh->b_data;
            for (i = 0; i < BLOCK_SIZE / 2; i++) {
                if (ind[i])
                    free_block(inode->i_dev, ind[i]);
            }
            brelse(bh);
        }
        free_block(inode->i_dev, inode->i_zone[7]);
        inode->i_zone[7] = 0;
    }

    inode->i_size = 0;
    inode->i_dirt = 1;
}

/* Wait until another task has finished loading this inode from disk.
 *
 * iget() exposes a freshly claimed slot (i_dev/i_num/i_count) *before*
 * read_inode() has filled it in, and read_inode() sleeps on the disk read.
 * A second task looking up the same inode therefore got it with a zeroed
 * body - which is how "the second concurrent exec of the same program
 * fails" showed up: namei resolved /bin to ino 7 and then refused to
 * traverse it because i_mode was still 0.  cli() closes the check/sleep
 * window exactly like wait_on_buffer(); the queue is the inode's own
 * i_wait, which iget() and get_empty_inode() reset to NULL on claim. */
static void wait_on_inode(struct m_inode *inode)
{
    cli();
    while (inode->i_lock)
        sleep_on(&inode->i_wait);
    sti();
}

struct m_inode *iget(int dev, int nr)
{
    struct m_inode *inode;
    int i;

    for (i = 0; i < NR_INODE; i++) {
        inode = &inode_table[i];
        if (inode->i_dev == dev && inode->i_num == nr) {
            wait_on_inode(inode);
            /* The slot may have been recycled while we slept: only take
               it if it is still the inode we asked for. */
            if (inode->i_dev != dev || inode->i_num != nr)
                continue;
            inode->i_count++;
            return inode;
        }
    }

    for (i = 0; i < NR_INODE; i++) {
        inode = &inode_table[i];
        if (!inode->i_count) {
            /* Reusing a slot whose inode is still dirty would silently
               discard unsynced changes (observed: a fresh mkdir lost its
               root-dir entry when iget() for the next inode took over
               the root's slot).  Flush it first. */
            if (inode->i_dirt)
                write_inode(inode);
            inode->i_count = 1;
            inode->i_dev = dev;
            inode->i_num = nr;
            inode->i_lock = 1;          /* "loading": see wait_on_inode */
            inode->i_dirt = 0;
            inode->i_wait = NULL;       /* wait queue must start empty */
            inode->i_pipe = 0;
            inode->i_mount = 0;
            inode->i_seek = 0;
            inode->i_update = 0;
            read_inode(inode);
            inode->i_lock = 0;
            wake_up(&inode->i_wait);

            /* read_inode() gives up (and clears i_dev/i_count) when the
               inode block cannot be read.  Returning that slot would hand
               the caller an inode with a zeroed body, which is the same
               damage the load race caused - so report failure instead. */
            if (inode->i_dev != dev)
                return NULL;
            return inode;
        }
    }

    printk("iget: inode table full (%d of %d slots in use), dev=%d nr=%d\n",
           iget_used(), NR_INODE, dev, nr);
    return NULL;
}

/* How many inode slots are currently held.  Read-only diagnostic used by
   the namei/open failure reports: "namei returned NULL" with a full table
   is a completely different bug from a missing directory entry. */
int iget_used(void)
{
    int i, n = 0;

    for (i = 0; i < NR_INODE; i++)
        if (inode_table[i].i_count)
            n++;
    return n;
}

/* Grab an inode-table slot without reading anything from disk
   (used for pipe inodes). */
struct m_inode *get_empty_inode(void)
{
    struct m_inode *inode;
    int i;

    for (i = 0; i < NR_INODE; i++) {
        inode = &inode_table[i];
        if (!inode->i_count) {
            inode->i_count = 1;
            inode->i_dev = 0;
            inode->i_num = 0;
            inode->i_dirt = 0;
            inode->i_lock = 0;
            inode->i_wait = NULL;       /* wait queue must start empty */
            inode->i_pipe = 0;
            inode->i_mount = 0;
            inode->i_seek = 0;
            inode->i_update = 0;
            inode->i_nlinks = 0;
            inode->i_size = 0;
            return inode;
        }
    }
    return NULL;
}

void iput(struct m_inode *inode)
{
    if (!inode) return;

    if (inode->i_pipe) {
        /* pipe inode: no disk representation; the last close frees
           the buffer page */
        if (--inode->i_count == 0) {
            free_page(inode->i_size);
            inode->i_pipe = 0;
            inode->i_size = 0;
        }
        return;
    }

    inode->i_count--;
    if (inode->i_count) return;
    if (inode->i_nlinks) return;

    free_inode(inode);
}
