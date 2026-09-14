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
 * the boot code, the kernel image and the VGA/BIOS holes. */
#define KERNEL_LOW_MEM      0x00100000

/* The kernel image (code + data + bss) is linked at KERNEL_IMG_BASE and
 * grows upward as code is added.  This is the ceiling it is allowed to
 * reach: past it the compiler would silently start overwriting the
 * kernel bump heap.
 *
 * The real figure is the link-time _end, which mem_check() and
 * scripts/check-layout.py both verify against this limit.  Note how much
 * bigger the bss is than the linked binary: the image on disk is ~93KB,
 * while _end lands near 0x2Axxx (~170KB) because of the static tables
 * (page allocator bitmap, inode/file tables, tty buffers).  Sizing this
 * from the binary alone is exactly the mistake this constant used to
 * contain.
 *
 * Raised from 0x2B000 to 0x30000 when the permission model landed: the
 * check had been down to ~4KB of headroom, which is not enough to add a
 * feature without tripping over it.  Everything below KERNEL_LOW_MEM is
 * free for the taking — the heap just has to stay under 1MB, where the
 * page tables and the frame pool begin. */
#define KERNEL_IMAGE_LIMIT  0x0030000

/* Cheap non-page kernel heap (lib/malloc.c): grows up from
 * KERNEL_HEAP_START, must stop before the rest of the low 1MB. */
#define KERNEL_HEAP_START   0x0030000
#define KERNEL_HEAP_END     0x0040000

/* Page-allocator pool: task pages, pipe pages and — since M3 — every
 * user page.  It is simply "whatever mem_map still reports as free":
 * mm/mem_init() reserves the low 1MB, the kernel page tables, the
 * buffer cache (fs/buffer.c marks its own pages) and mem_map itself.
 *
 * Before M3 this had a hard ceiling (USER_PROG_START) because the user
 * program image lived at a fixed *physical* address; the pool could
 * otherwise have handed a running program's pages to the kernel.  With
 * per-process address spaces user pages are ordinary pool pages, so the
 * ceiling is gone and exhaustion is a normal (teachable) condition:
 * get_free_page() returns 0. */
#define KERNEL_POOL_START   KERNEL_LOW_MEM
#define KERNEL_POOL_END     KERNEL_IDENTITY_TOP

/* Top of usable RAM.  main() clamps to this; mem_check() refuses to run
 * with less than it needs, because the whole map assumes the kernel can
 * address all of it. */
#define PHYS_MEM_TOP        KERNEL_IDENTITY_TOP

/* The least RAM this kernel will boot with: the buffer cache ends at
 * BUFFER_CACHE_TOP, and the page allocator's bitmap lives in the last
 * few KB of whatever RAM there is.  QEMU is run with -m 16M, which is
 * the largest the four kernel page tables can map; asking for less is
 * fine as long as it is at least this. */
#define MEMORY_END_MINIMUM  0x00400000

/* --- swap area (B4) ------------------------------------------------
 * The disk image is laid out as [MINIX filesystem][raw swap].  The fs
 * records its own size (1024 1KB zones = 1MB, tools/mkminix.c), so the
 * kernel can address the swap region directly by LBA without asking the
 * filesystem anything — swap is a raw device, not a file, which is also
 * why the page-reclaim path can use it with no buffers and no inode.
 *
 * One slot holds exactly one 4KB page, so a slot number is all a page
 * table entry needs in order to remember where its contents went. */
#define SWAP_START_LBA    2048                   /* 1MB into the image */
#define SWAP_SECTORS      4096                   /* 2MB of swap        */
#define SWAP_SLOT_SECTORS 8                      /* 8 x 512B = 4KB page */
#define NR_SWAP_SLOTS     (SWAP_SECTORS / SWAP_SLOT_SECTORS)  /* 512 pages */

/* The kernel buffer cache sits in the window between the end of the
 * kernel page tables and the low 1MB hole, nowhere near user memory: it
 * is kernel-only, mapped identity in every address space, and marked
 * USED in mem_map (fs/buffer.c does that itself) so the frame allocator
 * can never hand a cache page to a process.  NR_BUFFERS
 * (include/linux/fs.h) is derived from this window. */
#define BUFFER_CACHE_FLOOR  0x00350000
#define BUFFER_CACHE_TOP    0x003F0000
#define BUFFER_CACHE_WINDOW (BUFFER_CACHE_TOP - BUFFER_CACHE_FLOOR)

