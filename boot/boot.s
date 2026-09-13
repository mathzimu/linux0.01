.code16
.text
.globl _start

.equ SETUP_SECTORS, 4
.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800      /* kernel runs at 0x10000 + 0x800 = 0x10800 */

/* Disk layout written by tools/build: LBA 0 = boot sector,
   LBA 1..SETUP_SECTORS = setup, kernel from LBA 1+SETUP_SECTORS.
   CHS LBA = ((C * 2) + H) * 18 + (S - 1), so the kernel begins at
   CHS(0,0,2+SETUP_SECTORS).

   Design notes, all of them learned by tracing this image in QEMU:
     * the kernel used to be read from CHS(0,0,5) = LBA 4 while build.c
       puts it at LBA 5, so the loader read the tail of setup's padding;
     * multi-sector INT 13h reads are unreliable under SeaBIOS 1.16 (it
       reports success while transferring fewer sectors), so the load is
       one sector per call;
     * INT 13h is not required to preserve any register, and in practice
       it clobbers the general-purpose ones, so *all* loader state -
       remaining count, current LBA, destination - lives in memory and is
       reloaded every iteration.  Keeping the counter in a register made
       the loop stop early, leaving the tail of the image as whatever was
       in RAM (which is what a stale GDT looked like);
     * the destination is a 32-bit linear address converted to seg:off
       each time; comparing a 16-bit register against 0x10000 does not
       work because the assembler truncates the immediate. */
.equ SECTORS_PER_TRACK, 18
.equ SECTORS_PER_CYL, (SECTORS_PER_TRACK * 2)
.equ KERNEL_LBA, 1 + SETUP_SECTORS

_start:
    mov $0x07C0, %ax
    mov %ax, %ds
    mov %dl, (drive)
    mov $0x2401, %ax
    int $0x15
    mov $0x13, %ah
    mov $0x01, %al
    int $0x10
    mov $0x03, %ah
    xor %bh, %bh
    int $0x10

    /* --- setup: SETUP_SECTORS sectors from CHS(0,0,2) = LBA 1 --- */
    mov (drive), %dl
    mov $SETUPSEG, %ax
    mov %ax, %es
    xor %bx, %bx
    mov $0x0002, %cx
    xor %dh, %dh
    mov $0x0200 | SETUP_SECTORS, %ax
    int $0x13
    jnc load_kernel
    xor %ah, %ah
    int $0x13
    mov $0x0200 | SETUP_SECTORS, %ax
    int $0x13
    jnc load_kernel
    jmp _start

load_kernel:
    mov (kernel_sectors), %ax
    mov %ax, (left)
    mov $KERNEL_LBA, %eax
    mov %eax, (cur_lba)
    mov $0x10800, %eax
    mov %eax, (dest_lin)

sector_loop:
    cmpw $0, (left)
    je kernel_done

    /* CHS from cur_lba:  C = LBA / 36, rem = LBA % 36,
                          H = rem / 18, S = (rem % 18) + 1 */
    mov (cur_lba), %eax
    xor %edx, %edx
    mov $36, %ecx
    div %ecx                   /* eax = C, edx = rem */
    mov %eax, (cyl)
    mov %edx, %eax
    xor %edx, %edx
    mov $18, %ecx
    div %ecx                   /* eax = H, edx = rem */
    mov %al, (head)
    inc %edx
    mov %dl, (sect)

    /* cx = (cyl << 8) | sect */
    mov (cyl), %eax
    shl $8, %eax
    movzbl (sect), %edx
    or  %edx, %eax
    mov %eax, (chs_cx)
    /* dx = (head << 8) | drive */
    movzbl (head), %eax
    shl $8, %eax
    movzbl (drive), %edx
    or  %edx, %eax
    mov %eax, (chs_dx)

    /* es:bx from dest_lin */
    mov (dest_lin), %eax
    mov %eax, %ebx
    shr $4, %ebx
    mov %bx, (dst_seg)
    mov (dest_lin), %eax
    and $0x000F, %eax
    mov %ax, (dst_off)

    /* read one sector straight into its final address */
    mov (dst_seg), %ax
    mov %ax, %es
    mov (dst_off), %bx
    mov (chs_cx), %cx
    mov (chs_dx), %dx
    mov $0x0201, %ax           /* ah=02 read, al=1 sector */
    int $0x13
    jc read_failed

    decw (left)
    incl (cur_lba)
    addl $512, (dest_lin)
    jmp sector_loop

kernel_done:
    mov $SETUPSEG, %ax
    mov %ax, %ds
    mov $0x0003, %ax
    mov %ax, %fs
    ljmp $SETUPSEG, $0x0000

read_failed:
    /* Report the INT 13h status and remaining count as raw bytes on the
       QEMU debug port (0xE9), then stop rather than jumping into memory
       that was never loaded. */
    movb $'!', %al
    out %al, $0xE9
    mov %ah, %al
    out %al, $0xE9
    mov (left), %ax
    out %al, $0xE9
    mov %ah, %al
    out %al, $0xE9
    mov $0x0A, %al
    out %al, $0xE9
    cli
1:  hlt
    jmp 1b

drive:          .byte 0
left:           .word 0
cur_lba:        .long KERNEL_LBA
dest_lin:       .long 0x10800
cyl:            .word 0
head:           .byte 0
sect:           .byte 0
chs_cx:         .word 0
chs_dx:         .word 0
dst_seg:        .word 0
dst_off:        .word 0

.org 0x1F0
setup_sectors:  .word SETUP_SECTORS
kernel_sectors: .word 0
.org 0x1FE
.word 0xAA55
