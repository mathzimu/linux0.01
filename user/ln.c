/* ln：建立硬链接（系统调用 9 link）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("usage: ln <existing> <new>\n");
        return 1;
    }
    if (link(argv[1], argv[2]) < 0) {
        printf("ln: %s -> %s: failed\n", argv[1], argv[2]);
        return 1;
    }
    return 0;
}
