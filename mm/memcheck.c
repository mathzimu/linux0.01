#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/sched.h>

/* Set by fs/buffer.c: how many buffers the cache actually got, the first
 * byte of the cache data area, and the top of the cache region. */
extern int nr_buffers;
extern unsigned long buf_mem_start;
extern unsigned long buffer_cache_end;

/* ====================================================================
 * mem_check() — the memory-map watchdog.
 *
 * M1 introduced this because the buffer cache had silently ended up on
 * top of the user heap: NR_BUFFERS = 512 put the cache at ~0x370000
 * while the heap lived at [0x310000, 0x3FE000), so a user program could
 * hand its own malloc'd bytes to the filesystem cache and have them
 * written to disk.  Ring0 ignores the PTE U/S bit, so nothing faulted
 * and nothing was logged.
 *
 * M3 changed the model the check has to defend: user memory is no longer
 * parked at fixed physical addresses, it is handed out by the frame
 * allocator and mapped into per-process page tables.  The invariant that
 * replaces "the cache must not overlap the user regions" is therefore
 * "every page the allocator offers must be one nobody else owns" —
 * checked here by walking the free list and confirming that nothing in
 * the kernel's own regions (image, page tables, cache, bitmap) is free.
 *
 * Every check below is a fact about addresses, never about the current
 * task, so this is safe to call before the scheduler exists.
 * ==================================================================== */

