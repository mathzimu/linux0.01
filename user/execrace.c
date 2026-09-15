/* execrace — concurrent execve() of the SAME program.
 *
 * The reported failure is "the second concurrent exec of the same binary
 * fails" (`cat file | cat` in the Ring3 shell).  A shell pipeline only
 * reaches that state by timing luck: the plan is to make it deterministic
 * instead.  This program forks N children *before* waiting for any of
 * them, and every child execve()s the same absolute path, so N execs of
 * the same inode are in flight at once.
 *
 * The child prints a marker only if its execve() *fails*, so a successful
 * run shows no per-child error line at all; the parent then reports the
 * exit code of every child and the final verdict.  A single failed exec
 * shows up as a nonzero exit code (100 + child index), which makes the
 * run fail loudly instead of quietly.
 *
 * Usage:  exec /bin/execrace [children] [path] [arg]
 *         defaults: 4 children, /bin/cat, /hello.txt
 *
 * Build:  make user/execrace.elf  (the scenario also injects /bin/cat)
 */

#include "lib.h"

#define MAX_KIDS 8

int main(int argc, char *argv[])
{
    char *kid_argv[4];
    const char *path;
    int n = 4, i, kids = 0, bad = 0;
    int pid, r;
    unsigned long code;

    if (argc > 1)
        n = atoi(argv[1]);
    if (n < 1)
        n = 1;
    if (n > MAX_KIDS)
        n = MAX_KIDS;
    path = (argc > 2) ? argv[2] : "/bin/cat";

    kid_argv[0] = (char *)path;
    kid_argv[1] = (argc > 3) ? argv[3] : "/hello.txt";
    kid_argv[2] = NULL;

    printf("execrace: %d children will exec the same %s\n", n, path);

    /* Fork first, wait later: that is what puts several execve()s of the
       same inode in flight at the same time. */
    for (i = 0; i < n; i++) {
        pid = fork();
        if (pid < 0) {
            printf("execrace: fork %d failed\n", i);
            break;
        }
        if (pid == 0) {
            execve(path, kid_argv, NULL);
            /* Only reached when execve() failed — otherwise this image is
               gone.  Exit with a distinctive code so the parent counts it
               as a failure. */
            printf("execrace: child %d could not execute %s\n", i, path);
            exit(100 + i);
        }
        kids++;
    }

    printf("execrace: forked %d children\n", kids);

    for (i = 0; i < kids; i++) {
        code = 0;
        r = waitpid(-1, &code, 0);
        if (r < 0) {
            printf("execrace: waitpid failed after %d reaps\n", i);
            bad++;
            break;
        }
        printf("execrace: child pid=%d exit_code=%lu\n", r, code);
        if (code != 0)
            bad++;
    }

    printf("execrace: %d/%d children exec'd and exited cleanly\n",
           kids - bad, kids);
    if (bad || kids != n) {
        printf("execrace: FAIL (%d bad of %d forked, wanted %d)\n",
               bad, kids, n);
        return 1;
    }

    printf("execrace: PASS\n");
    return 0;
}
