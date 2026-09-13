/* cat：读文件并输出到 stdout（支持多个文件）；没有文件或 "-" 表示 stdin，
   这样它既能单独用，也能当管道的下游（cat /hello.txt | wc 的反面）。 */

#include "lib.h"

static void copy_fd(int fd)
{
    char buf[512];
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char *argv[])
{
    int i, fd;

    if (argc < 2) {
        copy_fd(0);
        return 0;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            copy_fd(0);
            continue;
        }
        fd = open(argv[i], 0, 0);          /* O_RDONLY */
        if (fd < 0) {
            printf("cat: %s: no such file\n", argv[i]);
            continue;
        }
        copy_fd(fd);
        close(fd);
    }
    return 0;
}
