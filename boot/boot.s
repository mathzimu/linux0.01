.code16
.text
.globl _start

.equ SETUP_SECTORS, 4
.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800      /* kernel runs at 0x10000 + 0x800 = 0x10800 */

/* Disk layout written by tools/build: LBA 0 = this boot sector,
   LBA 1..SETUP_SECTORS = setup, kernel from LBA 1+SETUP_SECTORS.
   CHS LBA = ((C * 2) + H) * 18 + (S - 1), so the kernel begins at
   CHS(0,0,2+SETUP_SECTORS).

   History, because each of these cost a debugging session:
     * the kernel used to be read from CHS(0,0,5) = LBA 4 while
       tools/build puts it at LBA 5, so the image loaded was the tail of
       setup's zero padding: the kernel appeared to "run" as a stream of
       zeros and never produced output;
     * one INT 13h read cannot cover the whole kernel here - SeaBIOS
       1.16 delivers at most 8 sectors and, worse, can report success
       while transferring less, so a single big read left the tail
       unloaded;
     * reads therefore go one sector at a time into a staging buffer low
       in conventional memory, and the whole kernel is copied to its link
       address afterwards.  That keeps every individual read small and
       keeps es:bx well inside one 64KB window. */
.equ SECTORS_PER_TRACK, 18
.equ SECTORS_PER_CYL, (SECTORS_PER_TRACK * 2)
.equ STAGESEG, 0x3000      /* 0x30000: staging area for the whole kernel */
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
    jnc kernel_load
    xor %ah, %ah
    int $0x13
    mov $0x0200 | SETUP_SECTORS, %ax
    int $0x13
    jnc kernel_load
    jmp _start

kernel_load:
    /* es:bx = staging position, cx = CHS, si = sectors left */
    mov $STAGESEG, %ax
    mov %ax, %es
    xor %bx, %bx
    mov (drive), %dl
    xor %dh, %dh
    mov $KERNEL_LBA + 1, %cx
    mov (kernel_sectors), %si

sector_loop:
    test %si, %si
    jz relocate

    mov $0x0201, %ax           /* ah=02 read, al=1 sector */
    int $0x13
    pushf                      /* read CF before anything can touch it */
    pop %ax
    test $1, %al
    jnz read_failed

    dec %si
    add $512, %bx              /* next slot in the staging buffer */
    jnc advance_chs
    mov %es, %ax               /* 64KB filled: next segment */
    add $0x1000, %ax
    mov %ax, %es
    xor %bx, %bx

advance_chs:
    /* next sector: CL wraps 18 -> 1, DH toggles 0 -> 1 -> 0, and each
       head wrap bumps the cylinder */
    inc %cl
    cmp $SECTORS_PER_TRACK + 1, %cl
    jb sector_loop
    mov $1, %cl
    xor $1, %dh
    cmp $2, %dh
    jb sector_loop
    mov $0, %dh
    inc %ch
    jmp sector_loop

relocate:
    /* kernel_sectors*512 bytes: STAGESEG:0 -> SYSSEG:SYSOFF */
    mov (kernel_sectors), %ecx
    shl $9, %ecx
    mov $STAGESEG, %ax
    mov %ax, %ds
    xor %si, %si
    mov $SYSSEG, %ax
    mov %ax, %es
    mov $SYSOFF, %di
    cld
    rep movsb

    mov $SETUPSEG, %ax
    mov %ax, %ds
    mov $0x0003, %ax
    mov %ax, %fs
    ljmp $SETUPSEG, $0x0000

read_failed:
    /* Never hand control to memory that was not loaded. */
    mov $0x0E, %ah
    mov $'!', %al
    int $0x10
    cli
1:  hlt
    jmp 1b

/* al = byte -> two hex digits on the debug port.
   Keep the value in bl (ah gets clobbered by the digit writer) and clear
   ah before the second digit, or both digits print as the high nibble. */
phex:
    mov %al, %bl
    shr $4, %al
    xor %ah, %ah
    call pdig
    mov %bl, %al
    and $0x0F, %al
    xor %ah, %ah
    call pdig
    ret

pdig:
    cmp $10, %al
    jb 1f
    add $('A' - 10), %al
    jmp 2f
1:  add $'0', %al
2:  out %al, $0xE9
    ret

drive:          .byte 0

.org 0x1F0
setup_sectors:  .word SETUP_SECTORS
kernel_sectors: .word 0
.org 0x1FE
.word 0xAA55
