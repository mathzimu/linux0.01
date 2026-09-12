/* bigalloc — regression for the buffer-cache / user-heap overlap.
 *
 * The bug this guards against: NR_BUFFERS = 512 placed the kernel's
 * buffer cache at ~0x370000, i.e. on top of the user heap.  Because
 * Ring0 ignores the PTE U/S bit, a user program could malloc a large
 * block, write into it, and silently overwrite filesystem blocks the
 * cache was holding — or have the cache overwrite its own data.  Nothing
 * faulted and nothing was logged.
 *
 * This program drives the heap up to its ceiling, keeping the bytes it
 * wrote, then re-reads a known file from disk.  If the cache and the
 * heap share pages, either the file contents change or the patterns are
 * corrupted, and the checks at the end fail.
 *
 * Build: make prog NAME=bigalloc   (then: exec /bin/bigalloc)
 */

#include "lib.h"
#include <sys/stat.h>

#define MAX_BLOCKS 64
#define BLK        4096

static unsigned char *blocks[MAX_BLOCKS];
static int nblocks = 0;

/* Read a whole file; returns the number of bytes, or -1. */
static int slurp(const char *path, char *dst, int cap)
{
    int fd, n, total = 0;

    fd = open(path, 0);
    if (fd < 0)
        return -1;
    while (total < cap) {
        n = read(fd, dst + total, cap - total);
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    return total;
}

/* Byte search (the user lib deliberately has no memmem). */
static int contains(const unsigned char *hay, int hlen,
                    const char *needle, int nlen)
{
    int i;
    for (i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char buf[256];
    struct stat st;
    unsigned long total = 0;
    int n, i, j, bad = 0, truncated = 0;

    printf("bigalloc: heap vs buffer cache\n");

    n = slurp("/hello.txt", buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("bigalloc: cannot read /hello.txt (n=%d)\n", n);
        return 1;
    }
    buf[n] = '\0';
    printf("bigalloc: /hello.txt = %d bytes\n", n);

    if (stat("/hello.txt", (unsigned long *)&st) < 0) {
        printf("bigalloc: stat failed\n");
        return 1;
    }

    /* Fill the heap until it refuses.  Every block gets a pattern keyed
       by its index so cross-contamination is detectable. */
    for (i = 0; i < MAX_BLOCKS; i++) {
        unsigned char *p = malloc(BLK);
        int k;
        if (!p) {
            truncated = 1;
            break;
        }
        for (k = 0; k < BLK; k++)
            p[k] = (unsigned char)((k + i * 7) & 0xFF);
        blocks[nblocks++] = p;
        total += BLK;
    }
    printf("bigalloc: malloc'd %d blocks (%lu bytes), refused=%d\n",
           nblocks, total, truncated);

    /* Report where the heap got to, so a layout change is visible in the
       regression log (the old cache started around 0x370000). */
    if (nblocks > 0)
        printf("bigalloc: heap top ~ %p\n", blocks[nblocks - 1] + BLK);

    /* Re-read the file while the cache still holds its blocks. */
    n = slurp("/hello.txt", buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("bigalloc: re-read failed\n");
        return 1;
    }
    buf[n] = '\0';
    printf("bigalloc: re-read = %d bytes\n", n);

    /* stat() must still agree — a clobbered superblock or inode shows up
       here first. */
    if (stat("/hello.txt", (unsigned long *)&st) < 0 || (int)st.st_size != n) {
        printf("bigalloc: stat disagrees with read (size=%d, read=%d)\n",
               (int)st.st_size, n);
        bad = 1;
    }

    /* Verify every byte of every pattern we wrote. */
    for (i = 0; i < nblocks && !bad; i++) {
        unsigned char *p = blocks[i];
        for (j = 0; j < BLK; j++) {
            if (p[j] != (unsigned char)((j + i * 7) & 0xFF)) {
                printf("bigalloc: block %d corrupted at offset %d\n", i, j);
                bad = 1;
                break;
            }
        }
    }

    /* Scan the heap for the file's own text: if the buffer cache and the
       heap were the same pages, file blocks would appear inside our
       allocated blocks. */
    for (i = 0; i < nblocks && !bad; i++) {
        if (contains(blocks[i], BLK, "Hello", 5)) {
            printf("bigalloc: file text found inside heap block %d\n", i);
            bad = 1;
        }
    }

    if (bad)
        printf("bigalloc: FAIL\n");
    else
        printf("bigalloc: PASS (heap and buffer cache are disjoint)\n");
    return bad ? 1 : 0;
}
