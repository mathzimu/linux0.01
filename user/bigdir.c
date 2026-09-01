/* 目录扩容验证：在 /big 建 70 个文件（> 64 项，触发单间接块目录），
   再用 opendir/readdir 数条目确认全部写入成功。 */

#include "lib.h"

/* 生成 "f<digits>" 文件名（用户库无 sprintf） */
static void name_it(char *buf, int i)
{
    char tmp[8];
    int k = 0, j = 0;

    buf[j++] = 'f';
    if (i == 0) {
        buf[j++] = '0';
        buf[j] = 0;
        return;
    }
    while (i) {
        tmp[k++] = (char)('0' + i % 10);
        i /= 10;
    }
    while (k)
        buf[j++] = tmp[--k];
    buf[j] = 0;
}

int main(void)
{
    char name[16], path[80];
    int i, fd, n;

    /* 建目录 /big（若已存在则忽略失败） */
    if (mkdir("/big", 0777) < 0)
        printf("mkdir /big (may already exist)\n");
    else
        printf("mkdir /big ok\n");

    /* 创建 70 个空文件 —— 超过 64 项，目录须扩容到间接块 */
    for (i = 0; i < 70; i++) {
        name_it(name, i);
        path[0] = 0;
        strcat(path, "/big/");
        strcat(path, name);
        fd = open(path, O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("open %s failed at i=%d\n", path, i);
            return 1;
        }
        close(fd);
    }
    printf("created 70 files under /big\n");

    /* 读回：readdir 应给出 70 + 2 (. 和 ..) = 72 项 */
    {
        DIR *d = opendir("/big");
        if (!d) {
            printf("opendir /big failed\n");
            return 1;
        }
        n = 0;
        {
            struct dirent *e;
            while ((e = readdir(d)) != NULL)
                n++;
        }
        closedir(d);
    }
    printf("readdir /big = %d entries (expect 72: 70 files + '.' + '..')\n", n);
    return (n == 72) ? 0 : 1;
}