/* --- static layout asserts ----------------------------------------
 * These turn a bad edit into a BUILD FAILURE instead of a filesystem
 * that silently corrupts itself at run time. */

STATIC_ASSERT(KERNEL_IMG_BASE < KERNEL_IMAGE_LIMIT, kernel_image_room);
STATIC_ASSERT(KERNEL_IMAGE_LIMIT <= KERNEL_HEAP_START, kernel_image_below_heap);
STATIC_ASSERT(KERNEL_HEAP_END <= KERNEL_LOW_MEM, kernel_heap_below_pool);
STATIC_ASSERT(KERNEL_LOW_MEM < KERNEL_IDENTITY_TOP, identity_map_nonempty);

STATIC_ASSERT(PAGE_TABLE_0 == PAGE_DIRECTORY + 0x1000, page_table_follows_directory);
/* The page directory and the kernel page tables sit just above the low
 * 1MB, where nothing else lives; the cache and the RAM floor are well
 * above them. */
STATIC_ASSERT(KERNEL_LOW_MEM <= PAGE_DIRECTORY, page_directory_above_low_mem);
STATIC_ASSERT(KERNEL_TABLES_END <= MEMORY_END_MINIMUM, kernel_tables_below_min_ram);
/* boot/boot.s loads the kernel at 0x10800: 0x100000 - 63.5KB (the low
 * 1MB holds BIOS, the boot sector at 0x7C00, setup.s at 0x10000 and the
 * kernel image from 0x10800). */
STATIC_ASSERT(KERNEL_IMG_BASE == KERNEL_LOW_MEM - 0xEF800, kernel_base_load_offset);

/* The user window is exactly one page-directory entry, and every user
 * region must live inside it (that is what makes a per-process page
 * table enough). */
STATIC_ASSERT(USER_BASE == (USER_PDE_INDEX << 22), user_base_is_a_pde_boundary);
STATIC_ASSERT(USER_WINDOW_SIZE == 0x400000, user_window_is_one_pde);
STATIC_ASSERT(USER_PROG_START == USER_BASE, prog_at_window_base);
STATIC_ASSERT(USER_PROG_START < USER_PROG_END, user_prog_ordered);
STATIC_ASSERT(USER_PROG_END <= USER_HEAP_START, user_prog_below_heap);
STATIC_ASSERT(USER_HEAP_START < USER_HEAP_END, user_heap_ordered);
STATIC_ASSERT(USER_HEAP_END <= USER_STACK_FLOOR, user_heap_below_stack);
STATIC_ASSERT(USER_STACK_FLOOR < USER_STACK_TOP, user_stack_has_room);
STATIC_ASSERT(USER_STACK_TOP < USER_WINDOW_TOP, user_stack_inside_window);
STATIC_ASSERT(USER_ARGV_STR_TOP > USER_STACK_TOP, argv_block_above_stack);
STATIC_ASSERT(USER_ARGV_STR_TOP < USER_WINDOW_TOP, argv_block_below_window_top);
STATIC_ASSERT(USER_STACK_END == USER_WINDOW_TOP, stack_ends_at_window_top);
STATIC_ASSERT(USER_SIGRETURN_ENTRY == USER_ARGV_STR_TOP, stub_at_string_top);

/* The cache has to fit in its window.  64 is a conservative stand-in for
 * sizeof(struct buffer_head), which is 32 on this target; the real count
 * is derived in include/linux/fs.h and verified at boot by mem_check().
 * NR_BUFFERS and NR_BUFFERS_MAX are defined there, not here. */
STATIC_ASSERT(BUFFER_CACHE_WINDOW >= (8 * (BLOCK_SIZE + 64)), cache_window_has_room);
STATIC_ASSERT(NR_BUFFERS * (BLOCK_SIZE + 64) <= BUFFER_CACHE_WINDOW,
              nr_buffers_fit_the_window);
STATIC_ASSERT(KERNEL_TABLES_END <= BUFFER_CACHE_FLOOR, cache_above_kernel_tables);
STATIC_ASSERT(MEMORY_END_MINIMUM > BUFFER_CACHE_TOP, minimum_ram_holds_the_cache);
STATIC_ASSERT(SWAP_SLOT_SECTORS * 512 == 4096, swap_slot_is_one_page);
STATIC_ASSERT(NR_SWAP_SLOTS <= 4096, swap_slot_fits_in_a_pte);

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
