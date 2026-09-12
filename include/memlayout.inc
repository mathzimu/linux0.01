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
 * Only the values user land actually needs are defined here.
 * ==================================================================== */

.equ USER_PROG_START,    0x00200000   /* ELF link address of user programs */
.equ USER_HEAP_START,    0x00310000
.equ USER_HEAP_END,      0x00340000
.equ CHILD_USER_STACK_TOP, 0x00340000 /* fork() child stack, grows down  */
.equ CHILD_USER_STACK_END, 0x00300000
.equ BUFFER_CACHE_FLOOR, 0x00350000
.equ USER_STACK_TOP,     0x003FF000
.equ IDENTITY_MAP_TOP,   0x00400000
.equ USER_ARGC_ADDR,     0x003FF004   /* execve: argc, read by user/crt.s  */
.equ USER_ARGV_ADDR,     0x003FF008   /* execve: argv array pointer        */

/* Signal delivery: fixed Ring3 stub the kernel writes and the handler
 * returns into (it issues the sigreturn syscall). */
.equ USER_SIGRETURN_ENTRY,    0x003FF010
.equ USER_SIGRETURN_STUB_ARG, 0x003FF014
.equ USER_SIGRETURN_SYSCALL,  67
.equ SIGFRAME_BYTES,          92
