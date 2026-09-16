/* head：打印文件（不指定文件时读 stdin）的前 N 行，默认 10 行。
 *
 * 逐字节读是刻意的：这个文件系统上 read() 走按需调页 + 缓冲区缓存，
 * 一行一行地读没有额外代价，而"读多少停多少"正是 head 的语义——它不该
 * 为了打印一行而把整个文件拖进内存（`head -n 1 /big.txt` 就是例子）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    int n = 10, first = 1, fd = 0, lines = 0;
    char c;

    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        first = 3;
    }

    if (first < argc) {
        fd = open(argv[first], 0, 0);
        if (fd < 0) {
            printf("head: %s: cannot open\n", argv[first]);
            return 1;
        }
    }

    while (lines < n && read(fd, &c, 1) == 1) {
        write(1, &c, 1);
        if (c == '\n')
            lines++;
    }

    if (fd > 0)
        close(fd);
    return 0;
}
