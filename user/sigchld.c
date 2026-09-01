/* SIGCHLD 语义演示：
   1. signal(SIGCHLD, SIG_IGN)：waitpid 立即返回 -1（无 waitable 子，
      ECHILD）；子进程退出后由调度器自动回收，不留僵尸
   2. signal(SIGCHLD, SIG_DFL)：子进程成为僵尸，父 waitpid(-1) 回收
   3. waitpid(-1, WNOHANG)：子进程存活时返回 0（非阻塞），退出后可回收 */

#include "lib.h"

static void wait_ticks(unsigned long n)
{
    unsigned long t0 = (unsigned long)time(NULL);
    while ((unsigned long)time(NULL) - t0 < n)
        ;
}

int main(void)
{
    int pid, r;
    unsigned long st;

    /* ---- phase 1: SIGCHLD ignored -> nothing waitable, auto-reap ---- */
    printf("== phase 1: signal(SIGCHLD, SIG_IGN) ==\n");
    signal(SIGCHLD, SIG_IGN);
    pid = fork();
    if (pid == 0) {
        printf("    child (pid %d) exiting with 3\n", getpid());
        return 3;
    }
    printf("    parent forked child pid=%d\n", pid);
    r = waitpid(-1, &st, WNOHANG);
    printf("    waitpid(-1, WNOHANG) = %d (expect -1: ignored, ECHILD)\n", r);
    wait_ticks(2);                       /* child exits; scheduler reaps it */
    r = waitpid(-1, &st, WNOHANG);
    printf("    after child exit: waitpid = %d (expect -1: no zombie left)\n", r);
    signal(SIGCHLD, SIG_DFL);

    /* ---- phase 2: default -> zombie, reaped by waitpid(-1) ---- */
    printf("== phase 2: signal(SIGCHLD, SIG_DFL) ==\n");
    pid = fork();
    if (pid == 0) {
        printf("    child (pid %d) exiting with 7\n", getpid());
        return 7;
    }
    printf("    parent waiting for child pid=%d\n", pid);
    r = waitpid(-1, &st, 0);             /* blocking, any child */
    printf("    waitpid(-1) reaped pid=%d exit_code=%lu\n", r, st);

    /* ---- phase 3: WNOHANG returns 0 while child still runs ---- */
    printf("== phase 3: waitpid(-1, WNOHANG) ==\n");
    pid = fork();
    if (pid == 0) {
        wait_ticks(1);                   /* stay alive ~1 second */
        printf("    child (pid %d) exiting with 9\n", getpid());
        return 9;
    }
    printf("    parent forked child pid=%d\n", pid);
    r = waitpid(-1, &st, WNOHANG);
    printf("    waitpid(-1, WNOHANG) = %d (expect 0: child alive)\n", r);
    r = waitpid(-1, &st, 0);             /* block until it exits */
    printf("    waitpid(-1) reaped pid=%d exit_code=%lu\n", r, st);
    return 0;
}
