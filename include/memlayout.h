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
 * *contract* between boot/head, the page tables, the kernel and user
 * land now lives here, and nobody else may hard-code it.
 *
 * This header is deliberately pure preprocessor arithmetic so that
 * BOTH the kernel (include/linux/memmap.h) and user programs
 * (user/lib.c) can include it.  It must never contain declarations.
 *
 * --------------------------------------------------------------------
 * M3: TWO ADDRESS SPACES (this is the big change from M1/M2)
 * --------------------------------------------------------------------
 * Until M3 there was one address space: a single page directory
 * identity-mapping 0..4MB, with the user program image, heap and stack
 * parked at fixed *physical* addresses inside it.  That worked, but it
 * made fork() semantics wrong (code and heap were literally shared) and
 * it made execve() of a child destroy the parent: the ELF loader wrote
 * the new image to physical 0x200000, which is the page the parent was
 * executing from.  A Ring3 shell could therefore never run a program.
 *
 * Now every process has its own page directory:
 *
 *   PDE[0..3]  kernel identity map 0..16MB   shared, supervisor-only
 *   PDE[32]    the process's user window     private, Ring3-accessible
 *
 * The kernel stays identity-mapped (physical == kernel virtual) so that
 * task pages, the buffer cache, the page tables themselves and mem_map
 * can be reached by plain pointers whatever CR3 happens to hold; user
 * pages are reached through the user window and can live anywhere in
 * RAM.  The user window is exactly one 4MB page-directory entry, which
 * keeps a process's page-table overhead at two pages (directory + one
 * table) and makes region checks a simple range test.
 * ==================================================================== */

/* --- kernel physical layout (identity mapped) ---------------------- */
#define KERNEL_IMG_BASE     0x0010800   /* boot/head.s loads the image here;
                                           kernel.ld links startup_32 here  */
#define PAGE_DIRECTORY      0x00100000  /* boot/head.s PGDIR (the kernel PDT) */
#define PAGE_TABLE_0        0x00101000  /* boot/head.s PGTBL0 (PDE[0])     */

/* One page table per 4MB of identity-mapped kernel space.  boot/head.s
 * fills all of them at boot and every process directory points at them
 * (supervisor-only), so the kernel can address all of RAM whatever CR3
 * holds.  4 tables = 16MB, which is the largest RAM this kernel
 * supports; QEMU is run with -m 16M. */
#define KERNEL_PT_COUNT     4
#define KERNEL_IDENTITY_TOP (KERNEL_PT_COUNT << 22)          /* 0x01000000 */
#define KERNEL_TABLES_END   (PAGE_TABLE_0 + (KERNEL_PT_COUNT << 12))  /* 0x105000 */

/* --- the user address space (per process) -------------------------- */
/* One page-directory entry covers the whole user world, so all four
 * regions below share 4MB of virtual address space and the kernel needs
 * only one additional page table per process. */
#define USER_BASE           0x08000000
#define USER_WINDOW_SIZE    0x00400000
#define USER_WINDOW_TOP     (USER_BASE + USER_WINDOW_SIZE)   /* 0x08400000 */
#define USER_PDE_INDEX      (USER_BASE >> 22)                /* 32 */

#define USER_PROG_START     USER_BASE
#define USER_PROG_END       (USER_BASE + 0x00100000)

/* malloc() (user/lib.c) owns [USER_HEAP_START, USER_HEAP_END); pages are
 * handed out by the kernel on first touch (demand paging), so the region
 * costs nothing until it is used. */
#define USER_HEAP_START     (USER_BASE + 0x00100000)
#define USER_HEAP_END       (USER_BASE + 0x00200000)

/* The user stack grows down from USER_STACK_TOP; USER_STACK_FLOOR is how
 * deep it may go.  Pages appear on demand, so a deep stack is free until
 * it is touched — and the 1MB between the heap and the floor is a guard
 * hole, not memory: touching it kills the process. */
#define USER_STACK_FLOOR    (USER_BASE + 0x00300000)
#define USER_STACK_TOP      (USER_BASE + 0x003FF000)
#define USER_STACK_END      USER_WINDOW_TOP   /* stack grows down from TOP */

/* execve publishes argc/argv just above the stack top (kernel/sys.c,
 * user/crt.s read them back), in the tail page
 * [USER_STACK_TOP, USER_WINDOW_TOP):
 *   +0x004  argc
 *   +0x008  pointer to the argv array
 *   +0x00C  the argv array itself
 * The slot and the array are different addresses on purpose: writing the
 * pointer must not clobber argv[0].
 *
 * The rest of the tail page is laid out so that nothing overlaps:
 *   0x…00C .. ~0x…050   argv array (up to 16 entries)
 *   0x…100              sigreturn stub (9 bytes, written on delivery)
 *   below 0x…100        argv strings, packed DOWN from there */
#define USER_ARGC_ADDR      (USER_STACK_TOP + 4)
#define USER_ARGV_PTR_ADDR  (USER_STACK_TOP + 8)
#define USER_ARGV_ADDR      (USER_STACK_TOP + 12)
#define USER_ARGV_STR_TOP   (USER_STACK_TOP + 0x100)
#define USER_TAIL_TOP       USER_WINDOW_TOP

/* --- signal delivery frame (kernel/process.c do_signal) -------------
 * A custom signal handler runs on the user stack, and when it returns
 * it must get back into the kernel to run sigreturn.  Two facts shape
 * the layout:
 *
 *  1. The handler's own stack frames grow DOWN from the esp it is
 *     entered with, so the word it returns to must sit at a LOWER
 *     address than anything the handler will push.  Putting the return
 *     address at the top of the user stack would let a deep handler
 *     overwrite it, so the whole block is placed just below the
 *     interrupted esp instead (exactly what Linux does).
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
#define USER_SIGRETURN_ENTRY     USER_ARGV_STR_TOP
#define SIGFRAME_BYTES           92   /* 8 for the header + 84 for the context */
#define USER_SIGRETURN_SYSCALL   67

#endif /* _MEMLAYOUT_H */
