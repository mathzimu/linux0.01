#ifndef _MEMLAYOUT_H
#define _MEMLAYOUT_H

/* ====================================================================
 * memlayout.h — THE single source of truth for this kernel's memory map.
 *
 * Purpose: the kernel used to hard-code these addresses in a dozen
 * places (kernel/main.c, mm/memory.c, kernel/sys.c, kernel/process.c,
 * user/lib.c, user/crt.s, tools/build.c ...).  That is how the buffer
 * cache silently ended up overlapping the user heap (see
 * docs/LIMITATIONS.md §2.4).  Every address that is part of the
 * *contract* between boot/head, the page table, the kernel and user
 * land now lives here, and nobody else may hard-code it.
 *
 * This header is deliberately pure preprocessor arithmetic so that
 * BOTH the kernel (include/linux/memmap.h) and user programs
 * (user/lib.c) can include it.  It must never contain declarations.
 *
 * INVARIANTS (enforced by the static asserts in linux/memmap.h and by
 * mem_check() at boot, see mm/memcheck.c):
 *
 *   0x00100000  ┌──────────────────────────────┐
 *               │ page directory + page table0 │  8KB, reserved
 *   0x00102000  ├──────────────────────────────┤
 *               │ kernel image (linked 0x10800)│  code+data+bss, ~168KB
 *   0x002B000   ├──────────────────────────────┤  (see KERNEL_IMAGE_LIMIT)
 *               │ kernel bump heap (unused)    │
 *   0x002D000   ├──────────────────────────────┤
 *               │ ** page-allocator pool **    │  task pages, pipe pages
 *   0x00200000  ├──────────────────────────────┤
 *               │ user program image           │  execve / embedded prog
 *   0x00300000  ├──────────────────────────────┤
 *               │ user heap (malloc)           │
 *   0x00340000  ├──────────────────────────────┤
 *               │ fork() child user stack      │  grows down; private
 *   0x00350000  ├──────────────────────────────┤  to each child, so no
 *               │ kernel buffer cache (top)    │  user page may live here
 *   0x003FF000  ├──────────────────────────────┤
 *               │ user stack (grows down)      │
 *   0x00400000  └──────────────────────────────┘
 *
 * USER_HEAP_END and CHILD_USER_STACK_END bound the window the buffer
 * cache is allowed to occupy; NR_BUFFERS (include/linux/fs.h) is
 * derived from that window so the cache can never share a page with
 * user data again.
 * ==================================================================== */

/* --- physical base of the kernel ---------------------------------- */
#define KERNEL_IMG_BASE     0x0010800   /* boot/head.s loads the image here;
                                           kernel.ld links startup_32 here  */
#define PAGE_DIRECTORY      0x00100000  /* boot/head.s PGDIR               */
#define PAGE_TABLE_0        0x00101000  /* boot/head.s PGTBL0 (PDE[0])     */

/* --- identity map -------------------------------------------------
 * boot/head.s fills PDE[0] with one page table and maps 0..4MB
 * (0x400 PTE entries of 4KB).  Nothing above this address exists as
 * far as the kernel is concerned, so every other constant below must
 * stay strictly underneath it. */
#define IDENTITY_MAP_SIZE   0x00400000
#define IDENTITY_MAP_TOP    (IDENTITY_MAP_SIZE)

/* --- user linear address space ------------------------------------
 * The teaching kernel uses identity paging, so these are simultaneously
 * user virtual addresses and physical addresses.  They are fixed: the
 * ELF loader copies each LOAD segment to its link-time vaddr, which is
 * why every user program must be linked at USER_PROG_START. */
#define USER_PROG_START     0x00200000
#define USER_PROG_END       0x00300000

/* User heap (user/lib.c malloc), the fork() child stack region, the
 * kernel buffer cache and the user stack share the space above the
 * program image.  Each has its own slice, and no two may meet:
 *
 *   [USER_HEAP_START, USER_HEAP_END)              malloc()
 *   (CHILD_USER_STACK_END, CHILD_USER_STACK_TOP]  fork() child stack
 *   [BUFFER_CACHE_FLOOR, BUFFER_CACHE_TOP)        kernel buffer cache
 *   [USER_STACK_FLOOR, USER_STACK_TOP)            user stack (grows down)
 *
 * A smaller heap/stack slice buys a bigger cache. */
