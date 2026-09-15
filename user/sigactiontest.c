/* sigactiontest — persistent handlers and sa_mask (B5 step 3).
 *
 * `signal()` resets a handler to SIG_DFL before it runs, so a program has
 * to re-arm it inside the handler (see user/sigblock.c, which does).  The
 * handler installed with sigaction() stays installed, and the kernel
 * blocks the signal being handled — plus whatever sa_mask names — for as
 * long as the handler runs, which is what stops a handler from recursing
 * into itself.
 *
 * The program checks all three:
 *
 *   1. a signal raised at itself from inside its own handler stays
 *      pending instead of re-entering the handler;
 *   2. a signal named in sa_mask is held off the same way;
 *   3. after the handler returns, the pending signals are delivered (the
 *      first one re-enters the handler) and the mask is back to normal;
 *   4. the handler is still installed afterwards — no re-arming needed,
 *      which is the whole difference from signal().
 *
 * Build: make prog NAME=sigactiontest  (then: exec /bin/sigactiontest)
 */

#include "lib.h"

static volatile int usr1_hits = 0;
static volatile int usr2_hits = 0;
static volatile int usr2_inside_usr1 = -1;   /* usr2_hits seen in on_usr1 */
static volatile int raised_from_handler = 0;

static void on_usr2(int sig)
{
    usr2_hits++;
}

static void on_usr1(int sig)
{
    usr1_hits++;

    if (!raised_from_handler) {
        raised_from_handler = 1;
        printf("sigaction: in handler: raising SIGUSR1 (self) and SIGUSR2\n");
        kill(getpid(), SIGUSR1);         /* must only become pending */
        kill(getpid(), SIGUSR2);         /* must only become pending */
        usr2_inside_usr1 = usr2_hits;    /* must still be 0 here */
        printf("sigaction: in handler: usr2_hits=%d (want 0)\n",
               usr2_inside_usr1);
    }
}

static int fail(const char *what)
{
    printf("sigaction: FAIL %s\n", what);
    return 1;
}

int main(int argc, char *argv[])
{
    struct sigaction sa, old;

    printf("sigaction: persistent handlers and sa_mask (B5 step 3)\n");

    sa.sa_handler = (unsigned long)on_usr2;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    if (sigaction(SIGUSR2, (unsigned long *)&sa, NULL) != 0)
        return fail("sigaction(SIGUSR2) returned non-zero");

    sa.sa_handler = (unsigned long)on_usr1;
    sa.sa_mask = 1UL << SIGUSR2;         /* blocked while on_usr1 runs */
    sa.sa_flags = SA_RESTART;
    old.sa_handler = 12345;              /* poison */
    if (sigaction(SIGUSR1, (unsigned long *)&sa, (unsigned long *)&old) != 0)
        return fail("sigaction(SIGUSR1) returned non-zero");
    printf("sigaction: SIGUSR1 installed, previous handler=%lu (want 0)\n",
           old.sa_handler);
    if (old.sa_handler != SIG_DFL)
        return fail("the previous action was not reported");

    /* One raise: the handler runs, raises both signals at itself (they
       stay pending), returns, and then they are delivered — so this call
       ends with the handler having run twice and SIGUSR2 once. */
    kill(getpid(), SIGUSR1);
    printf("sigaction: after one raise: usr1=%d usr2=%d\n",
           usr1_hits, usr2_hits);
    if (usr1_hits != 2)
        return fail("the self-raised SIGUSR1 was not held until the handler returned");
    if (usr2_inside_usr1 != 0)
        return fail("sa_mask did not hold SIGUSR2 off during the handler");
    if (usr2_hits != 1)
        return fail("the masked SIGUSR2 was not delivered after the handler");

    /* No re-arming: sigaction() handlers survive their own execution, so
       this raise must reach the handler again.  (With signal() semantics
       the disposition would be SIG_DFL by now and nothing would happen:
       SIGUSR1's default action is to ignore.) */
    kill(getpid(), SIGUSR1);
    printf("sigaction: after a second raise: usr1=%d (want 3)\n", usr1_hits);
    if (usr1_hits != 3)
        return fail("the handler was reset after running (not persistent)");

    /* Restoring SIG_DFL through sigaction() must take the persistence with
       it: a further raise is then ignored. */
    sa.sa_handler = SIG_DFL;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, (unsigned long *)&sa, NULL) != 0)
        return fail("sigaction(SIGUSR1, SIG_DFL) returned non-zero");
    kill(getpid(), SIGUSR1);
    printf("sigaction: after SIG_DFL + raise: usr1=%d (want 3)\n", usr1_hits);
    if (usr1_hits != 3)
        return fail("SIG_DFL did not take effect");

    printf("sigaction: PASS\n");
    return 0;
}
