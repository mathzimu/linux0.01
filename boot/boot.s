.code16
.text
.globl _start

.equ SETUP_SECTORS, 4
.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800      /* kernel runs at 0x10000 + 0x800 = 0x10800 */

/* Disk layout written by tools/build (floppy Image) and tools/mkdisk
   (the single-file hard-disk image): LBA 0 = boot sector,
   LBA 1..SETUP_SECTORS = setup, kernel from LBA 1+SETUP_SECTORS.
   CHS LBA = ((C * 2) + H) * 18 + (S - 1), so the kernel begins at
   CHS(0,0,2+SETUP_SECTORS).

   The sector also carries the LBA at which the *root filesystem* starts
   (fs_base_lba at 0x1F4).  On a floppy that is 0: minix.img is a device
   of its own and its filesystem begins at its LBA 0.  On the single-file
   hard-disk image the filesystem sits behind the kernel, and tools/mkdisk
   writes its offset into this very sector - so the image and the kernel
   cannot drift apart.  This loader only forwards the value to setup.s
   (in %ebx), which parks it in the boot parameter block; the kernel reads
   it in main() (BOOT_FS_BASE_ADDR) and hands it to the disk driver.
   See docs/roadmap.md, "单文件自启动镜像".

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

    /* Own stack before the first INT: the hard-disk path below uses
       call/ret, and the stack the BIOS happens to leave behind is not
       something a boot sector should rely on.  It grows down from 0x7C00,
       i.e. into the free memory *below* this sector, never into it. */
    cli
    xor %ax, %ax
    mov %ax, %ss
    mov $0x7C00, %sp
    sti

    mov $0x2401, %ax
    int $0x15
    mov $0x13, %ah
    mov $0x01, %al
    int $0x10
    mov $0x03, %ah
    xor %bh, %bh
    int $0x10

    /* Which medium are we on?  A hard disk (dl >= 0x80) has no usable
       CHS geometry here: this kernel's IDE driver addresses the disk as
       16 heads / 63 sectors per track, not the floppy's 2/18, and the
       BIOS's translated geometry is its own business.  The EDD LBA
       interface (INT 13h AH=42h) does not care about geometry at all, so
       disks are read that way.  Floppies below keep the original CHS
       path, untouched: it is what every regression scenario boots. */
    mov (drive), %al
    test $0x80, %al
    jnz disk_boot

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

/* --- hard disk (dl >= 0x80): EDD LBA reads -------------------------
   Same two steps as the floppy path - setup first, then the kernel - but
   through INT 13h AH=42h, which takes an absolute LBA and a buffer
   descriptor instead of cylinder/head/sector.  The CHS code above is
   left exactly as it is; this is the only path that changed.

   The 512-byte size of the sector is what makes the bookkeeping below
   cheap: both load addresses (0x10000 for setup, 0x10800 for the kernel)
   are multiples of 16, so the DAP's offset field is always zero and only
   its segment has to move - by 512/16 paragraphs per sector.  That is
   why this path advances (dap_seg) instead of carrying the 32-bit linear
   destination the CHS path keeps in (dest_lin).  The segment stays a
   16-bit one comfortably: KERNEL_IMAGE_LIMIT (include/linux/memmap.h)
   caps the kernel at 192KB, well inside the low 1MB the staging area
   lives in. */
.equ KERNEL_LIN,    0x10800       /* must stay 16-byte aligned: see above */
.equ KERNEL_SEG,    (KERNEL_LIN >> 4)
.equ SECTOR_PARAS,  (512 / 16)

disk_boot:
    movw $SETUP_SECTORS, (left)
    mov $1, %eax
    mov %eax, (cur_lba)
    movw $SETUPSEG, (dap_seg)      /* 0x1000:0 = linear 0x10000 */
    call edd_read_run

    mov (kernel_sectors), %ax
    mov %ax, (left)
    mov $KERNEL_LBA, %eax
    mov %eax, (cur_lba)
    movw $KERNEL_SEG, (dap_seg)    /* 0x1080:0 = linear 0x10800 */
    call edd_read_run
    jmp kernel_done

/* Read (left) sectors from (cur_lba) into the DAP's buffer, one INT 13h
   call per sector.  One sector per call for the same reason the CHS path
   does it: a multi-sector transfer that reports success while having
   moved fewer sectors leaves whatever happened to be in RAM behind, and a
   short load is nearly impossible to tell apart from a corrupt image. */
edd_read_run:
    cmpw $0, (left)
    je  edd_run_done
    call edd_read_one
    jc  read_failed
    decw (left)
    incl (cur_lba)
    addw $SECTOR_PARAS, (dap_seg)
    jmp edd_read_run

edd_run_done:
    ret

/* One INT 13h AH=42h read (DS:SI -> the disk address packet in this
   sector).  The count, buffer offset and the LBA's high half are
   constants and live in the packet's initialized fields; the segment is
   advanced by the caller and the LBA is the one thing that changes here.
   Every register is reloaded because INT 13h may clobber all of them -
   including the ones holding the loop state, which is exactly why that
   state lives in memory. */
edd_read_one:
    mov (cur_lba), %eax
    mov %eax, (dap_lba)
    mov (drive), %dl
    mov $0x42, %ah
    mov $dap, %si
    int $0x13
    ret

load_kernel:
    mov (kernel_sectors), %ax
    mov %ax, (left)
    mov $KERNEL_LBA, %eax
    mov %eax, (cur_lba)
    mov $KERNEL_LIN, %eax
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
    /* Hand the root-filesystem base LBA to setup.s in %ebx.  It has to be
       read before DS moves off this sector; setup.s parks it in the boot
       parameter block, where main() picks it up.  Zero means "the
       filesystem starts at LBA 0 of the device" - the floppy case. */
    mov (fs_base_lba), %ebx
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
dest_lin:       .long KERNEL_LIN
cyl:            .word 0
head:           .byte 0
sect:           .byte 0
chs_cx:         .word 0
chs_dx:         .word 0
dst_seg:        .word 0
dst_off:        .word 0

/* EDD disk address packet (INT 13h AH=42h): size, reserved, sector
   count, buffer offset, buffer segment, 64-bit LBA.  16 bytes, all of it
   used (the LBA is a qword even at size 0x10).  It must stay inside the
   boot sector: DS:SI points straight at it.  Only the segment and the
   LBA's low half are ever rewritten - the rest are constants, so they
   are initialized here and never touched again. */
dap:            .byte 0x10, 0x00
dap_count:      .word 1
dap_off:        .word 0
dap_seg:        .word 0
dap_lba:        .long 0
dap_lba_hi:     .long 0

.org 0x1F0
setup_sectors:  .word SETUP_SECTORS
kernel_sectors: .word 0
/* Root filesystem base LBA, patched by tools/mkdisk for the single-file
   hard-disk image and left 0 by tools/build for the floppy Image. */
fs_base_lba:    .long 0
.org 0x1FE
.word 0xAA55
