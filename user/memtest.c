/* 用户态堆演示：make prog NAME=memtest 编译注入，exec /memtest 运行。
   验证 malloc 写读、free 后重用。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char *a, *b, *c;
    int i;

    printf("memtest: heap test\n");

    a = malloc(100);
    b = malloc(50);
    if (!a || !b) {
        printf("memtest: malloc failed\n");
        return 1;
    }
    printf("memtest: a=%p b=%p\n", a, b);

    for (i = 0; i < 99; i++)
        a[i] = (char)('A' + (i % 26));
    a[99] = '\0';
    printf("memtest: a = %s\n", a);

    free(a);                        /* a 进入空闲链表 */
    c = malloc(80);                 /* 应重用 a 的块 */
    printf("memtest: c=%p (reuse=%d)\n", c, c == a);

    free(b);
    free(c);
    printf("memtest: done\n");
    return 0;
}
