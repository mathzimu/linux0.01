/* wc：统计文件的行数、单词数、字节数。没有文件名时读 stdin（管道的下游）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int fd, n, i, inword = 0, close_it;
    long lines = 0, words = 0, bytes = 0;
    const char *name;

    if (argc < 2 || strcmp(argv[1], "-") == 0) {
        fd = 0;                        /* stdin: works as `wc < f` or `a | wc` */
        close_it = 0;
        name = "-";
    } else {
        fd = open(argv[1], 0, 0);
        if (fd < 0) {
            printf("wc: %s: no such file\n", argv[1]);
            return 1;
        }
        close_it = 1;
        name = argv[1];
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n')
                lines++;
            if (isspace(c))
                inword = 0;
            else if (!inword) {
                inword = 1;
                words++;
            }
        }
    }
    if (close_it)
        close(fd);
    printf("%d %d %d %s\n", lines, words, bytes, name);
    return 0;
}
