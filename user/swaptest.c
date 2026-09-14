/* swaptest — 匿名页换出/换入（B4）
 *
 * B3 能回收的只有零页、只读正文页和 COW 共享页；程序真正吃内存的**私有脏页**
 * （堆、栈）没有后备存储，池子一空就只能 OOM 杀进程。B4 把它们写到磁盘镜像
 * 末尾的裸 swap 区（一个槽 = 一个 4KB 页，槽号就记在页表项里），下次缺页时
 * 再读回来。
 *
 * 本程序在**小内存**（4MB，约 695 个空闲帧）下制造超出物理内存的**不可回收**
 * 需求：3 个子进程各把 768KB 堆填上非零图案后**持有**（alarm 定时自行退场），
 * 父进程在它们持有期间再填自己的 768KB —— 峰值约 768 页私有脏页 + 栈/正文，
 * 超过物理内存。没有 swap 时这里必然 OOM 杀进程；有 swap 时数据被换出、再换
 * 回来，每个进程都要能逐字节读回自己的图案。
 *
 * 用法：QEMU_MEM=4M 跑场景 swap（scripts/regress.sh 场景 24）。
 */
#include "lib.h"
#include <sys/times.h>

#define HEAP_BYTES (768 * 1024)
#define MAX_KIDS   3
#define HOLD_SECS  20

static unsigned char pattern(unsigned long i)
{
    return (unsigned char)(0x5A ^ (i >> 12));
}

/* 填满给定的堆并立刻校验（读回会触发换入）；返回不匹配的页数 */
static int fill_and_check(unsigned char *p, int report)
{
    unsigned long i;
    int bad = 0;

    for (i = 0; i < HEAP_BYTES; i += 4096) {
        p[i] = pattern(i);
        p[i + 1] = pattern(i + 1);
        if ((i & (256 * 1024 - 1)) == 0) {
            struct tms tm;

            times((unsigned long *)&tm);      /* a syscall: stay killable */
            if (report)
                printf("swaptest: pid=%d filled %dKB\n", getpid(),
                       (int)(i / 1024));
        }
    }

    for (i = 0; i < HEAP_BYTES; i += 4096) {
        if (p[i] != pattern(i) || p[i + 1] != pattern(i + 1))
            bad++;
    }
    if (bad)
        printf("swaptest: pid=%d CORRUPT in %d page(s)\n", getpid(), bad);
    return bad;
}

int main(void)
{
    unsigned char *heap;
    int i, pid, kids = 0, bad = 0;
    int pids[MAX_KIDS];
    unsigned long status;

    /* Fork first and allocate after: user/lib.c's malloc is first-fit over
       one 1MB region, so a parent that had already taken 768KB would leave
       its children almost nothing. */
    for (i = 0; i < MAX_KIDS; i++) {
        pid = fork();
        if (pid < 0)
            break;

        if (pid == 0) {
            heap = (unsigned char *)malloc(HEAP_BYTES);
            if (!heap) {
                printf("swaptest: pid=%d malloc failed\n", getpid());
                exit(1);
            }
            if (fill_and_check(heap, 1) != 0)
                exit(1);
            printf("swaptest: pid=%d filled and verified %dKB, holding\n",
                   getpid(), HEAP_BYTES / 1024);
            alarm(HOLD_SECS);                 /* leave on our own */
            for (;;)
                pause();
        }
        pids[kids++] = pid;
    }
    printf("swaptest: forked %d children\n", kids);

    /* Now take our own share while they hold theirs: this is where the
       pool runs out and pages have to go to the swap device. */
    heap = (unsigned char *)malloc(HEAP_BYTES);
    printf("swaptest: parent filling %dKB heap under pressure\n",
           HEAP_BYTES / 1024);
    if (!heap) {
        printf("swaptest: parent malloc failed\n");
        bad++;
    } else if (fill_and_check(heap, 1) != 0) {
        bad++;
    }

    /* The children time themselves out; 142 is 128+SIGALRM, anything else
       means one of them died of something worse (139 = SIGSEGV on a failed
       fault, i.e. no memory and no swap). */
    for (i = 0; i < kids; i++) {
        if (waitpid(pids[i], &status, 0) != pids[i]) {
            printf("swaptest: child %d could not be reaped\n", pids[i]);
            bad++;
        } else if (status != 128 + 14) {
            printf("swaptest: child %d exited %lu (expected 142): it was "
                   "killed, not swapped\n", pids[i], status);
            bad++;
        }
    }

    /* Read our own data back one more time: by now those pages may well
       have been swapped out while the children held theirs. */
    if (heap && fill_and_check(heap, 0) != 0)
        bad++;

    if (bad == 0) {
        printf("swaptest: PASS (%d children, data intact across swap)\n",
               kids);
        return 0;
    }
    printf("swaptest: FAIL (%d problem(s))\n", bad);
    return 1;
}
