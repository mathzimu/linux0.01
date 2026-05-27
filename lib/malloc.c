#include <linux/kernel.h>
#include <linux/mm.h>

extern unsigned long _end;

static unsigned long heap_end = 0;

void *malloc(unsigned long size)
{
    void *addr;

    if (heap_end == 0) {
        heap_end = (unsigned long)&_end + 0x4000;
    }

    size = (size + 3) & ~3;
    addr = (void *)heap_end;
    heap_end += size;

    return addr;
}

void free(void *addr)
{
}
