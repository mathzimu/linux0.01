/* oomdiag — temporary diagnostic for the oom scenario (not a regression
 * test; it prints from *inside* the child, which oomtest does not).
 *
 * Question: the oom case reports "child N holding 768KB" for ten
 * children, yet memstat afterwards shows almost no page faults, no
 * evictions, no swap-outs and 447 free pages.  Either the children never
 * ran their fill loop, or their pages never became private.
 */
#include "lib.h"

#define KID_BYTES (768 * 1024)
#define MAX_KIDS  3

int main(int argc, char *argv[])
{
    int i, pid, kids = 0;
    unsigned long st = 0;
    unsigned char *p;
    unsigned long j;

    for (i = 0; i < MAX_KIDS; i++) {
        pid = fork();
        if (pid < 0) {
            printf("diag: fork %d failed\n", i);
            break;
        }
        if (pid == 0) {
            p = (unsigned char *)malloc(KID_BYTES);
            printf("diag: child malloc(%d) -> %s\n", KID_BYTES,
                   p ? "ok" : "NULL");
            if (!p)
                exit(1);
            for (j = 0; j < KID_BYTES; j += 4096)
                p[j] = 0xAA;
            printf("diag: child filled, first=%d last=%d\n",
                   p[0], p[KID_BYTES - 4096]);
            exit(7);
        }
        kids++;
    }

    printf("diag: forked %d children\n", kids);
    for (i = 0; i < kids; i++) {
        int r = waitpid(-1, &st, 0);
        printf("diag: reaped r=%d status=%lu\n", r, st);
    }
    printf("diag: done\n");
    return 0;
}
