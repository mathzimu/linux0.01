.text
.globl ltr

ltr:
    movl 4(%esp), %eax
    ltr %ax
    ret
