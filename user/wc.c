/* wc：统计文件的行数、单词数、字节数。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int fd, n, i, inword = 0;
    long lines = 0, words = 0, bytes = 0;

    if (argc < 2) {
        printf("usage: wc <file>\n");
        return 1;
    }
    fd = open(argv[1], 0, 0);
    if (fd < 0) {
        printf("wc: %s: no such file\n", argv[1]);
        return 1;
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
    close(fd);
    printf("%d %d %d %s\n", lines, words, bytes, argv[1]);
    return 0;
}
