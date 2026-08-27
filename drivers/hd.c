#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <asm/system.h>

static int hd_busy(void)
{
    int tries = 100000;
    while (tries--) {
        if (!(inb(HD_STATUS) & HD_STATUS_BSY))
            return 0;
    }
    return -1;
}

static int hd_wait_drq(void)
{
    int tries = 100000;
    while (tries--) {
        unsigned char status = inb(HD_STATUS);
        if (status & HD_STATUS_ERR)
            return -1;
        if (!(status & HD_STATUS_BSY) && (status & HD_STATUS_DRQ))
            return 0;
    }
    return -1;
}

void hd_out(unsigned int drive, unsigned int nsect,
            unsigned int sect, unsigned int head,
            unsigned int cyl, unsigned int cmd)
{
    if (hd_busy()) return;

    outb(0xA0 | (drive << 4) | (head & 0x0F), HD_CURRENT);
    outb(nsect, HD_NSECTOR);
    outb(sect, HD_SECTOR);
    outb(cyl & 0xFF, HD_LCYL);
    outb((cyl >> 8) & 0xFF, HD_HCYL);
    outb(cmd, HD_COMMAND);
}

int hd_read_sectors(unsigned int lba, unsigned int nsects, char *buf)
{
    unsigned int cyl, head, sect;
    int j;

    cyl = lba / (16 * 63);
    head = (lba / 63) % 16;
    sect = (lba % 63) + 1;

    if (hd_busy()) return -1;

    hd_out(0, nsects, sect, head, cyl, HD_CMD_READ);

    for (j = 0; j < nsects; j++) {
        if (hd_wait_drq() < 0) return -1;
        insl(HD_DATA, buf + j * 512, 512 / 4);
    }

    return 0;
}

int hd_write_sectors(unsigned int lba, unsigned int nsects, char *buf)
{
    unsigned int cyl, head, sect;
    int j;

    cyl = lba / (16 * 63);
    head = (lba / 63) % 16;
    sect = (lba % 63) + 1;

    if (hd_busy()) return -1;

    hd_out(0, nsects, sect, head, cyl, HD_CMD_WRITE);

    for (j = 0; j < nsects; j++) {
        if (hd_wait_drq() < 0) return -1;
        outsl(HD_DATA, buf + j * 512, 512 / 4);
    }

    /* wait for the drive to finish the command and report status */
    {
        int tries = 100000;
        while (tries--) {
            unsigned char status = inb(HD_STATUS);
            if (!(status & HD_STATUS_BSY)) {
                if (status & HD_STATUS_ERR)
                    return -1;
                return 0;
            }
        }
    }
    return -1;
}

int hd_request(void)
{
    return 0;
}

void hd_interrupt_handler(void)
{
}