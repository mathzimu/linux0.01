/* grep [-n] [-c] <pattern> <file>：逐行搜索模式串（strstr 子串匹配）。
 *
 * 读法仍是原来那套"一次读进 4095 字节缓冲" —— 这个 4KB 上限是**已知限制**
 * （/big.txt 那种 18KB 文件只有前 4KB 会被搜到）。改它要换成按块流式读，那是
 * 单独一步；这一版只加两个真正影响可用性的开关：
 *
 *   -n   打印行号（行号按 '\n' 真实计数）
 *   -c   只打印匹配行数
 *
 * 两者同时给时 -c 生效（与 GNU grep 一致）。匹配不到返回 1，这是 grep 的约定，
 * 也让 shell 的 `sh: grep exited with 1` 有意义。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char *buf = malloc(4096);
    int fd, n, i = 1, numbered = 0, count_only = 0, lineno = 0, matches = 0;
    char *line, *p;
    const char *pattern;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        const char *f;

        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'n')
                numbered = 1;
            else if (*f == 'c')
                count_only = 1;
            else {
                printf("usage: grep [-n] [-c] <pattern> <file>\n");
                return 1;
            }
        }
        i++;
    }
    if (argc - i < 2) {
        printf("usage: grep [-n] [-c] <pattern> <file>\n");
        return 1;
    }
    pattern = argv[i];

    fd = open(argv[i + 1], 0, 0);
    if (fd < 0) {
        printf("grep: %s: no such file\n", argv[i + 1]);
        return 1;
    }
    n = read(fd, buf, 4095);
    if (n < 0) {
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    close(fd);

    line = buf;
    while ((p = strchr(line, '\n')) != NULL) {
        *p = '\0';
        lineno++;
        if (strstr(line, pattern)) {
            matches++;
            if (!count_only) {
                if (numbered)
                    printf("%d:%s\n", lineno, line);
                else
                    printf("%s\n", line);
            }
        }
        line = p + 1;
    }
    if (*line) {                    /* 最后一行没有以换行结尾 */
        lineno++;
        if (strstr(line, pattern)) {
            matches++;
            if (!count_only) {
                if (numbered)
                    printf("%d:%s\n", lineno, line);
                else
                    printf("%s\n", line);
            }
        }
    }

    if (count_only)
        printf("%d\n", matches);
    return matches ? 0 : 1;
}
