#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/system.h>

void panic(const char *msg)
{
    const char *p;

    cli();

    /* Mirror the message on COM1 as well, so headless tests see it. */
    p = "KERNEL PANIC: ";
    while (*p) {
        serial_putc(*p++);
    }
    p = msg;
    while (*p) {
        serial_putc(*p++);
    }
    serial_putc('\r');
    serial_putc('\n');

    char *video = (char *)0xB8000;
    char *video_end = video + 80 * 25 * 2;
    p = "KERNEL PANIC: ";
    while (*p && video < video_end - 1) {
        *video++ = *p++;
        *video++ = 0x07;
    }
    p = msg;
    while (*p && video < video_end - 1) {
        *video++ = *p++;
        *video++ = 0x07;
    }

    while (1) {
        __asm__ volatile("hlt");
    }
}
