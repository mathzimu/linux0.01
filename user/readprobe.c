/* read 探针（临时诊断程序）：把每次 read() 的返回值打印出来。
 *
 * 起因：把 grep 改成按块流式读之后，读 /big.txt（18432 字节，mkminix 造的
 * 18KB 文件）只看到了第一行——而 `wc < /big.txt` 能把整份读完。这个探针就是
 * 用来分清"顺序 read 的 fd 偏移是否推进"与"返回的字节数是否正确"。
 *
 * 用法：exec /bin/readprobe [file]        （默认 /big.txt） */

#include "lib.h"

#define CHUNK 1024

static char buf[CHUNK];

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : "/big.txt";
    int fd, n, i = 0, total = 0;

    fd = open(path, 0, 0);
    if (fd < 0) {
        printf("probe: cannot open %s\n", path);
        return 1;
    }
    printf("probe: %s, chunk=%d\n", path, CHUNK);

    for (;;) {
        n = read(fd, buf, CHUNK);
        if (n <= 0) {
            printf("probe: read %d -> %d (stop)\n", i + 1, n);
            break;
        }
        i++;
        total += n;
        if (i <= 8)
            printf("probe: read %d -> %d, first=%c\n", i, n, buf[0]);
        if (i > 64) {
            printf("probe: giving up after %d reads\n", i);
            break;
        }
    }

    printf("probe: %d reads, %d bytes\n", i, total);
    close(fd);
    return 0;
}
