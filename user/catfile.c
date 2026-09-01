/* 编程体验演示：make prog NAME=catfile 编译注入，exec /catfile /readme.txt 运行。
   用 printf + open/read/close 读取并打印文件内容。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char buf[64];
    int fd, n;

    if (argc < 2) {
        printf("usage: %s <file>\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], 0, 0);
    if (fd < 0) {
        printf("catfile: cannot open %s\n", argv[1]);
        return 1;
    }

    printf("catfile: reading %s\n", argv[1]);
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';               /* printf %s needs NUL termination */
        printf("%s", buf);
    }
    close(fd);

    printf("catfile: done\n");
    return 0;
}
