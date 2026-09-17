/* devtest：验证 /dev/null、/dev/zero、/dev/tty 三个合成设备文件。
 *
 * 诊断程序，不进默认镜像（与 readprobe 一样用 make prog 注入）。预期输出：
 *   null read=0            —— /dev/null 读即 EOF
 *   zero read=16 allzero=1 —— /dev/zero 读出来全是 0
 *   tty ok                 —— /dev/tty 写回控制台
 * 任何一行不符都说明 kernel/sys.c 的 f_dev 分派或 fs.h 的 DEV_* 出了错。 */

#include "lib.h"

int main(void)
{
    char buf[16];
    int fd, n, i, allzero;

    fd = open("/dev/null", 0, 0);
    if (fd < 0) {
        printf("open /dev/null failed\n");
        return 1;
    }
    n = read(fd, buf, 16);
    write(fd, "x", 1);                  /* discard, must not block or fault */
    close(fd);
    printf("null read=%d\n", n);        /* expect 0 */

    fd = open("/dev/zero", 0, 0);
    if (fd < 0) {
        printf("open /dev/zero failed\n");
        return 1;
    }
    n = read(fd, buf, 16);
    allzero = 1;
    for (i = 0; i < n; i++)
        if (buf[i] != 0)
            allzero = 0;
    close(fd);
    printf("zero read=%d allzero=%d\n", n, allzero);  /* expect 16 1 */

    fd = open("/dev/tty", 2, 0);        /* O_RDWR */
    if (fd < 0) {
        printf("open /dev/tty failed\n");
        return 1;
    }
    write(fd, "tty ok\n", 6);
    close(fd);
    return 0;
}
