/* ls：用用户库 opendir/readdir/closedir 列目录（示例）。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    const char *path = (argc > 1) ? argv[1] : "/";
    DIR *d;
    struct dirent *e;

    d = opendir(path);
    if (!d) {
        printf("ls: %s: cannot open\n", path);
        return 1;
    }
    while ((e = readdir(d)) != NULL)
        printf("%d  %s\n", e->d_ino, e->d_name);
    closedir(d);
    return 0;
}
