/* oomtest — M3 regression: running out of physical memory is a normal
 * condition, not a kernel panic.
 *
 * Before M3 the frame allocator had a hard ceiling and a page handed out
 * past it panicked the whole machine ("page allocator reached the user
 * program image").  Now every user page comes from the same pool as task
 * pages and pipes, so exhaustion is expected: the faulting task is killed
 * (its parent can reap it) and fork() reports failure.  The kernel itself
 * must stay alive — this program forks children that each hold on to a
 * few hundred private pages until the 16MB pool is gone, then reports.
 */
#include "lib.h"

#define KID_BYTES (768 * 1024)
#define MAX_KIDS  40

int main(void)
{
    int kids = 0, i, pid, status;
    unsigned char *p;
    unsigned long j;

    for (i = 0; i < MAX_KIDS; i++) {
        pid = fork();
        if (pid < 0)
            break;                      /* out of task slots or memory */

        if (pid == 0) {
            /* Touch the whole allocation so every page really exists
               (first touch: demand paging, and each written page is a
               private copy because it is shared with the parent). */
            p = (unsigned char *)malloc(KID_BYTES);
            if (!p)
                exit(1);
            for (j = 0; j < KID_BYTES; j += 4096)
                p[j] = (unsigned char)j;

            /* Hold it: this is what consumes the pool. */
            for (;;)
                pause();
        }
        kids++;
    }

    printf("oomtest: %d children are holding memory\n", kids);

    /* Try to allocate more than can possibly be left: the fault handler
       runs out of pages and kills the task doing the touching, which is
       this one.  Expect the kernel's "out of memory" message and then
       SIGSEGV's exit status (139) for this process. */
    p = (unsigned char *)malloc(KID_BYTES);
    if (p) {
        for (j = 0; j < KID_BYTES; j += 4096)
            p[j] = 1;
        printf("oomtest: UNEXPECTED - %dKB still allocatable with %d kids\n",
               KID_BYTES / 1024, kids);
    }

    printf("oomtest: done\n");
    (void)status;
    return 0;
}
