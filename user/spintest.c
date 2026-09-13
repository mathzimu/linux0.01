/* spintest — 信号投递时机（B2'）
 *
 * 内核原先只在**系统调用返回**时投递信号（ret_from_sys_call）。后果：
 * 一个再也不进内核的进程——纯计算循环，或卡在"纯内存写"缺页循环里的
 * 子进程——既杀不掉，也永远等不到自己的 alarm。
 *
 * 这里就是那个进程：设置 alarm(1) 之后进入一个不含任何系统调用的死循环。
 * 正确行为是定时器中断把它带走（SIGALRM 默认动作 → exit 128+14 = 142）；
 * 没有中断路径投递时，它会一直转下去，父进程（内核 shell 的 exec）永远
 * 等不到它，用例会超时——这正是这条场景要守住的回归。
 *
 * 用法：make prog NAME=spintest  →  exec /bin/spintest
 */
#include "lib.h"

int main(void)
{
    volatile unsigned long spin = 0;

    printf("spintest: pid=%d installing alarm(1), then spinning with no "
           "syscalls\n", getpid());
    alarm(1);

    /* No read/write/getpid/times here on purpose: nothing but a computed
       increment.  Only the timer interrupt can reach us now. */
    for (;;)
        spin++;

    printf("spintest: NOT REACHED (the alarm should have killed us)\n");
    return 0;
}
