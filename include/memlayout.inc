/* ====================================================================
 * memlayout.inc — assembler (.s) view of the memory map.
 *
 * This is the gas "flavour" of include/memlayout.h: the two must be
 * kept in step (scripts/check-layout.py cross-checks the constants and
 * fails CI if they drift apart).  Assembled with -Iinclude so both
 * boot/*.s and user/*.s can write:
 *
 *     .include "memlayout.inc"
 *
 * Only the values user land (and boot/head.s) actually needs are
 * defined here.  The values are literals on purpose: the checker reads
 * them back with a plain hex regex.
 * ==================================================================== */

/* Kernel identity map: boot/head.s builds PDE[0..3] over these tables. */
.equ PAGE_DIRECTORY,     0x00100000   /* kernel page directory            */
.equ PAGE_TABLE_0,       0x00101000   /* first kernel page table (PDE[0]) */
.equ KERNEL_PT_COUNT,    4            /* 4 tables = 16MB identity-mapped  */

/* User space (M3): one page-directory entry per process, PDE[32]. */
.equ USER_BASE,          0x08000000
.equ USER_WINDOW_TOP,    0x08400000
.equ USER_PROG_START,    0x08000000   /* ELF link address of user programs */
.equ USER_HEAP_START,    0x08100000
.equ USER_HEAP_END,      0x08200000
.equ USER_STACK_FLOOR,   0x08300000
.equ USER_STACK_TOP,     0x083FF000
.equ USER_ARGC_ADDR,     0x083FF004   /* execve: argc, read by user/crt.s  */
.equ USER_ARGV_PTR_ADDR, 0x083FF008   /* execve: pointer to the argv array */
.equ USER_ARGV_ADDR,     0x083FF00C   /* execve: the argv array itself     */
.equ USER_ARGV_STR_TOP,  0x083FF100   /* argv strings pack down from here  */
.equ USER_TAIL_TOP,      0x08400000   /* end of the user-granted tail page */

/* Signal delivery: fixed Ring3 stub the handler returns into; it issues
 * the sigreturn syscall (the handler itself is entered by the kernel). */
.equ USER_SIGRETURN_ENTRY,    0x083FF100
.equ USER_SIGRETURN_SYSCALL,  67
.equ SIGFRAME_BYTES,          92
