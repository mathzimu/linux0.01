/* pwd：打印当前工作目录的绝对路径。
 *
 * 优先用 getcwd(2)（系统调用 73，内核从 pwd inode 沿 ".." 反查，见 kernel/sys.c）。
 * 老的纯用户态实现（用 opendir/readdir 加相对路径上溯）保留为降级路径：它不依赖
 * 新系统调用，也不 chdir，所以跑在旧内核上也不会弄乱调用者的工作目录。逐级上溯
 * 的细节见下（第 d 层打开 ".."/"../.."，在父目录里找 inode 号等于当前目录的条目）。
 *
 *   第 d 层打开 ".."、"../.."、…，在里面找 inode 号等于"当前目录"的那个条目，
 *   它就是当前目录在父目录里的名字；父目录自己的 inode 号从**同一次** opendir 的
 *   "." 条目拿到，祖父的从 ".." 条目拿到（所以每个目录只读一遍——readdir 是有状态
 *   的，分两次扫描会读空）。
 *
 * MINIX v1 里根目录的 ".." 与 "." 同号，循环自然在那里收尾；一次 chdir 都不做，
 * 失败也不会给调用者留下副作用。路径深度上限 16 层，超出时会打印已解析出的前缀。 */

#include "lib.h"

#define MAX_DEPTH 16

static char names[MAX_DEPTH][NAME_MAX + 1];

/* depth=0 -> ".."；depth=1 -> "../.."；… */
static DIR *open_up(int depth)
{
    static char path[MAX_DEPTH * 3 + 1];
    int i;

    path[0] = '\0';
    for (i = 0; i <= depth; i++) {
        if (i)
            strcat(path, "/");
        strcat(path, "..");
    }
    return opendir(path);
}

static int pwd_via_walk(void);

int main(void)
{
    char buf[512];

    /* Prefer the kernel's getcwd(2) (syscall 73): it walks the tree with
       real inode reads and does not touch the caller's cwd.  Fall back to
       the purely userland ".." walk below for kernels without it. */
    if (getcwd(buf, sizeof(buf)) == 0) {
        printf("%s\n", buf);
        return 0;
    }
    return pwd_via_walk();
}

static int pwd_via_walk(void)
{
    DIR *d;
    struct dirent *e;
    unsigned short cur, parent;
    int depth;

    d = opendir(".");
    if (!d) {
        printf("pwd: cannot open .\n");
        return 1;
    }
    cur = 0;
    parent = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0)
            cur = e->d_ino;
        else if (strcmp(e->d_name, "..") == 0)
            parent = e->d_ino;
    }
    closedir(d);

    if (!cur) {
        printf("pwd: cannot read .\n");
        return 1;
    }

    for (depth = 0; depth < MAX_DEPTH && cur != parent; depth++) {
        unsigned short self = 0, up = 0;
        int found = 0;

        d = open_up(depth);
        if (!d)
            break;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0)
                self = e->d_ino;
            else if (strcmp(e->d_name, "..") == 0)
                up = e->d_ino;
            else if (e->d_ino == cur) {
                strcpy(names[depth], e->d_name);
                found = 1;
            }
        }
        closedir(d);
        if (!found || !self)
            break;                  /* 名字查不到：结构异常，打印已有的前缀 */
        cur = self;
        parent = up;
    }

    if (depth == 0) {
        printf("/\n");
        return 0;
    }
    while (depth-- > 0)
        printf("/%s", names[depth]);
    printf("\n");
    return 0;
}
