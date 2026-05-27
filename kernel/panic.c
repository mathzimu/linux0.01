#include <linux/kernel.h>
#include <asm/system.h>

void panic(const char *msg)
{
    cli();

    char *video = (char *)0xB8000;
    const char *p = "KERNEL PANIC: ";
    while (*p) {
        *video++ = *p++;
        *video++ = 0x07;
    }
    p = msg;
    while (*p) {
        *video++ = *p++;
        *video++ = 0x07;
    }

    while (1) {
        __asm__ volatile("hlt");
    }
}
