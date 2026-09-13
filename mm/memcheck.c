#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memmap.h>

/* Set by fs/buffer.c: how many buffers the cache actually got, the first
 * byte of the cache data area, and the top of the cache region. */
extern int nr_buffers;
extern unsigned long buf_mem_start;
extern unsigned long buffer_cache_end;

/* ====================================================================
 * mem_check() — the memory-map watchdog.
 *
 * The 0.01-style teaching kernel identity-maps 0..4MB, so "physical",
 * "kernel virtual" and "user virtual" are all the same numbers, and
 * several subsystems carve that single address space up by hand:
 * the page allocator, the kernel bump heap, the buffer cache and the
 * three fixed user regions.  Historically they were kept apart by
 * convention only, and the convention broke: NR_BUFFERS = 512 put the
 * buffer cache at ~0x370000, squarely on top of the user heap
 * [USER_HEAP_START, USER_HEAP_END), so a user program could hand its own
 * malloc'd bytes to the FS cache and have them written to disk — or have
 * the cache overwritten under it.  Ring0 ignores the PTE U/S bit, so
 * nothing faulted and nothing was logged.
 *
 * Every check below is a fact about addresses, never about the current
 * task, so this is safe to call before the scheduler exists.
 * ==================================================================== */

static int ranges_overlap(unsigned long a_start, unsigned long a_end,
                          unsigned long b_start, unsigned long b_end)
{
    /* half-open [start, end) */
    return (a_start < b_end) && (b_start < a_end);
}

