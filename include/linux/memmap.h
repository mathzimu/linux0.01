#ifndef _MEMMAP_H
#define _MEMMAP_H

/* ====================================================================
 * Kernel-side view of the memory map: symbolic region boundaries plus
 * the static (compile-time) layout asserts and the runtime self-check.
 *
 * The addresses themselves live in include/memlayout.h, which is shared
 * with user land.  Nothing in the kernel may hard-code them.
 * ==================================================================== */

#include <memlayout.h>
#include <linux/fs.h>          /* BLOCK_SIZE, struct buffer_head */
#include <static-assert.h>

/* --- kernel-side region boundaries -------------------------------- */

/* Where boot/boot.s parks the boot parameter block (setup.s writes the
 * INT 15h extended-memory size here as a 16-bit KB count). */
#define BOOT_PARAM_ADDR     0x0010002

/* Nothing below this is ever handed out by the page allocator; it holds
 * the page directory/table and the kernel image. */
#define KERNEL_LOW_MEM      0x00100000

/* The kernel image (code + data + bss) is linked at KERNEL_IMG_BASE and
 * grows upward as code is added.  This is the ceiling it is allowed to
 * reach: past it the compiler would silently start overwriting the
 * kernel bump heap.
 *
 * The real figure is the link-time _end, which mem_check() and
 * scripts/check-layout.py both verify against this limit.  Note how much
 * bigger the bss is than the linked binary: the image on disk is ~80KB,
 * while _end lands near 0x29E68 (~168KB) because of the static tables
 * (page allocator bitmap, inode/file tables, tty buffers).  Sizing this
 * from the binary alone is exactly the mistake this constant used to
 * contain. */
#define KERNEL_IMAGE_LIMIT  0x002B000

/* Cheap non-page kernel heap (lib/malloc.c): grows up from
 * KERNEL_HEAP_START, must stop before the page-allocator pool. */
#define KERNEL_HEAP_START   0x002B000
#define KERNEL_HEAP_END     0x002D000

/* Page-allocator pool (mm/memory.c get_free_page): task pages, pipe
 * pages.  It must stay below USER_PROG_START, because the identity map
 * makes those very same physical pages the user program image. */
#define KERNEL_POOL_START   KERNEL_HEAP_END
#define KERNEL_POOL_END     USER_PROG_START

/* Top of usable RAM.  main() still clamps to this; mem_check() refuses
 * to run with less, because the whole map above assumes 4MB. */
#define PHYS_MEM_TOP        IDENTITY_MAP_TOP

/* The kernel buffer cache sits in the window between the heap/child-stack
 * area and the user stack: BUFFER_CACHE_TOP is USER_STACK_FLOOR and
 * BUFFER_CACHE_FLOOR is defined next to it in include/memlayout.h.
 * NR_BUFFERS (include/linux/fs.h) is derived from that window, so the
 * cache can never share a page with user data.
 *
 * History worth keeping: main() used to pass "memory_end - 0x100000" to
 * buffer_init(), which treats its argument as an address, so the cache
 * really sat at [0x27C000, 0x300000) - inside the user program image
 * region.  It is now anchored explicitly at BUFFER_CACHE_TOP. */
#define BUFFER_CACHE_WINDOW (BUFFER_CACHE_TOP - BUFFER_CACHE_FLOOR)

/* --- static layout asserts ----------------------------------------
 * These turn a bad edit into a BUILD FAILURE instead of a filesystem
 * that silently corrupts itself at run time. */

STATIC_ASSERT(IDENTITY_MAP_TOP <= 0x01000000, identity_map_sane);

STATIC_ASSERT(KERNEL_IMG_BASE < KERNEL_IMAGE_LIMIT, kernel_image_room);
STATIC_ASSERT(KERNEL_IMAGE_LIMIT <= KERNEL_HEAP_START, kernel_image_below_heap);
STATIC_ASSERT(KERNEL_HEAP_END <= KERNEL_POOL_START, kernel_heap_below_pool);
STATIC_ASSERT(KERNEL_POOL_END <= USER_PROG_START, kernel_pool_below_user_prog);

