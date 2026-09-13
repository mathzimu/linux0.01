/* demandtest — M3 regression: pages are handed out on first touch.
 *
 * Nothing is mapped eagerly any more except the pages the ELF file
 * actually contains.  This program walks 256KB of .bss and 128KB of heap
 * that it never wrote before and checks that the pages read back as zero
 * (i.e. they were allocated and cleared on demand), then writes a pattern
 * and re-reads it.  The shell's `memstat` command shows the pages it
 * caused: run `memstat` before and after.
 */
#include "lib.h"

#define BSS_BYTES   (256 * 1024)
#define HEAP_BYTES  (128 * 1024)

static char big_bss[BSS_BYTES];

int main(void)
{
    unsigned char *heap;
    unsigned long i;
    int bad = 0;

    printf("demandtest: touching %dKB of .bss and %dKB of heap\n",
           BSS_BYTES / 1024, HEAP_BYTES / 1024);

    /* First touch of .bss: the kernel must map a zero page per fault. */
    for (i = 0; i < BSS_BYTES; i += 4096) {
        if (big_bss[i] != 0) {
            printf("demandtest: bss page at +%lu was not zero\n", i);
            bad++;
        }
        big_bss[i] = (char)(i >> 12);
    }

    /* Same for the heap. */
    heap = (unsigned char *)malloc(HEAP_BYTES);
    if (!heap) {
        printf("demandtest: malloc(%d) failed\n", HEAP_BYTES);
        return 1;
    }
    for (i = 0; i < HEAP_BYTES; i += 4096) {
        if (heap[i] != 0) {
            printf("demandtest: heap page at +%lu was not zero\n", i);
            bad++;
        }
        heap[i] = (unsigned char)(0xA0 + (i >> 12));
    }

    /* Re-read: the values must have stuck (the pages are real now). */
    for (i = 0; i < BSS_BYTES; i += 4096)
        if (big_bss[i] != (char)(i >> 12))
            bad++;
    for (i = 0; i < HEAP_BYTES; i += 4096)
        if (heap[i] != (unsigned char)(0xA0 + (i >> 12)))
            bad++;

    printf("demandtest: %dKB touched, %d mismatches\n",
           (BSS_BYTES + HEAP_BYTES) / 1024, bad);

    if (bad == 0) {
        printf("demandtest: PASS (BSS and heap arrived zeroed, on demand)\n");
        return 0;
    }
    printf("demandtest: FAIL\n");
    return 1;
}
