/* grep：在文件里逐行搜索模式串（用 libc 的 strstr）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char *buf = malloc(4096);
    int fd, n;
    char *line, *p;

    if (argc < 3) {
        printf("usage: grep <pattern> <file>\n");
        return 1;
    }
    fd = open(argv[2], 0, 0);
    if (fd < 0) {
        printf("grep: %s: no such file\n", argv[2]);
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
        if (strstr(line, argv[1]))
            printf("%s\n", line);
        line = p + 1;
    }
    if (*line && strstr(line, argv[1]))
        printf("%s\n", line);
    return 0;
}