void mem_check(void)
{
    unsigned long cache_start, cache_end;
    unsigned long kernel_tables_start = PAGE_DIRECTORY;
    unsigned long kernel_tables_end = KERNEL_TABLES_END;
    int free_pages = 0, i, bad = 0;

    if (mem_map == NULL || max_map_nr <= 0)
        panic("mem_check: page allocator not initialised");

    if (memory_end < MEMORY_END_MINIMUM) {
        printk("mem_check: only %luKB of RAM, the layout needs %luKB\n",
               memory_end / 1024, (unsigned long)MEMORY_END_MINIMUM / 1024);
        panic("mem_check: not enough usable RAM");
    }
    if (memory_end > KERNEL_IDENTITY_TOP) {
        printk("mem_check: memory_end 0x%lx is above the identity map "
               "(0x%lx); boot/head.s maps KERNEL_PT_COUNT tables\n",
               memory_end, (unsigned long)KERNEL_IDENTITY_TOP);
        bad = 1;
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

    /* 2. The page directory and the kernel page tables are shared by
          every address space; an allocator that could hand one out would
          corrupt every process at once. */
    for (i = (int)MAP_NR(kernel_tables_start);
         i < (int)MAP_NR(kernel_tables_end) && i < max_map_nr; i++) {
        if (mem_map[i] != USED) {
            printk("mem_check: kernel page table page 0x%lx is not reserved "
                   "(mem_map[%d]=%lu)\n",
                   LOW_MEM + (unsigned long)i * PAGE_SIZE, i, mem_map[i]);
            bad = 1;
        }
    }

    /* 3. The buffer cache must sit inside its window AND be reserved in
          the allocator's map.  This is the M1/M3 version of the original
          bug: the cache may not share a page with anything. */
    cache_start = buf_mem_start;
    cache_end = buffer_cache_end;
    if (cache_start == 0 || cache_end == 0)
        panic("mem_check: buffer cache not initialised");

    if (cache_end > BUFFER_CACHE_TOP) {
        printk("mem_check: cache ends at 0x%lx, above the cache top 0x%lx\n",
               cache_end, (unsigned long)BUFFER_CACHE_TOP);
        bad = 1;
    }
    if (cache_start < BUFFER_CACHE_FLOOR) {
        printk("mem_check: cache starts at 0x%lx, below the floor 0x%lx\n",
               cache_start, (unsigned long)BUFFER_CACHE_FLOOR);
        bad = 1;
    }
    for (i = (int)MAP_NR(cache_start & ~(PAGE_SIZE - 1));
         LOW_MEM + (unsigned long)i * PAGE_SIZE < cache_end; i++) {
        if (i < max_map_nr && mem_map[i] != USED) {
            printk("mem_check: cache page 0x%lx is free in mem_map[%d]=%lu: "
                   "the allocator would hand it out\n",
                   LOW_MEM + (unsigned long)i * PAGE_SIZE, i, mem_map[i]);
            bad = 1;
        }
    }

    /* 3b. The Ring3 sigreturn stub and the argv block live in the stack's
           tail page; all of it must be inside the user window. */
    if (!(USER_STACK_TOP < USER_ARGC_ADDR && USER_ARGV_ADDR < USER_ARGV_STR_TOP &&
          USER_SIGRETURN_ENTRY + 20 <= USER_TAIL_TOP &&
          USER_TAIL_TOP == USER_WINDOW_TOP)) {
        printk("mem_check: the argv/sigreturn block does not fit the user "
               "window tail page\n");
        bad = 1;
    }

    /* 4. The cache must not have been silently truncated either: if the
          derived NR_BUFFERS did not fit the gap, buffer_init() would
          manage fewer buffers and the FS would still work, just with a
          different cache size than the headers promise. */
    if (nr_buffers != NR_BUFFERS) {
        printk("mem_check: cache holds %d buffers, NR_BUFFERS=%d\n",
               nr_buffers, NR_BUFFERS);
        bad = 1;
    }

    /* 5. User regions must be ordered inside the window (the compile-time
          asserts already say so; this catches a header that lies about
          the macros being used at run time). */
    if (!(USER_PROG_START < USER_PROG_END &&
          USER_PROG_END <= USER_HEAP_START &&
          USER_HEAP_START < USER_HEAP_END &&
          USER_HEAP_END <= USER_STACK_FLOOR &&
          USER_STACK_FLOOR < USER_STACK_TOP &&
          USER_STACK_TOP < USER_WINDOW_TOP)) {
        printk("mem_check: user regions are out of order\n");
        bad = 1;
    }

    /* 6. The kernel's own address space must have no user window: if it
          did, every process (and every kernel task) would inherit those
          mappings through the shared page directory. */
    {
        unsigned long *pde = (unsigned long *)kernel_pg_dir;
        if (pde[USER_PDE_INDEX] & PTE_PRESENT) {
            printk("mem_check: the kernel page directory has a user window "
                   "(PDE[%d]=0x%lx)\n", USER_PDE_INDEX, pde[USER_PDE_INDEX]);
            bad = 1;
        }
    }

    /* 7. How much room is there, really?  With M3 every user page comes
          out of this pool, so the number is worth printing. */
    {
        unsigned long first_free = 0;
        for (i = 0; i < max_map_nr; i++) {
            if (mem_map[i] == 0) {
                free_pages++;
                if (first_free == 0)
                    first_free = LOW_MEM + (unsigned long)i * PAGE_SIZE;
            }
        }
        if (free_pages < 32) {
            printk("mem_check: only %d free pages\n", free_pages);
            bad = 1;
        } else {
            printk("mem_check: %d free pages (%luKB) for tasks, pipes and "
                   "user pages, starting at 0x%lx\n",
                   free_pages, (unsigned long)free_pages * PAGE_SIZE / 1024,
                   first_free);
        }
    }

    if (bad) {
        printk("\n--- memory map (%lu KB usable) ---\n", memory_end / 1024);
        printk("  kernel image   [0x%lx, 0x%lx)\n",
               (unsigned long)KERNEL_IMG_BASE, (unsigned long)&_end);
        printk("  kernel heap    [0x%lx, 0x%lx)  (unused)\n",
               (unsigned long)KERNEL_HEAP_START, (unsigned long)KERNEL_HEAP_END);
        printk("  kernel tables  [0x%lx, 0x%lx)  (PDE[0..%d] identity)\n",
               kernel_tables_start, kernel_tables_end, KERNEL_PT_COUNT - 1);
        printk("  buffer cache   [0x%lx, 0x%lx)  %d x %d bytes\n",
               cache_start, cache_end, nr_buffers, BLOCK_SIZE);
        printk("  user window    [0x%lx, 0x%lx)  PDE[%d] per process\n",
               (unsigned long)USER_BASE, (unsigned long)USER_WINDOW_TOP,
               USER_PDE_INDEX);
        printk("    program      [0x%lx, 0x%lx)\n",
               (unsigned long)USER_PROG_START, (unsigned long)USER_PROG_END);
        printk("    heap         [0x%lx, 0x%lx)\n",
               (unsigned long)USER_HEAP_START, (unsigned long)USER_HEAP_END);
        printk("    stack        [0x%lx, 0x%lx) top 0x%lx\n",
               (unsigned long)USER_STACK_FLOOR, (unsigned long)USER_STACK_TOP,
               (unsigned long)USER_STACK_TOP);
        panic("mem_check: memory map is inconsistent (see dump above)");
    }
}