void mem_check(void)
{
    unsigned long cache_start, cache_end;
    unsigned long free_lo, free_hi;
    int bad = 0;

    if (mem_map == NULL || max_map_nr <= 0) {
        panic("mem_check: page allocator not initialised");
    }

    if (memory_end < USER_STACK_END) {
        panic("mem_check: less than the identity-mapped 4MB of usable RAM");
    }

    /* 1. The kernel image must not have grown into the kernel's own
          cheap heap (silent corruption if it did: the compiler has no
          idea the heap is there). */
    if ((unsigned long)&_end >= KERNEL_HEAP_START) {
        printk("mem_check: kernel _end=0x%lx >= KERNEL_HEAP_START=0x%lx\n",
               (unsigned long)&_end, (unsigned long)KERNEL_HEAP_START);
        printk("mem_check: move the kernel heap in include/linux/memmap.h\n");
        bad = 1;
    }

    /* 2. Buffer cache vs the user regions: the bug this file exists for.
          The fork() child stack counts too — its old anchor at 0x3E0000
          sat inside the cache, so a Ring3 fork wrote the child's stack
          straight through the filesystem's blocks. */
    cache_start = buf_mem_start;
    cache_end = buffer_cache_end;
    if (cache_start == 0 || cache_end == 0) {
        panic("mem_check: buffer cache not initialised");
    }
    if (cache_end > BUFFER_CACHE_TOP) {
        printk("mem_check: cache ends at 0x%lx, above the cache top 0x%lx "
               "(the user stack starts there)\n",
               cache_end, (unsigned long)BUFFER_CACHE_TOP);
        bad = 1;
    }
    if (cache_start < BUFFER_CACHE_FLOOR) {
        printk("mem_check: cache starts at 0x%lx, below the floor 0x%lx\n",
               cache_start, (unsigned long)BUFFER_CACHE_FLOOR);
        bad = 1;
    }
    if (ranges_overlap(cache_start, cache_end, USER_HEAP_START, USER_HEAP_END)) {
        printk("mem_check: buffer cache [0x%lx,0x%lx) overlaps user heap "
               "[0x%lx,0x%lx)\n",
               cache_start, cache_end,
               (unsigned long)USER_HEAP_START, (unsigned long)USER_HEAP_END);
        bad = 1;
    }
    if (ranges_overlap(cache_start, cache_end, USER_STACK_FLOOR, USER_STACK_TOP)) {
        printk("mem_check: buffer cache [0x%lx,0x%lx) overlaps the user stack "
               "region [0x%lx,0x%lx)\n",
               cache_start, cache_end,
               (unsigned long)USER_STACK_FLOOR, (unsigned long)USER_STACK_TOP);
        bad = 1;
    }
    if (ranges_overlap(cache_start, cache_end,
                       CHILD_USER_STACK_END, CHILD_USER_STACK_TOP)) {
        printk("mem_check: buffer cache [0x%lx,0x%lx) overlaps the fork() "
               "child stack region (top 0x%lx)\n",
               cache_start, cache_end, (unsigned long)CHILD_USER_STACK_TOP);
        bad = 1;
    }

    /* 2b. The Ring3 sigreturn stub and the argv block live in the stack's
           tail page; both must stay inside the part of it that is granted
           to Ring3 ([USER_STACK_FLOOR, USER_TAIL_TOP)). */
    if (USER_SIGRETURN_ENTRY < USER_STACK_TOP ||
        USER_SIGRETURN_ENTRY + 20 > USER_TAIL_TOP) {
        printk("mem_check: sigreturn stub at 0x%lx is outside the granted "
               "tail page\n", (unsigned long)USER_SIGRETURN_ENTRY);
        bad = 1;
    }
    if (USER_ARGV_STR_TOP < USER_STACK_TOP || USER_ARGV_STR_TOP > USER_TAIL_TOP) {
        printk("mem_check: argv string area top 0x%lx is outside the granted "
               "tail page\n", (unsigned long)USER_ARGV_STR_TOP);
        bad = 1;
    }
    if (USER_TAIL_TOP > IDENTITY_MAP_TOP) {
        printk("mem_check: user tail page ends at 0x%lx, past the RAM top\n",
               (unsigned long)USER_TAIL_TOP);
        bad = 1;
    }

    /* 3. The cache must not have been silently truncated either: if the
          derived NR_BUFFERS did not fit the gap, buffer_init() would
          manage fewer buffers and the FS would still work, just with a
          different cache size than the headers promise. */
    if (nr_buffers != NR_BUFFERS) {
        printk("mem_check: cache holds %d buffers, NR_BUFFERS=%d\n",
               nr_buffers, NR_BUFFERS);
        bad = 1;
    }

    /* 4. User regions must be disjoint and inside the identity map. */
    if (USER_HEAP_END > USER_STACK_TOP) {
        printk("mem_check: user heap top 0x%lx above stack top 0x%lx\n",
               (unsigned long)USER_HEAP_END, (unsigned long)USER_STACK_TOP);
        bad = 1;
    }
    if (USER_STACK_END > IDENTITY_MAP_TOP) {
        printk("mem_check: user stack end 0x%lx outside the identity map\n",
               (unsigned long)USER_STACK_END);
        bad = 1;
    }

    /* 5. Page-allocator pool vs the fixed user regions.  get_free_page()
          hands out pages from the whole free map; the user program image
          and the user heap sit at fixed addresses in that same identity
          map, so an allocator that could reach past the pool ceiling
          would hand a live user page to the kernel. */
    free_lo = 0;
    free_hi = 0;
    {
        int i;
        for (i = 0; i < max_map_nr; i++) {
            unsigned long addr;
            if (mem_map[i] != 0)
                continue;
            addr = KERNEL_LOW_MEM + (unsigned long)i * PAGE_SIZE;
            if (free_lo == 0 || addr < free_lo)
                free_lo = addr;
            if (addr + PAGE_SIZE > free_hi)
                free_hi = addr + PAGE_SIZE;
        }
    }
    if (free_hi > KERNEL_POOL_END) {
        printk("mem_check: page allocator can reach 0x%lx, past the pool "
               "ceiling 0x%lx (user program image)\n",
               free_hi, (unsigned long)KERNEL_POOL_END);
        bad = 1;
    }
    if (free_hi > free_lo && (free_hi - free_lo) < 16 * PAGE_SIZE) {
        printk("mem_check: only %luKB of page pool left below the user image\n",
               (free_hi - free_lo) / 1024);
    }

    if (bad) {
        printk("\n--- memory map (%lu KB usable) ---\n", memory_end / 1024);
        printk("  kernel image   [0x%lx, 0x%lx)\n",
               (unsigned long)KERNEL_IMG_BASE, (unsigned long)&_end);
        printk("  kernel heap    [0x%lx, 0x%lx)  (unused)\n",
               (unsigned long)KERNEL_HEAP_START, (unsigned long)KERNEL_HEAP_END);
        printk("  page pool      [0x%lx, 0x%lx)\n",
               (unsigned long)KERNEL_POOL_START, (unsigned long)KERNEL_POOL_END);
        printk("  user program   [0x%lx, 0x%lx)\n",
               (unsigned long)USER_PROG_START, (unsigned long)USER_PROG_END);
        printk("  user heap      [0x%lx, 0x%lx)\n",
               (unsigned long)USER_HEAP_START, (unsigned long)USER_HEAP_END);
        printk("  user stack     [0x%lx, 0x%lx)\n",
               (unsigned long)USER_STACK_TOP, (unsigned long)USER_STACK_END);
        printk("  buffer cache   [0x%lx, 0x%lx)  %d x %d bytes\n",
               cache_start, cache_end, nr_buffers, BLOCK_SIZE);
        panic("mem_check: memory map is inconsistent (see dump above)");
    }
}
