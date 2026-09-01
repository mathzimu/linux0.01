/* printf 增强特性演示：宽度、精度、long 修饰符。 */

#include "lib.h"

int main(void)
{
    long  l = 123456789L;
    unsigned long ul = 0xCAFEBABEUL;
    char *s = "kernel";

    printf("[%d]\n", 42);          /* 普通 */
    printf("[%5d]\n", 42);         /* 右对齐宽度 */
    printf("[%.3d]\n", 42);        /* 精度补零 */
    printf("[%.5d]\n", 7);
    printf("[%8.4d]\n", 123);      /* 宽度+精度 */
    printf("[%-6d]\n", 42);        /* 左对齐 */
    printf("[%ld]\n", l);          /* long 十进制 */
    printf("[%lu]\n", 1234567890UL);
    printf("[%lx]\n", ul);         /* long 十六进制 */
    printf("[%#lx]\n", ul);        /* 0x 前缀 */
    printf("[%5s]\n", s);          /* 字符串宽度 */
    printf("[%.3s]\n", s);         /* 字符串精度(截断) */
    printf("[%8.4s]\n", s);
    printf("[%x]\n", 255);
    printf("done\n");
    return 0;
}
