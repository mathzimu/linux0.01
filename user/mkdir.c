/* mkdir：创建目录（系统调用 39）。
 *
 * 这是 Ring3 工具集里缺的一块：内核态 shell 有 mkdir 内建命令，但用户态
 * （`exec /bin/sh` 之后）一直没有，`mkdir /d` 只会得到
 * `sh: /bin/mkdir: cannot execute`。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    int i, rc = 0;

    if (argc < 2) {
        printf("usage: mkdir <dir>...\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) < 0) {
            printf("mkdir: %s: failed\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
