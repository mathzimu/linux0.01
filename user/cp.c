/* cp：复制文件（open/read/write/close 的完整闭环）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int fdin, fdout, n;

    if (argc < 3) {
        printf("usage: cp <src> <dst>\n");
        return 1;
    }
    fdin = open(argv[1], 0, 0);
    if (fdin < 0) {
        printf("cp: %s: no such file\n", argv[1]);
        return 1;
    }
    fdout = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fdout < 0) {
        printf("cp: cannot create %s\n", argv[2]);
        close(fdin);
        return 1;
    }
    while ((n = read(fdin, buf, sizeof(buf))) > 0)
        write(fdout, buf, n);
    close(fdin);
    close(fdout);
    printf("cp: %s -> %s done\n", argv[1], argv[2]);
    return 0;
}
