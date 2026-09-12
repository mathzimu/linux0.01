.text
.globl _start

/* Minimal C runtime entry for execve-loaded programs.
   The kernel publishes argc at USER_ARGC_ADDR and argv at USER_ARGV_ADDR
   (just above the user stack top) and iret's to _start; we read them
   from there and set up our own stack.  All three addresses come from
   include/memlayout.inc so the assembler and the kernel cannot drift. */

.include "memlayout.inc"

_start:
    movl USER_ARGV_ADDR, %ecx /* argv */
    movl USER_ARGC_ADDR, %edx /* argc */
    movl $USER_STACK_TOP, %esp
    pushl %ecx
    pushl %edx
    call main
    /* exit(main's return value) */
    movl %eax, %ebx
    movl $1, %eax
    int $0x80
    hlt
