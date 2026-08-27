/* 示例用户程序：make prog NAME=hello 编译注入，QEMU 里 exec /hello 运行。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    int i;

    printf("hello from user program: argc=%d\n", argc);
    for (i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);

    return 42;
}