STATIC_ASSERT(KERNEL_LOW_MEM == USER_PROG_START - 0x00100000, low_mem_agrees_with_user_prog);
STATIC_ASSERT(PAGE_TABLE_0 == PAGE_DIRECTORY + 0x1000, page_table_follows_directory);
/* boot/boot.s loads the kernel at 0x10800: 0x100000 - 63.5KB (the low
 * 1MB holds BIOS, the boot sector at 0x7C00, setup.s at 0x10000 and the
 * kernel image from 0x10800). */
STATIC_ASSERT(KERNEL_IMG_BASE == KERNEL_LOW_MEM - 0xEF800, kernel_base_load_offset);

STATIC_ASSERT(USER_PROG_START < USER_PROG_END, user_prog_ordered);
STATIC_ASSERT(USER_PROG_END <= USER_HEAP_START, user_prog_below_heap);
STATIC_ASSERT(USER_HEAP_START < USER_HEAP_END, user_heap_ordered);
STATIC_ASSERT(USER_HEAP_END <= USER_STACK_FLOOR, user_heap_below_stack);
STATIC_ASSERT(USER_STACK_FLOOR < USER_STACK_TOP, user_stack_has_room);
STATIC_ASSERT(USER_STACK_TOP < IDENTITY_MAP_TOP, user_stack_inside_map);
STATIC_ASSERT(USER_ARGV_STR_TOP > USER_STACK_TOP, argv_block_above_stack);
STATIC_ASSERT(USER_ARGV_STR_TOP < IDENTITY_MAP_TOP, argv_block_below_map_top);
STATIC_ASSERT(USER_STACK_END == IDENTITY_MAP_TOP, stack_ends_at_map_top);

/* The fork() child stack region must be disjoint from the heap and from
 * the buffer cache window: a Ring3 fork writes a copy of the parent's
 * stack there, and if it lands in the cache the filesystem's blocks are
 * silently overwritten (the old anchor of 0x3E0000 did exactly that). */
STATIC_ASSERT(USER_HEAP_END <= CHILD_USER_STACK_TOP, child_stack_below_heap);
STATIC_ASSERT(CHILD_USER_STACK_END < CHILD_USER_STACK_TOP, child_stack_ordered);
STATIC_ASSERT(USER_PROG_START <= CHILD_USER_STACK_END, child_stack_above_prog);
STATIC_ASSERT(CHILD_USER_STACK_TOP <= BUFFER_CACHE_FLOOR, child_stack_below_cache);
STATIC_ASSERT(BUFFER_CACHE_FLOOR < BUFFER_CACHE_TOP, cache_floor_below_top);
/* The cache must not reach into the user stack. */
STATIC_ASSERT(BUFFER_CACHE_TOP <= USER_STACK_FLOOR, cache_below_user_stack);

/* The cache has to fit in its window.  64 is a conservative stand-in for
 * sizeof(struct buffer_head), which is 32 on this target; the real count
 * is derived in include/linux/fs.h and verified at boot by mem_check().
 * NR_BUFFERS and NR_BUFFERS_MAX are defined there, not here. */
STATIC_ASSERT(BUFFER_CACHE_WINDOW >= (8 * (BLOCK_SIZE + 64)), cache_window_has_room);
STATIC_ASSERT(NR_BUFFERS * (BLOCK_SIZE + 64) <= BUFFER_CACHE_WINDOW,
              nr_buffers_fit_the_window);

/* --- runtime self-check (mm/memcheck.c) ---------------------------
 * Called once from main(), right after mem_init() and buffer_init()
 * have had their say and before anything can write through the map.
 * Panics with a memory map dump on any inconsistency. */
void mem_check(void);

/* The real link-time end of the kernel image (_end); checked at boot
 * against KERNEL_IMAGE_LIMIT so a grown kernel cannot silently walk
 * into the heap. */
extern unsigned long _end;

#endif /* _MEMMAP_H */
