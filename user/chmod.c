/* chmod <octal-mode> <file>...：改文件权限（系统调用 15）。
 *
 * 只收八进制数字（如 0644、0600），与内核 i_mode 的低 9 位一一对应；符号
 * 形式（u+x、go-w）留给更完整的 shell 语义。失败不中断，逐个文件报错。 */

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
    int mode, i, rc = 0;

    if (argc < 3) {
        printf("usage: chmod <octal-mode> <file>...\n");
        return 1;
    }
    mode = parse_octal(argv[1]);
    if (mode < 0) {
        printf("chmod: %s: not an octal mode\n", argv[1]);
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (chmod(argv[i], mode) < 0) {
            printf("chmod: %s: failed\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
