#ifndef _HDREG_H
#define _HDREG_H

#define HD_DATA        0x1F0
#define HD_ERROR       0x1F1
#define HD_NSECTOR     0x1F2
#define HD_SECTOR      0x1F3
#define HD_LCYL        0x1F4
#define HD_HCYL        0x1F5
#define HD_CURRENT     0x1F6
#define HD_STATUS      0x1F7
#define HD_COMMAND     0x1F7

#define HD_CMD         0x3F6

#define HD_IRQ         14

#define HD_CMD_READ    0x20
#define HD_CMD_WRITE   0x30
#define HD_CMD_IDENT   0xEC

#define HD_STATUS_BSY  0x80
#define HD_STATUS_DRDY 0x40
#define HD_STATUS_DRQ  0x08
#define HD_STATUS_ERR  0x01

#define MAJOR_NR 3

struct partition {
    unsigned char boot_ind;
    unsigned char head;
    unsigned char sector;
    unsigned char cyl;
    unsigned char sys_ind;
    unsigned char end_head;
    unsigned char end_sector;
    unsigned char end_cyl;
    unsigned long start_sect;
    unsigned long nr_sects;
};

/* LBAs here are relative to the start of the root image, not to the start
   of the device: see the comment on root_lba in drivers/hd.c.  On the
   single-file disk image the filesystem sits behind the kernel, and the
   offset comes from the boot sector via the boot parameter block. */
int hd_read_sectors(unsigned int lba, unsigned int nsects, char *buf);
int hd_write_sectors(unsigned int lba, unsigned int nsects, char *buf);
void hd_out(unsigned int drive, unsigned int nsect,
            unsigned int sect, unsigned int head,
            unsigned int cyl, unsigned int cmd);

/* Install / read the root image's base LBA (main() sets it from
   BOOT_FS_BASE_ADDR before sys_setup() does the first read). */
void hd_set_root_lba(unsigned int lba);
unsigned int hd_root_lba(void);

/* Unmask IRQ14 so the drive's own interrupt can wake a sleeping task
   (drivers/hd.c); called from main() before the first disk access. */
void hd_init(void);

/* How many IRQ14s have been taken — shown by the shell's `memstat`. */
unsigned long hd_interrupt_count(void);

#endif
