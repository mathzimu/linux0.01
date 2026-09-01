/* pipe 演示：fork 后父子通过管道通信（Linux 0.01 语义）。
   子进程写入，父进程阻塞读取；读端/写端关闭后 read 返回 0 (EOF)。 */

#include "lib.h"

int main(void)
{
    unsigned long fds[2];
    int pid, n;
    char buf[64];

    if (pipe(fds) < 0) {
        printf("pipe failed\n");
        return 1;
    }
    printf("pipe: read fd=%d write fd=%d\n", (int)fds[0], (int)fds[1]);

    pid = fork();
    if (pid == 0) {
        /* child: write, then close the write end */
        const char *msg = "hello from child via pipe!";
        int len = 0;
        close((int)fds[0]);
        while (msg[len]) len++;
        if (write((int)fds[1], msg, len) != len)
            printf("child: write failed\n");
        close((int)fds[1]);
        return 0;
    }

    /* parent: block on read until the child writes */
    close((int)fds[1]);
    n = read((int)fds[0], buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        printf("parent read %d bytes: \"%s\"\n", n, buf);
    }
    n = read((int)fds[0], buf, sizeof(buf) - 1);
    printf("parent read after EOF: %d (expect 0)\n", n);
    close((int)fds[0]);
    printf("pipe demo done\n");
    return 0;
}
