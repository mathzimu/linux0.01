/* oomtest — M3 regression: running out of physical memory is a normal
 * condition, not a kernel panic.
 *
 * Before M3 the frame allocator had a hard ceiling and a page handed out
 * past it panicked the whole machine ("page allocator reached the user
 * program image").  Now every user page comes from the same pool as task
 * pages and pipes, so exhaustion is expected: the faulting task is killed
 * (its parent can reap it) and fork() reports failure.  The kernel itself
 * must stay alive — this program forks children that each hold on to a
 * few hundred private pages until the pool is gone, then reports.
 *
 * It runs on a small machine on purpose: the regression scenario boots
 * with 4MB (~695 free frames), so ten children holding 768KB each already
 * overflow it.  The count has to clear memory *and* swap: since B4 the
 * reclaimer writes dirty pages to the swap area, so a workload that
 * merely outgrows RAM now survives instead of being killed (695 frames +
 * 512 swap slots = ~1207 pages, and 10 x 192 = 1920 pages are asked for).
 */
#include "lib.h"

#define KID_BYTES (768 * 1024)
#define MAX_KIDS  10
#define MAX_CHUNKS 8

int main(void)
{
    int kids = 0, i, pid, held;
    unsigned char *p;
    unsigned long j;

    for (i = 0; i < MAX_KIDS; i++) {
        pid = fork();
        if (pid < 0)
            break;                      /* out of task slots or memory */

        if (pid == 0) {
            /* Touch the whole allocation so every page really exists
               (first touch: demand paging, and each written page is a
               private copy because it is shared with the parent).
               The pattern is deliberately NON-ZERO: a page that is still
               all zeros is reclaimable by the eviction path added in B3
               (see user/evicttest.c), and then this test would never
               reach exhaustion.  Writing 0xAA makes every page a dirty
               private page, which has no backing store and cannot be
               reclaimed — which is the point of *this* test. */
            p = (unsigned char *)malloc(KID_BYTES);
            if (!p)
                exit(1);
            for (j = 0; j < KID_BYTES; j += 4096)
                p[j] = 0xAA;

            /* Hold it: this is what consumes the pool. */
            for (;;)
                pause();
        }
        kids++;

        /* Progress, one line per child: the test harness stops a run
           after ~1.5s of silence, and the memory-touching below is long
           enough to look like silence (B3 made it longer still, because
           image pages are no longer pre-loaded). */
        printf("oomtest: child %d holding %dKB\n", kids, KID_BYTES / 1024);
    }

    printf("oomtest: %d children are holding memory\n", kids);

    /* Now eat whatever is left, in non-evictable chunks, until an
       allocation cannot be satisfied at all.  This is the deterministic
       part: 64 chunks is 48MB on a 16MB machine, so the pool *must* run
       out and the fault that finds no frame is this process's own — the
       kernel kills it (exit 139) and everything else keeps running. */
    held = 0;
    for (i = 0; i < MAX_CHUNKS; i++) {
        p = (unsigned char *)malloc(KID_BYTES);
        if (!p) {
            printf("oomtest: malloc refused after %d chunks\n", held);
            break;
        }
        for (j = 0; j < KID_BYTES; j += 4096)
            p[j] = 0x55;
        held++;
        printf("oomtest: parent holds %dKB, %d children still alive\n",
               held * (KID_BYTES / 1024), kids);
    }

    printf("oomtest: done\n");
    return 0;
}
