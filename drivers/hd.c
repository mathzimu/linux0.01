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

static int hd_ready(void)
{
    int status;

    if (hd_busy()) return -1;

    status = inb(HD_STATUS);
    if (status & HD_STATUS_ERR) return -1;
    if (!(status & HD_STATUS_DRDY)) return -1;

    return 0;
}

void hd_out(unsigned int drive, unsigned int nsect,
            unsigned int sect, unsigned int head,
            unsigned int cyl, unsigned int cmd)
{
    if (hd_busy()) return;

    outb((drive << 4) | (head & 0xF), HD_CURRENT);
    outb(nsect, HD_NSECTOR);
    outb(sect, HD_SECTOR);
    outb(cyl, HD_LCYL);
    outb(cyl >> 8, HD_HCYL);
    outb(cmd, HD_COMMAND);
}

int hd_request(void)
{
    return 0;
}

void hd_interrupt_handler(void)
{
}


