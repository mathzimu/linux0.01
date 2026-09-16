/* tail [-n N] [file]：打印文件（或 stdin）的最后 N 行，默认 10 行。
 *
 * 这个内核的 read() 只能往前读，所以做法是"整块读进来、再从末尾往回数 N 个
 * 换行"。缓冲区是**静态数组**而不是 malloc：用户堆的可用量比这个小得多
 * （1MB 的 malloc 在实测里直接失败，于是 tail 打不出任何东西）。256KB 覆盖了
 * 这个文件系统上所有测试文件（最大的是 18KB 的 /big.txt）；真要看更大的文件，
 * 才需要真实 tail 里那套 N 行环形缓冲，可以完全不要缓冲区。
 *
 * 文件末尾那个换行不算"另起一行"，否则 `tail -n 2` 在 "b\nc\n" 上只会打印
 * "c\n"。 */

#include "lib.h"

#define TAIL_MAX (256 * 1024)

static char buf[TAIL_MAX];

int main(int argc, char *argv[])
{
    int n = 10, first = 1, fd = 0, len = 0, r, start, lines = 0;

    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        first = 3;
    }

    if (first < argc) {
        fd = open(argv[first], 0, 0);
        if (fd < 0) {
            printf("tail: %s: cannot open\n", argv[first]);
            return 1;
        }
    }

    while (len < TAIL_MAX && (r = read(fd, buf + len, TAIL_MAX - len)) > 0)
        len += r;
    if (fd > 0)
        close(fd);

    if (n <= 0)
        return 0;

    start = len;
    if (start > 0 && buf[start - 1] == '\n')
        start--;

    while (start > 0) {
        start--;
        if (buf[start] == '\n') {
            lines++;
            if (lines == n) {
                start++;
                break;
            }
        }
    }

    for (r = start; r < len; r++)
        write(1, &buf[r], 1);
    return 0;
}
