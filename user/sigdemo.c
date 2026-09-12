/* sigdemo — custom signal handlers + sigreturn (M2-1).
 *
 * The kernel enters a handler by rewriting the iret frame; when the
 * handler returns it lands on a stub that issues the sigreturn syscall,
 * which must resume the interrupted code exactly where it left off.
 * This program checks that the handler actually runs, that it receives
 * the signal number as its argument, that execution continues after the
 * interrupting instruction, and that signal() hands back the previous
 * disposition.
 *
 * Build: make prog NAME=sigdemo   (then: exec /bin/sigdemo)
 */

#include "lib.h"

static volatile int hits = 0;
static volatile int last_sig = 0;

static void on_alarm(int sig)
{
    hits++;
    last_sig = sig;
    printf("sigdemo: handler ran (sig=%d, hits=%d)\n", sig, hits);
}

int main(int argc, char *argv[])
{
    unsigned long prev;
    int i;
    int sum = 0;

    printf("sigdemo: custom signal handlers\n");

    prev = (unsigned long)signal(SIGALRM, (unsigned long)on_alarm);
    printf("sigdemo: signal(SIGALRM) previous = %lu\n", prev);

    alarm(1);

    /* Sum until the alarm fires.  If sigreturn resumed at the right
       instruction the loop simply continues and the sum stays sane. */
    for (i = 0; i < 200000 && hits == 0; i++)
        sum += i;
    printf("sigdemo: after alarm: hits=%d last_sig=%d sum=%d\n",
           hits, last_sig, sum);

    if (hits != 1 || last_sig != SIGALRM) {
        printf("sigdemo: FAIL handler did not run for SIGALRM\n");
        return 1;
    }

    /* signal() resets a handled signal to SIG_DFL before running it
       (classic semantics), so a second SIGALRM must terminate us — do
       not test that here.  Instead check SIG_IGN round-trips. */
    prev = (unsigned long)signal(SIGUSR2, SIG_IGN);
    printf("sigdemo: signal(SIGUSR2, SIG_IGN) previous = %lu\n", prev);
    prev = (unsigned long)signal(SIGUSR2, SIG_DFL);
    printf("sigdemo: signal(SIGUSR2, SIG_DFL) previous = %lu (expect 1)\n", prev);

    if (prev != SIG_IGN) {
        printf("sigdemo: FAIL SIG_IGN was not reported back\n");
        return 1;
    }

    /* SIGKILL must not be catchable or ignorable. */
    if (signal(SIGKILL, (unsigned long)on_alarm) != -1) {
        printf("sigdemo: FAIL SIGKILL was accepted\n");
        return 1;
    }
    printf("sigdemo: SIGKILL rejected as expected\n");

    printf("sigdemo: PASS\n");
    return 0;
}
