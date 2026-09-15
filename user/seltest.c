/* seltest — sleep() and select() (B5.7).
 *
 * Before this, the kernel had no way to say "wake me later": alarm()
 * wakes a task by delivering a signal, and a timeout that fires a signal
 * is not a timeout.  sleep() and select() are both built on the same
 * per-task deadline that do_timer() checks.
 *
 * Checked here:
 *   1. sleep(2) really sleeps about two seconds and reports 0 (nothing
 *      left to sleep);
 *   2. select() on the console with a one second timeout and nobody
 *      typing returns 0 instead of hanging or reporting a bogus fd;
 *   3. the console is always writable, so select() on fd 1 returns 1
 *      immediately even with no timeout set;
 *   4. a signal cuts sleep() short, the handler runs, and sleep() reports
 *      the seconds it did not sleep (POSIX semantics).
 *
 * Build: make prog NAME=seltest   (then: exec /bin/seltest)
 */

#include "lib.h"

static volatile int alarm_hits = 0;

static void on_alarm(int sig)
{
    alarm_hits++;
}

static int fail(const char *what)
{
    printf("seltest: FAIL %s\n", what);
    return 1;
}

int main(int argc, char *argv[])
{
    unsigned long rfds, wfds, timeout;
    unsigned long t0, t1;
    int r;

    printf("seltest: sleep() and select() (B5.7)\n");

    /* 1. sleep(2): about two seconds, nothing left over */
    t0 = time(NULL);
    r = sleep(2);
    t1 = time(NULL);
    printf("seltest: sleep(2) -> %d, clock %lu -> %lu\n", r, t0, t1);
    if (r != 0)
        return fail("sleep(2) reported time left over");
    if (t1 - t0 < 2)
        return fail("sleep(2) returned too early");

    /* 2. select() on fd 0 with a 1s timeout: nobody is typing, so it must
       time out and clear the set */
    rfds = 1UL;
    timeout = 100;                      /* ticks; HZ = 100 */
    t0 = time(NULL);
    r = select(1, &rfds, NULL, NULL, &timeout);
    t1 = time(NULL);
    printf("seltest: select(fd0, 1s) -> %d, rfds=%lu, %lu s\n",
           r, rfds, t1 - t0);
    if (r != 0)
        return fail("select() did not report a clean timeout");
    if (rfds != 0)
        return fail("select() left fd 0 marked ready after a timeout");

    /* 3. the console is always writable: no timeout, must not block */
    wfds = 1UL << 1;
    timeout = 0;
    r = select(2, NULL, &wfds, NULL, &timeout);
    printf("seltest: select(fd1, no timeout) -> %d, wfds=%lu\n", r, wfds);
    if (r != 1 || wfds != (1UL << 1))
        return fail("fd 1 was not reported writable");

    /* 4. a signal must cut sleep() short and hand back the time left */
    signal(SIGALRM, (unsigned long)on_alarm);
    alarm(1);
    t0 = time(NULL);
    r = sleep(10);
    t1 = time(NULL);
    printf("seltest: sleep(10) with alarm(1) -> %d left, hits=%d, %lu s\n",
           r, alarm_hits, t1 - t0);
    if (alarm_hits != 1)
        return fail("the alarm handler did not run");
    if (r <= 0 || r > 10)
        return fail("sleep() did not report the seconds left");
    if (t1 - t0 > 3)
        return fail("sleep() did not return when the signal arrived");

    printf("seltest: PASS\n");
    return 0;
}
