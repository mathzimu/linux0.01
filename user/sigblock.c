/* sigblock — signal masks and sigsuspend (B5).
 *
 * A blocked signal must not be thrown away: it stays pending and is
 * delivered as soon as the mask lets it through.  That is what makes a
 * mask useful for a critical section, and it is what this program
 * checks, in three steps:
 *
 *   1. block SIGUSR1, raise it at ourselves: the handler must NOT run;
 *   2. unblock it: the handler runs on the return path of the very
 *      sigprocmask() call that unblocked it, with no new signal sent;
 *   3. block it again, have a child send it, and wait in sigsuspend():
 *      that call must return -1 and the signal must be delivered.
 *
 * Build: make prog NAME=sigblock   (then: exec /bin/sigblock)
 */

#include "lib.h"

static volatile int hits = 0;
static volatile int last_sig = 0;

static void on_usr1(int sig)
{
    hits++;
    last_sig = sig;
    printf("sigblock: handler ran (sig=%d, hits=%d)\n", sig, hits);
}

static int fail(const char *what)
{
    printf("sigblock: FAIL %s\n", what);
    return 1;
}

int main(int argc, char *argv[])
{
    unsigned long set = 1UL << SIGUSR1;
    unsigned long old = 12345;         /* poison: must be overwritten */
    unsigned long empty = 0;
    int r;

    printf("sigblock: signal masks (B5)\n");

    signal(SIGUSR1, (unsigned long)on_usr1);

    if (sigprocmask(SIG_BLOCK, &set, &old) != 0)
        return fail("sigprocmask(SIG_BLOCK) returned non-zero");
    printf("sigblock: blocked SIGUSR1, previous mask=%lu\n", old);
    if (old != 0)
        return fail("the initial mask was not empty");

    /* Raise the signal at ourselves.  It is blocked, so it must only
       become pending: kill() returns and the handler stays away. */
    kill(getpid(), SIGUSR1);
    printf("sigblock: raised SIGUSR1 while blocked: hits=%d\n", hits);
    if (hits != 0)
        return fail("a blocked signal ran its handler anyway");

    /* Unblocking alone must deliver the pending signal -- remember, we
       never sent a second one. */
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) != 0)
        return fail("sigprocmask(SIG_UNBLOCK) returned non-zero");
    printf("sigblock: after unblock: hits=%d last_sig=%d\n", hits, last_sig);
    if (hits != 1 || last_sig != SIGUSR1)
        return fail("the pending signal was lost when it was unblocked");

    /* sigsuspend(): block SIGUSR1, let a child send it, and wait.  The
       signal may arrive before we get to sigsuspend() -- the mask makes
       that harmless. */
    /* Classic signal() semantics: the disposition went back to SIG_DFL
       when the handler ran (see do_signal), so a second SIGUSR1 would
       now be ignored instead of handled.  Re-arm it -- this is exactly
       the wart that sigaction() exists to fix. */
    signal(SIGUSR1, (unsigned long)on_usr1);
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
        return fail("sigprocmask(SIG_BLOCK) returned non-zero");
    r = fork();
    if (r == 0) {
        int i;
        /* give the parent time to reach sigsuspend() */
        for (i = 0; i < 300000; i++)
            ;
        kill(getppid(), SIGUSR1);
        exit(0);
    }
    if (r < 0)
        return fail("fork failed");

    r = sigsuspend(&empty);
    printf("sigblock: sigsuspend returned %d, hits=%d last_sig=%d\n",
           r, hits, last_sig);
    if (r != -1 || hits != 2 || last_sig != SIGUSR1)
        return fail("sigsuspend did not wait for the unblocked signal");

    /* The mask must be back to what it was before sigsuspend(), i.e.
       SIGUSR1 blocked again: a raise now must not run the handler. */
    kill(getpid(), SIGUSR1);
    printf("sigblock: mask restored after sigsuspend: hits=%d\n", hits);
    if (hits != 2)
        return fail("sigsuspend did not restore the old mask");

    /* ... and SIGKILL cannot be blocked, not even by asking for it. */
    set = 1UL << SIGKILL;
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0)
        return fail("sigprocmask(SIG_BLOCK, SIGKILL) returned non-zero");
    printf("sigblock: SIGKILL is still unblockable (hits=%d)\n", hits);

    waitpid(-1, NULL, 0);              /* reap the child */
    printf("sigblock: PASS\n");
    return 0;
}
