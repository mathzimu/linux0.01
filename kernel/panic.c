#include <linux/kernel.h>
#include <asm/system.h>

void panic(const char *msg)
{
    cli();

    char *video = (char *)0xB8000;
    char *video_end = video + 80 * 25 * 2;
    const char *p = "KERNEL PANIC: ";
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
