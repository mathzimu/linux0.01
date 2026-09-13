#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/hdreg.h>
#include <asm/system.h>

static struct buffer_head *free_list = NULL;
static struct buffer_head *hash_table[NR_BUFFERS];
static char *buffer_mem;

/* Inspected by mm/memcheck.c: how many buffers the cache actually got,
 * and the [start, end) of the region it occupies.  A mismatch between
 * nr_buffers and NR_BUFFERS means the compile-time layout no longer
 * matches reality and mem_check() will refuse to boot. */
int nr_buffers = 0;
unsigned long buf_mem_start = 0;
unsigned long buffer_cache_end = 0;

/* The cache is placed at the top of usable RAM and grows downward:
 *   [ buf_mem_start .......... buffer_heads .......... buffer_end )
 * and it must end at or above BUFFER_CACHE_FLOOR, or it would share
 * pages with the user heap or the fork() child stack (Ring0 ignores the
 * PTE U/S bit, so the corruption would be completely silent).  If the
 * window cannot hold NR_BUFFERS this panics instead of quietly
 * overlapping user data. */
void buffer_init(long buffer_end)
{
    struct buffer_head *bh;
    char *data;
    char *data_start;
    int i;

    long total_bh_size = NR_BUFFERS * sizeof(struct buffer_head);
    long total_data_size = NR_BUFFERS * BLOCK_SIZE;

    bh = (struct buffer_head *)(buffer_end - total_bh_size);
    data_start = (char *)bh - total_data_size;
    buffer_mem = data_start;
    nr_buffers = NR_BUFFERS;

    /* Same invariant as mm/memcheck.c, checked here because this is the
       function that would be doing the clobbering. */
    if ((unsigned long)data_start < BUFFER_CACHE_FLOOR) {
        printk("buffer_init: cache [0x%lx,0x%lx) would grow below "
               "BUFFER_CACHE_FLOOR 0x%lx\n",
               (unsigned long)data_start, (unsigned long)buffer_end,
               (unsigned long)BUFFER_CACHE_FLOOR);
        panic("buffer_init: NR_BUFFERS does not fit the cache window");
    }
    buf_mem_start = (unsigned long)data_start;
    buffer_cache_end = (unsigned long)buffer_end;

    printk("buffer cache: %d buffers (%luKB) at [0x%lx, 0x%lx)\n",
           nr_buffers,
           ((unsigned long)total_data_size + (unsigned long)total_bh_size) / 1024,
           (unsigned long)data_start, (unsigned long)buffer_end);

    free_list = bh;
    data = data_start;
    for (i = 0; i < NR_BUFFERS; i++) {
        bh[i].b_data = data;
        bh[i].b_dev = 0;
        bh[i].b_blocknr = 0;
        bh[i].b_uptodate = 0;
        bh[i].b_dirt = 0;
        bh[i].b_count = 0;
        bh[i].b_lock = 0;
        bh[i].b_wait = NULL;
        data += BLOCK_SIZE;

        if (i > 0) {
            bh[i].b_prev_free = &bh[i - 1];
            bh[i - 1].b_next_free = &bh[i];
        }
    }
    bh[0].b_prev_free = &bh[NR_BUFFERS - 1];
    bh[NR_BUFFERS - 1].b_next_free = &bh[0];

    for (i = 0; i < NR_BUFFERS; i++)
        hash_table[i] = NULL;

    /* Protect buffer cache pages from the page allocator */
    {
        unsigned long cache_start = (unsigned long)data_start;
        unsigned long cache_end = (unsigned long)buffer_end;
        int first, last, j;
        first = MAP_NR(cache_start & ~(PAGE_SIZE - 1));
        last = MAP_NR(cache_end - 1);
        for (j = first; j <= last && j < max_map_nr; j++)
            mem_map[j] = USED;
    }
}

struct buffer_head *getblk(int dev, int block)
{
    struct buffer_head *bh;
    int i;

repeat:
    bh = NULL;
    i = (dev ^ block) & (NR_BUFFERS - 1);

    bh = hash_table[i];
    while (bh) {
        if (bh->b_dev == dev && bh->b_blocknr == block) {
            bh->b_count++;
            return bh;
        }
        bh = bh->b_next;
    }

