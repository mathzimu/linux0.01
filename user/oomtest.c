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
 * The parent deliberately does *not* allocate: whoever faults when the
 * pool is empty is the one the kernel kills, so a parent that ate memory
 * too would be the victim and could never report the outcome.
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
#define HOLD_SECS 10

static volatile int times_up = 0;

static void on_alarm(int sig)
{
    times_up = 1;
}

int main(void)
{
    int kids = 0, i, pid, reaped;
    unsigned char *p;
    unsigned long st = 0;
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

    /* Now wait for exhaustion to do its work — and ask for nothing
       ourselves.  This kernel's answer to a fault it cannot satisfy is to
       kill the process that faulted, and the children have already asked
       for more than the machine can hold (10 x 192 = 1920 pages against
       ~695 frames plus 512 swap slots), so a child is guaranteed to be
       the victim.  The parent used to fill a chunk of its own here, which
       made *it* the faulting process once the pool was gone: it was
       killed, 'done' never printed, and the scenario failed even though
       the kernel had done exactly the right thing.  Keep the parent out
       of the pool and it survives to report. */
    signal(SIGALRM, (unsigned long)on_alarm);
    alarm(HOLD_SECS);
    while (!times_up)
        pause();                       /* the OOM killer runs meanwhile */

    /* Whoever died is a zombie now: reap and report. */
    reaped = 0;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        reaped++;
        printf("oomtest: reaped child, status=%lu (139 = killed)\n", st);
    }
    printf("oomtest: %d children still holding, %d reaped\n", kids - reaped,
           reaped);

    printf("oomtest: done\n");
    return 0;
}
