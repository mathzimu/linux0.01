/* mv：重命名/移动 —— 直接用内核的 rename(2)（系统调用 38，Linux 0.01 就有的）。
 *
 * 早先这版用的是 link(old,new)+unlink(old)：那是因为当时误以为内核没有
 * rename，其实 0.01 的 sys_call_table 第 38 号就是它。用 rename(2) 有三点
 * 严格更好：原子（不会留下"新名字有了、旧名字还在"的中间态）、支持目录、
 * 跨目录移动由内核统一处理。失败时只报一句，原因让调用者去读 errno。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("usage: mv <old> <new>\n");
        return 1;
    }

    if (rename(argv[1], argv[2]) < 0) {
        printf("mv: %s -> %s: failed\n", argv[1], argv[2]);
        return 1;
    }
    return 0;
}
