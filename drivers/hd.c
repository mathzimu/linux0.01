#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <asm/system.h>

/* ====================================================================
 * Hard disk driver — interrupt driven since B2.
 *
 * It used to be pure polling: hd_wait_drq() spun on port 0x1F7 up to
 * 100000 times, and the whole system waited with it.  setup.s leaves the
 * slave PIC masked (0xFF), IRQ14 was never enabled and
 * hd_interrupt_handler() was dead code that nothing could reach.
 *
 * Now the drive's own interrupt does the waking:
 *
 *   - hd_init() unmasks IRQ14 (bit 6 of the slave PIC);
 *   - a task that issues a command sleeps on hd_wait until the handler
 *     reports that the drive changed state;
 *   - the deadline is checked against jiffies, and schedule() also
 *     returns on the next timer tick, so a *lost* interrupt costs a
 *     timeout rather than a hung machine;
 *   - during boot, though, I/O happens before main() calls sched_init(),
 *     so the timer is not programmed yet and jiffies never advances: a
 *     sleeper could not be timed out.  The wait therefore falls back to
 *     the old bounded poll until sched_ready is set.  (This used to be
 *     decided by hd_irq_enabled(), which is wrong - hd_lock() enables
 *     interrupts itself, so on the first boot read the flag was already
 *     set and the driver slept with no task table and no timer.)
 *
 * Sleeping inside the driver also means two tasks can now be in here at
 * the same time, which the polling version never had to consider: the
 * lock below serialises whole operations (command + data transfer).
 * ==================================================================== */

#define HD_TIMEOUT_TICKS 50        /* ~0.5s at 100Hz */
#define HD_POLL_LIMIT    1000000UL /* spins when interrupts are off */

static struct task_struct *hd_wait = NULL;     /* waiting for the drive */
static struct task_struct *hd_lock_q = NULL;   /* waiting for the lock  */
static volatile int hd_locked = 0;
static volatile int hd_intr = 0;               /* IRQs seen (diagnostics) */
static unsigned long hd_irqs = 0;

/* Is the CPU taking interrupts right now? */
static int hd_irq_enabled(void)
{
    unsigned long flags;

    __asm__ volatile("pushfl; popl %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

void hd_init(void)
{
    unsigned char mask = inb(0xA1);

    outb(mask & ~0x40, 0xA1);          /* IRQ14 = bit 6 of the slave PIC */
    printk("hd: IRQ14 enabled (slave mask 0x%02x -> 0x%02x)\n",
           mask, mask & ~0x40);
}

/* --- lock ---------------------------------------------------------- */

static void hd_lock(void)
{
    while (1) {
        cli();
        if (!hd_locked) {
            hd_locked = 1;
            sti();
            return;
        }
        sti();
        sleep_on(&hd_lock_q);          /* woken by hd_unlock() */
    }
}

static void hd_unlock(void)
{
    hd_locked = 0;
    wake_up(&hd_lock_q);
}

/* --- waiting for the drive ----------------------------------------- */

/* Wait until (status & mask) == value, or the drive reports an error, or
   we time out.  Returns 0 or -1.

   The register semantics matter here: while data is being transferred
   the drive has BSY *clear* and DRQ *set*, so "wait for data" is
   (status & (BSY|DRQ)) == DRQ, not "both set" — the first version of
   this function got that wrong and every read timed out. */
static int hd_wait_bits(unsigned char mask, unsigned char value,
                        const char *what)
{
    unsigned long deadline = jiffies + HD_TIMEOUT_TICKS;
    unsigned long spins = HD_POLL_LIMIT;

    while (1) {
        unsigned char status = inb(HD_STATUS);

        if (status & HD_STATUS_ERR) {
            printk("hd: %s: drive error, status 0x%02x\n", what, status);
            return -1;
        }
        if ((status & mask) == value)
            return 0;

        if (hd_irq_enabled() && sched_ready) {
            if ((long)(jiffies - deadline) >= 0) {
                printk("hd: %s: timeout, status 0x%02x (IRQ14 never came)\n",
                       what, status);
                return -1;
            }
            /* Sleep; IRQ14 (or the timer tick) brings us back. */
            current->state = TASK_INTERRUPTIBLE;
            hd_wait = current;
            schedule();
            current->state = TASK_RUNNING;
            hd_wait = NULL;
        } else {
            /* Boot-time I/O: the timer is not running yet, so nothing
               would ever time this wait out, and the only way to make
               progress is to poll - which is what this driver used to do
               all the time.  Testing the interrupt flag is not enough:
               hd_lock() enables interrupts as soon as it takes the lock,
               so on the very first (boot) read it is already set. */
            if (--spins == 0) {
                printk("hd: %s: timeout while polling, status 0x%02x\n",
                       what, status);
                return -1;
            }
        }
    }
}

/* 0 if the drive is ready to accept a command. */
static int hd_busy(void)
{
    return hd_wait_bits(HD_STATUS_BSY, 0, "ready");
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

/* CHS from a linear block address (16 heads, 63 sectors/track). */
static void hd_geometry(unsigned int lba, unsigned int *cyl,
                        unsigned int *head, unsigned int *sect)
{
    *cyl = lba / (16 * 63);
    *head = (lba / 63) % 16;
    *sect = (lba % 63) + 1;
}

int hd_read_sectors(unsigned int lba, unsigned int nsects, char *buf)
{
    unsigned int cyl, head, sect;
    int j, ret = 0;

    hd_geometry(lba, &cyl, &head, &sect);

    hd_lock();
    if (hd_busy()) {
        hd_unlock();
        return -1;
    }

    hd_out(0, nsects, sect, head, cyl, HD_CMD_READ);

    for (j = 0; j < nsects; j++) {
        if (hd_wait_bits(HD_STATUS_BSY | HD_STATUS_DRQ, HD_STATUS_DRQ,
                         "read data") < 0) {
            ret = -1;
            break;
        }
        insl(HD_DATA, buf + j * 512, 512 / 4);
    }

    hd_unlock();
    return ret;
}

int hd_write_sectors(unsigned int lba, unsigned int nsects, char *buf)
{
    unsigned int cyl, head, sect;
    int j, ret = 0;

    hd_geometry(lba, &cyl, &head, &sect);

    hd_lock();
    if (hd_busy()) {
        hd_unlock();
        return -1;
    }

    hd_out(0, nsects, sect, head, cyl, HD_CMD_WRITE);

    for (j = 0; j < nsects; j++) {
        if (hd_wait_bits(HD_STATUS_BSY | HD_STATUS_DRQ, HD_STATUS_DRQ,
                         "write data") < 0) {
            ret = -1;
            break;
        }
        outsl(HD_DATA, buf + j * 512, 512 / 4);
    }

    /* The last block is only really on the platter once BSY drops. */
    if (ret == 0 && hd_wait_bits(HD_STATUS_BSY, 0, "write complete") < 0)
        ret = -1;

    hd_unlock();
    return ret;
}

int hd_request(void)
{
    return 0;
}

/* IRQ14.  head.s has already sent the EOI (slave then master) before
   calling this, so all that is left is to note it and wake the driver. */
void hd_interrupt_handler(void)
{
    hd_irqs++;
    hd_intr = 1;
    (void)inb(HD_STATUS);              /* acknowledge the drive */

    if (hd_wait)
        wake_up(&hd_wait);
}

/* Diagnostics for the shell's memstat-style reporting. */
unsigned long hd_interrupt_count(void)
{
    return hd_irqs;
}
