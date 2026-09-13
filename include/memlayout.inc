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
.equ USER_STACK_FLOOR,   0x003F0000
.equ BUFFER_CACHE_TOP,   0x003F0000
.equ USER_STACK_TOP,     0x003FF000
.equ IDENTITY_MAP_TOP,   0x00400000
.equ USER_ARGC_ADDR,     0x003FF004   /* execve: argc, read by user/crt.s  */
.equ USER_ARGV_PTR_ADDR, 0x003FF008   /* execve: pointer to the argv array */
.equ USER_ARGV_ADDR,     0x003FF00C   /* execve: the argv array itself     */
.equ USER_ARGV_STR_TOP,  0x003FF100   /* argv strings pack down from here  */
.equ USER_TAIL_TOP,      0x003FF400   /* end of the user-granted tail page */

/* Signal delivery: fixed Ring3 stub the handler returns into; it issues
 * the sigreturn syscall (the handler itself is entered by the kernel). */
.equ USER_SIGRETURN_ENTRY,    0x003FF100
.equ USER_SIGRETURN_SYSCALL,  67
.equ SIGFRAME_BYTES,          92
