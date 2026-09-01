/* 隔离验证：Ring3 程序试图访问内核内存（0x0 页，supervisor-only）。
   期望：page fault → 内核 do_no_page panic（隔离生效的证据）。 */

#include "lib.h"

int main(void)
{
    printf("bad: Ring3 reading kernel memory at 0x0...\n");
    printf("value = %d\n", *(volatile char *)0x0);
    printf("bad: should never reach here\n");
    return 0;
}
