/* umask [octal-mask]：打印/设置文件创建掩码（系统调用 60）。
 *
 * 不带参数：用 umask(0); umask(old) 的经典做法读出当前值而不留下改变。
 * 带参数：设置并打印旧值（POSIX 约定）。掩码是八进制。 */

#include "lib.h"

static int parse_octal(const char *s)
{
    int v = 0;

    for (; *s; s++) {
        if (*s < '0' || *s > '7')
            return -1;
        v = v * 8 + (*s - '0');
    }
    return v;
}

int main(int argc, char *argv[])
{
    int old;

    if (argc > 1) {
        int mask = parse_octal(argv[1]);

        if (mask < 0) {
            printf("umask: %s: not an octal mask\n", argv[1]);
            return 1;
        }
        old = umask(mask);
    } else {
        old = umask(0);
        umask(old);
    }
    printf("0%o\n", old);
    return 0;
}
