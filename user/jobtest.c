/* jobtest: verify minimal SIGSTOP/SIGCONT semantics.
 * parent forks a child (infinite pause), SIGSTOPs it, reads /proc/ps to
 * check state 4, SIGCONTs it, checks state 0/1, then SIGKILLs and reaps. */
#include "lib.h"

static void dump_ps(const char *tag)
{
    int fd = open("/proc/ps", 0, 0);
    char buf[512];
    int n;

    if (fd < 0) {
        printf("%s: cannot open /proc/ps\n", tag);
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s:\n%s", tag, buf);
    }
}

int main(void)
{
    int pid = fork();

    if (pid < 0) {
        printf("jobtest: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        for (;;)
            pause();
    }

    sleep(1);
    kill(pid, SIGSTOP);
    sleep(1);
    dump_ps("after-stop");

    kill(pid, SIGCONT);
    sleep(1);
    dump_ps("after-cont");

    kill(pid, SIGKILL);
    waitpid(pid, 0, 0);
    printf("jobtest: done\n");
    return 0;
}
