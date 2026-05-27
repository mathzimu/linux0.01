#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/system.h>

static struct buffer_head *free_list = NULL;
static struct buffer_head *hash_table[NR_BUFFERS];
static char *buffer_mem;
static int nr_buffers = 0;

void buffer_init(long buffer_end)
{
    struct buffer_head *bh;
    char *data;
    int i;

    data = (char *)buffer_end;
    bh = (struct buffer_head *)&data[-NR_BUFFERS * sizeof(struct buffer_head)];
    data -= NR_BUFFERS * sizeof(struct buffer_head);
    buffer_mem = data;
    nr_buffers = NR_BUFFERS;

    free_list = bh;
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

    if (tmp)
        tmp->state = TASK_RUNNING;
}

void wake_up(struct task_struct **p)
{
    if (p && *p) {
        (*p)->state = TASK_RUNNING;
    }
}
