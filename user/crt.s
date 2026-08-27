.text
.globl _start

/* Minimal C runtime entry for execve-loaded programs.
   The kernel publishes argc at 0x3FF004 and argv at 0x3FF008
   (just above the user stack top 0x3FF000) and iret's to _start;
   we read them from there and set up our own stack. */

_start:
    movl 0x3FF008, %ecx       /* argv */
    movl 0x3FF004, %edx       /* argc */
    movl $0x3FF000, %esp      /* user stack top */
    pushl %ecx
    pushl %edx
    call main
    /* exit(main's return value) */
    movl %eax, %ebx
    movl $1, %eax
    int $0x80
    hlt
