.code16
.text
.globl _start

.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800

_start:
    mov $SETUPSEG, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov $0x0800, %sp

    mov $0x88, %ah
    int $0x15
    mov %ax, (2)

    cli

    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    mov $0x11, %al
    outb %al, $0x20
    outb %al, $0xA0
    mov $0x20, %al
    outb %al, $0x21
    mov $0x28, %al
    outb %al, $0xA1
    mov $0x04, %al
    outb %al, $0x21
    mov $0x02, %al
    outb %al, $0xA1
    mov $0x01, %al
    outb %al, $0x21
    outb %al, $0xA1
    mov $0x00, %al
    outb %al, $0x21
    outb %al, $0xA1

    mov $0xFF, %al
    outb %al, $0xA1

    lgdt (gdt_descr - _start)
    lidt (idt_descr - _start)

    mov %cr0, %eax
    or  $1, %al
    mov %eax, %cr0

    .byte 0x66, 0xea
    .long SYSSEG*16 + SYSOFF
    .word 0x08

gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF

gdt_descr:
    .word (3*8)-1
    .long (gdt - _start + SETUPSEG*16)

idt_descr:
    .word 0
    .long 0
