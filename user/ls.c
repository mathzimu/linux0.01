/* ls [-l] [path]：列目录（或单个文件）。-l 用 stat(2) 打长格式。
 *
 * 默认输出保持不变（"ino  name"），`-l` 才走长格式：类型/权限位、链接数、
 * uid、gid、大小、名字。权限字符串按 S_IRUSR..S_IXOTH 逐个拼出，文件类型由
 * S_ISDIR/S_ISCHR/S_ISBLK/S_ISFIFO 判出。目录项里的 "." 和 ".." 也照常列出。
 *
 * 先 stat 判类型，而不是直接 opendir：opendir 对普通文件也会成功（open 不区分
 * 类型），随后 readdir 会把文件内容当目录项读，`ls -l 文件` 就会拿垃圾名字去
 * stat "文件/垃圾名"，让内核打出一串 cannot traverse。先判类型就把这条路堵死了。
 *
 * 没有 snprintf，路径拼装用手写 strcpy/strcat；一个分量最长 NAME_MAX 字节，
 * 缓冲区 256 足够。 */

#include "lib.h"
#include <sys/stat.h>

static void mode_str(umode_t m, char s[11])
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' :
           S_ISFIFO(m) ? 'p' : '-';
    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_IXUSR) ? 'x' : '-';
    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_IXGRP) ? 'x' : '-';
    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_IXOTH) ? 'x' : '-';
    s[10] = '\0';
}

static void print_long(const char *name, struct stat *st)
{
    char mode[11];

    mode_str(st->st_mode, mode);
    printf("%s %d %d %d %d %s\n", mode, st->st_nlink, st->st_uid,
           st->st_gid, (int)st->st_size, name);
}

int main(int argc, char *argv[])
{
    int longfmt = 0, i = 1;
    const char *path = "/";
    struct stat st;
    DIR *d;
    struct dirent *e;

    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        longfmt = 1;
        i = 2;
    }
    if (i < argc)
        path = argv[i];

    /* 先看类型：普通文件只打一行，目录才走 opendir/readdir。 */
    if (stat(path, (unsigned long *)&st) < 0) {
        printf("ls: %s: cannot open\n", path);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (longfmt)
            print_long(path, &st);
        else
            printf("%d  %s\n", st.st_ino, path);
        return 0;
    }

    d = opendir(path);
    if (!d) {
        printf("ls: %s: cannot open\n", path);
        return 1;
    }
    while ((e = readdir(d)) != NULL) {
        if (longfmt) {
            char full[256];

            strcpy(full, path);
            if (strcmp(path, "/") != 0)
                strcat(full, "/");
            strcat(full, e->d_name);
            if (stat(full, (unsigned long *)&st) == 0)
                print_long(e->d_name, &st);
            else
                printf("ls: %s: cannot stat\n", e->d_name);
        } else {
            printf("%d  %s\n", e->d_ino, e->d_name);
        }
    }
    closedir(d);
    return 0;
}
