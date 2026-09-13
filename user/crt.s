.text
.globl _start

/* Minimal C runtime entry for execve-loaded programs.
   The kernel publishes argc at USER_ARGC_ADDR and a pointer to the argv
   array at USER_ARGV_PTR_ADDR (just above the user stack top) and iret's
   to _start; we read them from there and set up our own stack.  The slot
   and the array are separate addresses - see include/memlayout.h. */

.include "memlayout.inc"

_start:
    /* The kernel's sys_execve() copies each LOAD segment to its link-time
       vaddr (there is no relocation), so a program linked anywhere but
       USER_PROG_START would overwrite whatever lives at its link address.
       __user_prog_start is defined by the Makefile's user link rule; this
       is the only place where a wrong -Ttext can be caught, because it is
       the only user code that runs before any C gets a chance to. */
    movl $__user_prog_start, %eax
    cmpl $USER_PROG_START, %eax
    jne link_bad

    movl USER_ARGV_PTR_ADDR, %ecx /* argv: the pointer stored above the stack */
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

link_bad:
    /* Write "bad link address\n" straight to fd 1 and exit(1).  No C is
       safe to call here: this binary's relocations are already wrong. */
    movl $4, %eax             /* write */
    movl $1, %ebx             /* fd 1 = stdout */
    movl $link_msg, %ecx
    movl $link_msg_len, %edx
    int $0x80
    movl $1, %ebx             /* exit(1) */
    movl $1, %eax
    int $0x80
1:  hlt
    jmp 1b

.data
link_msg:
    .ascii "bad link address\n"
link_msg_len = . - link_msg
