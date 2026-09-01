/* touch：创建空文件（若已存在则保持内容，仅更新）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    int i, fd;

    if (argc < 2) {
        printf("usage: touch <file>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        fd = open(argv[i], O_CREAT | O_WRONLY, 0644);
        if (fd < 0)
            printf("touch: %s: failed\n", argv[i]);
        else
            close(fd);
    }
    return 0;
}
