.code16
.text
.globl _start

.equ SETUP_SECTORS, 4
.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800  /* kernel at 0x10000 + 0x800 = 0x10800 */

_start:
    mov %dl, (drive)
    mov $0x2401, %ax
    int $0x15
    mov $0x13, %ah
    mov $0x01, %al
    int $0x10
    mov $0x03, %ah
    xor %bh, %bh
    int $0x10

    /* Load setup: SETUP_SECTORS sectors from CHS(0,0,2) to 0x10000 */
    mov (drive), %dl
    mov $SETUPSEG, %ax
    mov %ax, %es
    xor %bx, %bx
    mov $0x0002, %cx    /* CHS(0,0,2) */
    xor %dh, %dh
    mov $0x0200 | SETUP_SECTORS, %ax
    int $0x13
    jnc load_setup_ok
    xor %ah, %ah
    int $0x13
    mov $0x0200 | SETUP_SECTORS, %ax
    int $0x13
    jnc load_setup_ok
    jmp _start

load_setup_ok:
    /* Load kernel: kernel_sectors sectors from CHS(0,0,1+1+SETUP_SECTORS) */
    mov $SYSSEG, %ax
    mov %ax, %es
    mov $SYSOFF, %bx
    mov (drive), %dl
    xor %dh, %dh
    mov (kernel_sectors), %al
    or %al, %al
    jz load_done
    mov $0x0001+1+SETUP_SECTORS, %cx
    mov $0x02, %ah
    int $0x13
    jnc load_done

    /* Retry with disk reset */
    xor %ah, %ah
    int $0x13
    mov (kernel_sectors), %al
    mov $0x0001+1+SETUP_SECTORS, %cx
    mov $0x02, %ah
    int $0x13
    jnc load_done
    jmp load_done

load_done:
    mov $SETUPSEG, %ax
    mov %ax, %ds
    mov $0x0003, %ax
    mov %ax, %fs
    ljmp $SETUPSEG, $0x0000

drive:          .byte 0
.org 0x1F0
setup_sectors:  .word SETUP_SECTORS
kernel_sectors: .word 0
.org 0x1FE
.word 0xAA55