    bh = free_list;
    while (bh) {
        if (!bh->b_count) {
            /* Reusing a dirty buffer would silently lose its old
               contents, so write it back before recycling. */
            if (bh->b_dirt) {
                bh->b_count = 1;         /* keep it off the recycle path */
                ll_rw_block(WRITE, bh);
                bh->b_count = 0;
            }
            /* Unlink from its OLD hash chain first: otherwise the old
               chain keeps a ghost pointer to this buffer and walking it
               later follows into the new chain, eventually cycling
               forever in getblk. */
            {
                int old_i = (bh->b_dev ^ bh->b_blocknr) & (NR_BUFFERS - 1);
                if (bh->b_prev)
                    bh->b_prev->b_next = bh->b_next;
                else if (hash_table[old_i] == bh)
                    hash_table[old_i] = bh->b_next;
                if (bh->b_next)
                    bh->b_next->b_prev = bh->b_prev;
            }
            bh->b_count = 1;
            bh->b_dev = dev;
            bh->b_blocknr = block;
            bh->b_uptodate = 0;
            bh->b_dirt = 0;

            bh->b_prev = NULL;
            bh->b_next = hash_table[i];
            if (hash_table[i])
                hash_table[i]->b_prev = bh;
            hash_table[i] = bh;

            return bh;
        }
        bh = bh->b_next_free;
        if (bh == free_list) break;
    }

    schedule();
    goto repeat;
}

struct buffer_head *bread(int dev, int block)
{
    struct buffer_head *bh;

    bh = getblk(dev, block);
    if (bh->b_uptodate) return bh;
    if (bh->b_lock) {
        sleep_on(&bh->b_wait);
        if (bh->b_uptodate) return bh;
    }

    ll_rw_block(READ, bh);

    wait_on_buffer(bh);
    if (bh->b_uptodate) return bh;

    brelse(bh);
    return NULL;
}

void brelse(struct buffer_head *buf)
{
    if (!buf) return;

    buf->b_count--;
    if (buf->b_count) return;

    if (buf == free_list) return;

    if (buf->b_prev_free && buf->b_next_free) {
        buf->b_prev_free->b_next_free = buf->b_next_free;
        buf->b_next_free->b_prev_free = buf->b_prev_free;
    }

    buf->b_prev_free = free_list;
    buf->b_next_free = free_list->b_next_free;
    free_list->b_next_free->b_prev_free = buf;
    free_list->b_next_free = buf;
    free_list = buf;
}

void ll_rw_block(int rw, struct buffer_head *bh)
{
    unsigned int lba;
    int nsects;
    int ret;

    if (rw != READ && rw != WRITE) return;
    if (!bh) return;

    bh->b_lock = 1;

    lba = bh->b_blocknr * (BLOCK_SIZE / 512);
    nsects = BLOCK_SIZE / 512;

    if (rw == READ) {
        ret = hd_read_sectors(lba, nsects, bh->b_data);
        if (ret == 0)
            bh->b_uptodate = 1;
    } else {
        ret = hd_write_sectors(lba, nsects, bh->b_data);
        if (ret == 0) {
            bh->b_dirt = 0;
            bh->b_uptodate = 1;
        }
    }

    bh->b_lock = 0;
}

/* Write back every dirty buffer (and, via sync_inodes, every dirty
   inode) belonging to dev.  The free_list is a circular list holding
   all NR_BUFFERS heads, so walking it once reaches everything.

   Returns the number of data blocks that were actually written, so the
   periodic write-back task can stay silent when there was nothing to do
   (and a regression test can tell "flushed" from "flushed nothing"). */
int sync_dev(int dev)
{
    int i;
    int written = 0;
    struct buffer_head *bh = free_list;

    for (i = 0; i < NR_BUFFERS; i++, bh = bh->b_next_free) {
        if (bh->b_dev == dev && bh->b_dirt) {
            bh->b_count++;
            ll_rw_block(WRITE, bh);
            bh->b_count--;
            written++;
        }
    }

    sync_inodes(dev);

    return written;
}

void wait_on_buffer(struct buffer_head *bh)
{
    while (bh->b_lock) {
        sleep_on(&bh->b_wait);
    }
}

void sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p) return;
    if (!current) return;

    tmp = *p;
    *p = current;
    current->state = TASK_UNINTERRUPTIBLE;
    schedule();

    if (tmp && (tmp->state == TASK_UNINTERRUPTIBLE ||
                tmp->state == TASK_INTERRUPTIBLE))
        tmp->state = TASK_RUNNING;
}

void wake_up(struct task_struct **p)
{
    if (p && *p) {
        (*p)->state = TASK_RUNNING;
    }
}
