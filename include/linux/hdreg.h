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

int hd_read_sectors(unsigned int lba, unsigned int nsects, char *buf);
void hd_out(unsigned int drive, unsigned int nsect,
            unsigned int sect, unsigned int head,
            unsigned int cyl, unsigned int cmd);

#endif
