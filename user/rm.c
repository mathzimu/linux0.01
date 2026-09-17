/* rm [-r] <file>...：删文件（unlink，10）与目录（rmdir，40；-r 递归）。
 *
 * 用 stat(2) 先判类型，而不是像早先那样靠 unlink/rmdir 的返回值猜——
 * 内核一直有 stat（Linux 0.01 第 18 号），所以没必要猜：
 *
 *   - 普通文件 → unlink；
 *   - 目录 → 没有 -r 就报 "is a directory"；有 -r 就深度优先删空再 rmdir；
 *   - stat 失败 → "no such file"。
 *
 * 这样三类错误消息各归各位，比"都猜不出来就一句'目录需要 -r 或没有权限'"
 * 清楚得多。rm_tree 仍用 opendir/readdir 递归，因为调用它的前提已经是目录。 */

#include "lib.h"
#include <sys/stat.h>

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
    struct stat st;

    if (stat(path, (unsigned long *)&st) < 0) {
        printf("rm: %s: no such file\n", path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) < 0) {
            printf("rm: %s: cannot remove\n", path);
            return 1;
        }
        return 0;
    }

    if (!recursive) {
        printf("rm: %s: is a directory\n", path);
        return 1;
    }
    return rm_tree(path);
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
