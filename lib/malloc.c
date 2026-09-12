#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memmap.h>

/* Cheap non-page kernel heap: a bump pointer that lives in the window
 * reserved for it by include/memlayout.h, between the kernel image and
 * the page-allocator pool.  It is deliberately trivial — the kernel
 * allocates almost everything in pages — but it must never grow into
 * the pool (get_free_page's territory) or into the user program image
 * beyond it. */

static unsigned long heap_end = 0;

void *malloc(unsigned long size)
{
    void *addr;

    if (heap_end == 0) {
        heap_end = KERNEL_HEAP_START;
    }

    size = (size + 7) & ~7;

    if (heap_end + size > KERNEL_HEAP_END)
        return (void *)0;

    addr = (void *)heap_end;
    heap_end += size;

    return addr;
}

void free(void *addr)
{
}
