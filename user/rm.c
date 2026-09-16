/* rm：删除文件（unlink，10）与目录（rmdir，40；-r 递归）。
 *
 * 和 mkdir 一样，这是用户态缺的一块。删除的判断顺序是刻意的：
 *
 *   1. unlink()——普通文件走这条；目录会被内核拒绝；
 *   2. rmdir()——空目录走这条；
 *   3. 都给绝了再看 -r：用 opendir/readdir 深度优先删空，最后 rmdir 自己。
 *
 * 没有 stat(2) 可用（Linux 0.01 没有这个系统调用），所以"这是不是目录"
 * 只能靠这三个调用的返回值推出来，而不是先查属性再决定——这也让 -r 只在
 * 真需要时才去递归。 */

#include "lib.h"

#define PATH_MAX_LOCAL 128

static int rm_one(const char *path, int recursive);
static int rm_tree(const char *path);

/* 删掉一个目录的全部内容，然后删目录本身。 */
static int rm_tree(const char *path)
{
    DIR *d;
    struct dirent *e;
    char child[PATH_MAX_LOCAL];
    int rc = 0;

    d = opendir(path);
    if (!d) {
        printf("rm: %s: cannot open\n", path);
        return 1;
    }

    while ((e = readdir(d)) != NULL) {
        unsigned long need;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        need = strlen(path) + strlen(e->d_name) + 2;
        if (need > sizeof(child)) {
            printf("rm: %s: path too long\n", e->d_name);
            rc = 1;
            continue;
        }
        strcpy(child, path);
        if (child[strlen(child) - 1] != '/')
            strcat(child, "/");
        strcat(child, e->d_name);

        if (rm_one(child, 1) != 0)
            rc = 1;
    }
    closedir(d);

    if (rc == 0 && rmdir(path) < 0) {
        printf("rm: %s: cannot remove directory\n", path);
        rc = 1;
    }
    return rc;
}

static int rm_one(const char *path, int recursive)
{
    if (unlink(path) == 0)
        return 0;                       /* plain file */

    if (rmdir(path) == 0)
        return 0;                       /* empty directory */

    if (!recursive) {
        printf("rm: %s: cannot remove (directory needs -r, or not permitted)\n",
               path);
        return 1;
    }
    return rm_tree(path);               /* non-empty directory */
}

int main(int argc, char *argv[])
{
    int i, first = 1, recursive = 0, rc = 0;

    if (argc > 1 && strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        first = 2;
    }
    if (first >= argc) {
        printf("usage: rm [-r] <file>...\n");
        return 1;
    }

    for (i = first; i < argc; i++)
        if (rm_one(argv[i], recursive) != 0)
            rc = 1;
    return rc;
}
