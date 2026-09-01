/* cat：读文件并输出到 stdout（支持多个文件）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int i, fd, n;

    if (argc < 2) {
        printf("usage: cat <file>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        fd = open(argv[i], 0, 0);          /* O_RDONLY */
        if (fd < 0) {
            printf("cat: %s: no such file\n", argv[i]);
            continue;
        }
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, n);
        close(fd);
    }
    return 0;
}
