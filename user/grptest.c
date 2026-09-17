/* grptest：验证 kill(-pgid) 组播——两个子进程进同一进程组，一条 kill(-pgid,
 * SIGKILL) 应同时带走两个。诊断程序，make prog NAME=grptest 注入。 */

#include "lib.h"

int main(void)
{
    int c1, c2;

    c1 = fork();
    if (c1 < 0) {
        printf("grptest: fork failed\n");
        return 1;
    }
    if (c1 == 0)
        for (;;) pause();

    c2 = fork();
    if (c2 < 0) {
        printf("grptest: fork2 failed\n");
        return 1;
    }
    if (c2 == 0)
        for (;;) pause();

    /* c1 成为组长，c2 加入 c1 的组（父进程不加入，避免把自己也杀了） */
    if (setpgid(c1, c1) < 0 || setpgid(c2, c1) < 0) {
        printf("grptest: setpgid failed\n");
        return 1;
    }

    sleep(1);
    printf("grptest: kill(-%d) -> %d\n", c1, kill(-c1, SIGKILL));

    if (waitpid(c1, 0, 0) != c1 || waitpid(c2, 0, 0) != c2) {
        printf("grptest: a child did not die\n");
        return 1;
    }
    printf("grptest: group kill reaped both children\n");
    return 0;
}
