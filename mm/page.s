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

    /* error_code: after the two pushes above it sits 8 bytes higher than
       in the "from esp" map at the top of this file, i.e. at 0x38.
       Reading 0x34 here handed do_no_page the *eip* as the error code
       (and the same value again as the eip argument), so every fault
       looked like a kernel instruction fetch. */
    mov 0x38(%esp), %eax           /* error_code (after 2 pushes) */
    push %eax                      /* error_code (arg1) */

    call do_no_page
    add $12, %esp

    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds

    /* The CPU pushed an error code below the iret frame (for #PF it is
       always present), and nothing above ever consumed it: the pushes and
       pops of this handler are symmetric, so esp now points AT the error
       code rather than at the saved eip.  Without this skip the iret
       would pop the error code as eip and shift the whole frame by one
       word - harmless while do_no_page() always ended the task, fatal as
       soon as it returns (demand paging, copy-on-write). */
    add $4, %esp
    iret
