#include <linux/kernel.h>
#include <asm/io.h>

/* Minimal COM1 (16550) serial driver: mirror of the console output.
   Lets QEMU capture exact kernel text via `-serial file:...` — the
   primary verification channel for headless tests. */

#define COM1 0x3F8

void serial_init(void)
{
    outb(0x00, COM1 + 1);      /* disable UART interrupts */
    outb(0x80, COM1 + 3);      /* DLAB on */
    outb(0x01, COM1 + 0);      /* divisor low: 115200 baud */
    outb(0x00, COM1 + 1);      /* divisor high */
    outb(0x03, COM1 + 3);      /* 8N1, DLAB off */
    outb(0x00, COM1 + 4);      /* no flow control */
}

void serial_putc(char c)
{
    int tries = 100000;

    while (tries-- && !(inb(COM1 + 5) & 0x20))   /* wait THR empty */
        ;
    outb(c, COM1);
}
