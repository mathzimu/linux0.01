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

    mov %cr2, %eax
    push %eax
    mov 52(%esp), %eax
    push %eax
    call do_no_page
    add $8, %esp

    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret
