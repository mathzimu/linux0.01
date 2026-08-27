.text
.globl page_fault

page_fault:
    push %ds
    push %es
    push %fs
    push %gs
    pushal

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* stack now: ds es fs gs eax ecx edx ebx esp ebp esi edi |
       error_code | eip | cs | eflags  (pushal=8, +4 segs = 12)
       From esp: error_code@0x30  eip@0x34  cs@0x38  eflags@0x3C */
    mov %cr2, %eax
    push %eax                      /* address (arg3) */

    mov 0x38(%esp), %eax           /* saved eip */
    push %eax                      /* eip (arg2) */

    mov 0x34(%esp), %eax           /* error_code (after 2 pushes) */
    push %eax                      /* error_code (arg1) */

    call do_no_page
    add $12, %esp

    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret
