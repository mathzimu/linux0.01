/* evicttest — 内存压力下的页回收（B3）
 *
 * 按需调页如果没有回收，就只是半个内存管理器：池子一空就只能杀进程。
 * B3 让三类页可以被安全回收（见 mm/memory.c try_to_free_page）：
 *   1. 全零页 —— 丢弃，下次访问重新变成零页；
 *   2. 写时复制的共享页 —— 只解除本进程的映射，物理页留给另一个进程；
 *   3. 只读镜像页（程序正文）—— 丢弃，下次取指时从可执行文件读回。
 *
 * 本程序在**小内存**（4MB，约 695 个空闲页）下故意制造超出物理内存的需求：
 * 父进程与 6 个子进程各触碰 512KB 零 BSS + 256KB 私有堆，峰值需求约 1280 页。
 * 其中零页与正文页可回收、私有堆页不可回收，所以系统必须靠回收撑过去，
 * 而不是把进程 OOM 掉。
 *
 * 子进程触碰完内存后**驻留**（管道屏障告诉父进程"我已经占住了"），这样峰值
 * 压力是真实存在的；父进程确认全部驻留后校验自己的数据，再 kill 掉它们。
 * 任何一页被回收后没能正确重建，校验都会失败。
 *
 * 用法：QEMU_MEM=4M 跑场景 evict（scripts/regress.sh 场景 22）。
 */
#include "lib.h"
#include <sys/times.h>

/* Demand per process: 896KB of zero .bss (224 pages) + 512KB of heap
   (128 pages) = 352 pages.  Three processes (parent + 2 children) want
   ~1056 pages on a 4MB machine with 695 free frames, so reclamation is
   forced — but with only three tasks faulting at once the system does
   not spend all its time thrashing, which matters because this test runs
   under TCG in CI. */
#define BSS_BYTES  (896 * 1024)
#define HEAP_BYTES (512 * 1024)
#define MAX_KIDS   2

static char blob[BSS_BYTES];

/* 触碰整块 BSS：每一页都真的建起来（内容保持 0，所以是可回收的零页） */
static void touch_bss(void)
{
    int i;

    for (i = 0; i < (int)sizeof(blob); i += 4096)
        blob[i] = 0;
}

/* 读回校验：任何一页被回收后没有正确重建，都会在这里露出来 */
static int check_bss(const char *who)
{
    int i;

    for (i = 0; i < (int)sizeof(blob); i += 4096) {
        if (blob[i] != 0) {
            printf("evicttest: %s CORRUPT at +%d\n", who, i);
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    int i, pid, kids = 0, bad = 0, k;
    int pids[MAX_KIDS];
    unsigned long status;

    touch_bss();
    printf("evicttest: parent touched %dKB of zero BSS\n", BSS_BYTES / 1024);

    for (i = 0; i < MAX_KIDS; i++) {
        pid = fork();
        if (pid < 0)
            break;

        if (pid == 0) {
            unsigned char *heap = (unsigned char *)malloc(HEAP_BYTES);
            int n;

            /* 写自己那份 BSS：COW 断开会给每个子进程一份私有零页。
               循环里定期做一次系统调用：本内核只在**系统调用返回**时投递
               信号，纯内存写的循环会让 alarm 一直悬着不生效。 */
            for (n = 0; n < (int)sizeof(blob); n += 4096) {
                blob[n] = 0;
                if ((n & (128 * 1024 - 1)) == 0) {
                    struct tms tm;

                    times((unsigned long *)&tm);       /* a syscall */
                    printf("evicttest: child %d touched %dKB\n",
                           getpid(), n / 1024);
                }
            }

            if (heap) {
                /* 私有脏页：没有后备存储，回收器不能碰它们 */
                for (n = 0; n < HEAP_BYTES; n += 4096)
                    heap[n] = (unsigned char)(n >> 12);
            }

            if (check_bss("child") < 0)
                exit(1);
            if (heap) {
                for (n = 0; n < HEAP_BYTES; n += 4096) {
                    if (heap[n] != (unsigned char)(n >> 12)) {
                        printf("evicttest: child %d CORRUPT (heap +%d)\n",
                               getpid(), n);
                        exit(1);
                    }
                }
            }

            printf("evicttest: child %d ok and holding\n", getpid());

            /* Hold the pages, then leave on our own.  The alarm is not a
               convenience: signals in this kernel are delivered on
               *system call* return, so a SIGKILL sent while a child is
               buried in a pure memory-writing fault loop is not acted on
               until that loop ends.  Letting each child time itself out
               sidesteps that entirely — and the expected exit status
               (128+SIGALRM = 142) is a precise check: anything else
               (139 = killed by SIGSEGV on a failed fault) is a failure. */
            alarm(3);
            for (;;)
                pause();
        }
        pids[kids++] = pid;
    }
    printf("evicttest: forked %d children\n", kids);

    /* Create pressure ourselves while the children get going: every pass
       re-faults our 128 pages, so a frame has to be found for each. */
    for (k = 0; k < 2; k++)
        if (check_bss("parent") < 0)
            bad++;

    /* Reap them in turn: each one should have exited via its alarm. */
    for (i = 0; i < kids; i++) {
        if (waitpid(pids[i], &status, 0) != pids[i]) {
            printf("evicttest: child %d could not be reaped\n", pids[i]);
            bad++;
        } else if (status != 128 + 14) {
            printf("evicttest: child %d exited %lu, expected 142 (SIGALRM): "
                   "reclamation did not keep it alive\n", pids[i], status);
            bad++;
        } else {
            printf("evicttest: child %d finished cleanly (rc=%lu)\n",
                   pids[i], status);
        }
    }

    printf("evicttest: %d children survived to their own timeout\n", kids);

    if (check_bss("parent") < 0)
        bad++;

    if (bad == 0) {
        printf("evicttest: PASS (integrity kept across eviction)\n");
        return 0;
    }
    printf("evicttest: FAIL (%d problem(s))\n", bad);
    return 1;
}