#define USER_HEAP_START     0x00310000
#define USER_HEAP_END       0x00340000

/* fork() gives the child a copy of the parent's user stack with its top
 * here, growing down.  This must stay clear of the buffer cache.
 * kernel/process.c refuses to copy more than the region holds. */
#define CHILD_USER_STACK_TOP 0x00340000
#define CHILD_USER_STACK_END 0x00300000

#define BUFFER_CACHE_FLOOR  0x00350000

/* The user stack grows down from USER_STACK_TOP; USER_STACK_FLOOR is how
 * deep it is allowed to go, which is also what bounds the buffer cache
 * from above.  The window between them is the cache. */
#define USER_STACK_FLOOR    0x003F0000
#define BUFFER_CACHE_TOP    USER_STACK_FLOOR

#define USER_STACK_TOP      0x003FF000
#define USER_STACK_END      IDENTITY_MAP_TOP   /* stack grows down from TOP */

/* execve publishes argc/argv just above the stack top (kernel/sys.c,
 * user/crt.s read them back):
 *   0x3FF004  argc
 *   0x3FF008  pointer to the argv array
 *   0x3FF00C  the argv array itself
 * The slot and the array are different addresses on purpose: writing the
 * pointer must not clobber argv[0].
 *
 * The rest of the tail page is laid out so that nothing overlaps:
 *   0x3FF00C .. ~0x3FF050   argv array (up to 16 entries)
 *   0x3FF100                sigreturn stub (9 bytes, written on delivery)
 *   below 0x3FF100          argv strings, packed DOWN from there
 * mem_init() keeps the page allocator's bitmap (mem_map) in the last
 * kilobytes of RAM, so the user-granted tail stops at USER_TAIL_TOP. */
#define USER_ARGC_ADDR      (USER_STACK_TOP + 4)
#define USER_ARGV_PTR_ADDR  (USER_STACK_TOP + 8)
#define USER_ARGV_ADDR      (USER_STACK_TOP + 12)
#define USER_ARGV_STR_TOP   0x003FF100
#define USER_TAIL_TOP       0x003FF400

/* --- signal delivery frame (kernel/process.c do_signal) -------------
 * A custom signal handler runs on the user stack, and when it returns
 * it must get back into the kernel to run sigreturn.  Two facts shape
 * the layout:
 *
 *  1. The handler's own stack frames grow DOWN from the esp it is
 *     entered with, so the word it returns to must sit at a LOWER
 *     address than anything the handler will push.  Putting the
 *     return address at the top of the user stack (0x3FF000) would let
 *     a deep handler overwrite it, so the whole block is placed just
 *     below the interrupted esp instead (exactly what Linux does).
 *  2. The code that issues sigreturn cannot live in the kernel, so nine
 *     bytes of Ring3 stub are written at USER_SIGRETURN_ENTRY, which sits
 *     clear of both the argv array and the strings:
 *         movl $USER_SIGRETURN_SYSCALL, %eax   (B8 43 00 00 00)
 *         int  $0x80                           (CD 80)
 *         jmp  .-2                             (EB FE)
 *     The handler itself is entered by the kernel's iret, so the stub
 *     only has to be the place the handler returns to.
 *
 * Block placed at [user_esp - 4 - SIGFRAME_BYTES, user_esp - 4):
 *     +4   signal number   <- the handler's argument
 *      0   USER_SIGRETURN_ENTRY   <- the handler's return address
 *     -4   ... saved-context snapshot (struct user_regs, 80 bytes) ...
 *     -84
 * ------------------------------------------------------------------ */
#define USER_SIGRETURN_ENTRY     0x003FF100
#define SIGFRAME_BYTES           92   /* 8 for the header + 84 for the context */
#define USER_SIGRETURN_SYSCALL   67

#endif /* _MEMLAYOUT_H */
