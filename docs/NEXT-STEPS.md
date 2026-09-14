# 后续工作清单（NEXT STEPS）

> Linux 0.01 功能对齐完成于提交 `5426b8b`；其后 main 持续推进（/bin 布局、目录扩容、
> 基础应用、SIGSEGV 语义、测试与 CI 加固）。当前主线是 **M1 → M2 → M3 三阶段演进**。

---

# 演进路线（M1 / M2 / M3）

## M1 — 内存地图固化与静默损坏修复 ✅ 已完成

**动因**：审查中发现两处会**静默写坏文件系统**的重叠（Ring0 无视 PTE 的 U/S 位，
所以既不会缺页也不会报错）：

1. `NR_BUFFERS = 512` → 缓冲区缓存位于 `~0x370000..0x3F2000`，
   **压在用户堆 `[0x310000,0x3FE000)` 上**（M3 前布局）：用户 `malloc` 的字节与文件系统块缓冲是同一批物理页
2. fork 的子进程用户栈锚点 `0x3E0000` **同样落在缓存区里**（M3 前布局）：
   Ring3 fork 会把用户栈副本直接写穿文件系统缓存

**已完成**：

- `include/memlayout.h`：内存地图唯一权威（内核 + 用户态共用）；
  `include/linux/memmap.h`：内核侧边界 + `STATIC_ASSERT`；
  `include/memlayout.inc`：汇编侧镜像（`-Iinclude` 后 `.include "memlayout.inc"`）
- 用户区窗口重排（M3 前布局，M3 已整体废弃）：程序 `[0x200000,0x300000)` / 堆 `[0x310000,0x340000)` /
  fork 子进程栈 `(0x300000,0x340000]` / 缓冲区缓存 `[0x3BC000,0x400000)`（256 × 1KB）
- `NR_BUFFERS` **由内存地图派生**（`include/linux/fs.h`），不再手工挑选；
  `buffer_init()` 装不下即 `panic`（不再静默重叠）
- `mm/memcheck.c` + `mem_check()`：启动即校验（内核 `_end` 上限、缓存 vs 堆/栈/子进程栈、
  缓存条数、用户区有序、页分配器上限），不一致 → 打印完整内存地图并 panic
- `get_free_page()` 加池上限守卫（M3 已改为「耗尽返回 0」）；`lib/malloc.c` 上界从
  `memory_end-0x200000`（M3 前的旧上界）改为
  `KERNEL_HEAP_END`（旧上界会伸进页分配器池）
- `sys_execve()` 校验 ELF 入口与 LOAD 段必须落在用户程序镜像内（越界不再静默写内核区）
- fork 的用户栈副本改到专属区并加上限检查（放不下 → fork 返回 -1，不再覆盖相邻区域）
- `user/bigalloc.c` + 回归场景 11（堆填满 → 重读文件 → 逐字节校验）、场景 12（启动自检）
- `scripts/check-layout.py`（`make check-layout`）：**不需要编译器**即可校验地图；
  并扫描“用户区地址被硬编码到布局头之外”
- `.gitignore`/`git rm --cached`：根目录误跟踪的 `system`、`system.bin` 移出版本库

**M1 遗留（归入 M3）**：fork 仍是**代码段/堆共享 + 用户栈真实复制**，
不是 POSIX 语义（子进程写堆父进程可见、并发 fork 的子进程栈会互相覆盖）。
M1 只是把它限制在可检测/可控范围内，真正的修复是 M3 的 COW + 独立地址空间。
→ **M3 已关闭**：见下文 M3 小节，回归场景 16 `cow` 验证「子进程的写对父进程不可见」。

## M2 — 用户态能力补齐 ✅ 已完成（M2-2 有一处架构性遗留，转 M3）

| 项 | 状态 | 内容 | 关键文件 |
|----|------|------|----------|
| M2-1 | ✅ | **自定义信号处理器 + `sigreturn`**：`signal()` 接受 Ring3 函数指针，投递时在用户栈上构造 sigframe（信号号 + 返回地址 + 80 字节上下文），`do_signal` 在 `ret_from_sys_call` 路径投递；`sigreturn`（syscall 67）校验 magic/retaddr/cs/ss/esp 后精确恢复上下文 | `kernel/process.c`、`kernel/asm.s`、`boot/head.s`、`include/signal.h`、`include/memlayout.h` |
| M2-2 | ⚠️ 部分 | **Ring3 用户态 shell（`/bin/sh`）**：内建 cd/pwd/exit/help，其余按 `/bin/<name>` fork+execve+waitpid；Ring3 标准输入（`read(0,…)`）已通。**遗留**：让子程序*成功* execve 会写坏父 shell 的代码页（见下），故只把 `/bin/sh` 作为救援入口的替代品提供，回归场景默认不跑 | `user/sh.c`、`kernel/main.c`、`init/shell.c` |
| M2-3 | ✅ | **定时回写**：`do_timer` 置标志 + 唤醒专用回写任务（`kernel/sync.c`，占最后一个任务槽），每 5s `sync_dev()`；`sync_dev` 返回真实写入块数，只在 >0 时打印。异常退出不再丢数据 | `kernel/sched.c`、`kernel/sync.c`、`fs/buffer.c` |

**M2-2 的架构性遗留（M3 的入口）**：`execve` 把 ELF 段按 vaddr 直接写进**恒等映射的物理页**。
内核态 shell `exec /bin/x` 没事（父进程是内核），但用户态 shell fork 出子进程后让子进程
execve，子进程就会把新镜像写进 0x200000（M3 前的固定物理地址）—— 那正是父 shell 正在执行的代码页。
现象：父 shell 恢复执行时 EIP 落在指令中间、CPL=3 空转（QEMU `info registers` 可见），
串口输出恰好断在子程序最后一行。这不是 sh 的 bug，而是 M1 遗留（代码段/堆父子共享）
在用户态的第一处硬伤：**只有每进程独立地址空间才能修**。

**M2 新增回归**：场景 14 `autosync`（两阶段：touch 后空转 8s 不调用 sync → 冷启动 `ls` 必须看到文件）；
`TEST_USERSH=1` 时另跑 Ring3 shell 场景（M3 后转正）。

## M3 — 架构级内存模型 ✅ 已完成

放弃「单页表恒等映射 0–4MB + 固定用户地址」，改为**每进程独立页目录 + 帧分配器按页授权**。

**新模型**：

```
内核恒等映射 0..16MB（PDE[0..3]，4 张内核页表，全 supervisor-only，所有进程共享）
  + 页目录 0x100000、内核页表 0x101000..0x105000、页帧池 0x105000 起、缓冲区缓存窗口、mem_map 末尾

用户地址空间（每进程一份页目录，只占 PDE[32] = 0x08000000..0x08400000）
  0x08000000 程序镜像（ELF LOAD 段按页装入新帧）
  0x08100000 堆（首次访问才分配）
  0x08200000 保护空洞（访问 = SIGSEGV）
  0x08300000 栈下界（向下按需增长）
  0x083FF000 栈顶尾页：argc/argv/sigreturn stub
```

| 项 | 状态 | 内容 |
|----|------|------|
| M3-1 | ✅ | `alloc_user_pgdir()`（页目录 + 一张用户页表）/`free_user_space()`/`alloc_user_page()`/`map_user_page()`；内核页表全局共享，`get_free_page()` 上限取消，池 = mem_map 里所有空闲页（16MB 下约 3700 页） |
| M3-2 | ✅ | `execve` 在**新地址空间**里按 ELF LOAD 段建页（内核经恒等映射填帧内容），纯 BSS 页不预先建 → 首次访问触发 `do_no_page` 按需分配；非法区域访问仍 SIGSEGV |
| M3-3 | ✅ | `copy_page_tables()` 让父子共享只读页（PTE 打上软件 COW 位 0x200），`un_wp_page()` 在写缺页时复制；fork 不再复制用户栈，也不再需要「子进程栈区」 |
| M3-4 | ✅ | fork/exit/execve 适配（exit 先切回内核页目录再释放地址空间）；`boot/head.s` 建 4 张内核页表把恒等映射扩到 16MB；QEMU 统一 `-m 16M` |
| M3-5 | ✅ | 新回归场景 16 `cow`（子进程写对父进程不可见）、17 `demand`（384KB BSS/堆首访为 0）、18 `oom`（池耗尽只杀肇事进程，内核存活）；`memstat` 暴露空闲页/缺页/COW/OOM 计数 |

**M3 顺带修掉的两个老 bug**：

1. `mm/page.s` 的 `page_fault` **从不丢弃 CPU 压入的错误码**：以前 `do_no_page` 从不返回
   （一律杀进程），所以 iret 少弹一个字的错误永远暴露不出来；一旦开始按需调页/COW 就会
   iret 到错误码上然后 #GP 死循环。现在返回前 `add $4,%esp`。
2. `mm/page.s` 读取错误码的偏移写成了 `0x34`（两次 push 之后应为 `0x38`），
   传给 `do_no_page` 的「错误码」其实是 eip。

**M3 让 M2-2 的遗留闭环**：Ring3 `/bin/sh` 现在可以真正 fork+execve 子程序
（回归场景 14 `usersh` 默认跑），因为子进程的 execve 不再写进父进程正在执行的物理页。

## M4 — 文件权限模型 + 映像余量 ✅ 已完成

**动因**：`i_mode`/`i_uid`/`i_gid` 从第一天就存在，`chmod`/`chown`/`setuid` 也都能改它们，
但**从来没有一处代码读过**——`sys_access()` 的注释直说了「Simplest permission model:
existence check (all tasks are uid 0)」。只要系统里只有 uid 0，这个问题就不出现；
一旦有程序 `setuid()` 降权（`user/sysdemo.c` 早就在做），文件系统就必须回答「谁能做什么」。

| 项 | 内容 |
|----|------|
| 判定 | `permission(inode, mask)` + `suser()`（`fs/namei.c`，规则照搬 Linux 0.01）：euid==i_uid 取 owner 三位、egid==i_gid 取 group 三位、否则 other 三位；root 覆盖一切；**已删除的 inode（nlinks==0）连 root 都不给**。掩码 `MAY_EXEC/WRITE/READ` = 1/2/4，与 0.01 相同 |
| 检查点 | `open`（按 O_RDONLY/O_WRONLY/O_RDWR/O_TRUNC 要 r 或 w）、`execve`（要 x）、`access`（真查 R/W/X，`F_OK` 仍只查存在）、`chdir`（目录要 x）、`creat`/`mkdir`/`unlink`/`rmdir`/`link`/`rename`（**父目录要 w**）、`chmod`（仅 owner 或 root）、`chown`（仅 root）、`utime`（owner 或可写） |
| 路径遍历 | `namei()` 里每一级**被穿过**的目录都要 x（0.01 的 `dir_namei` 语义）：0700 目录对别人等于不存在，`stat`/`open`/`chdir` 全部 `-1`，连文件名都探测不到 |
| 归属 | 新建文件/目录的 `i_uid/i_gid` 取 `current->euid/egid`（旧代码写死 0，等于"所有文件都是 root 的"） |
| 回归 | 场景 19 `perm`（`user/permtest.c`）：两个 `setuid(1000)` 子进程，A 阶段 16 项越权操作必须全被拒、B 阶段 root 放宽后 11 项必须成功，且自己建的文件 uid==1000；任一方向反了都会打印 `*** FAILED` 并让断言失败 |
| 顺带（D） | `KERNEL_IMAGE_LIMIT`/`KERNEL_HEAP_START` 从 `0x2B000` 抬到 `0x30000`（`_end` 0x29E68，余量从 4.5KB 变成 25KB）——加功能前不必再和天花板搏斗 |
| 兼容性调整 | `user/sysdemo.c` 原来在主进程里 `setuid(7)` 然后 `setuid(0)`：降权是**单向**的，第二句本来就失败，只是以前没人检查权限所以看不出来。现在把 uid 实验放进 fork 出的子进程，父进程继续以 root 演示 `chmod/chown` |

## C1 — Ring3 shell 的管道与重定向 ✅ 已完成

**动因**：`pipe()`（syscall 42）和 `dup2()`（63）从 0.01 对齐那一轮就实现了，`user/pipedemo.c`
也一直在用——但**没有任何东西用它们把两个进程接起来**。Ring3 `/bin/sh` 只能跑单条命令，
`>`、`<`、`|` 一个都不认。

**先修的其实是内核**：`sys_read`/`sys_write` 把 fd 0/1/2 硬编码成控制台
（`if (fd == 1 || fd == 2) { ...tty... }`），**从不查 fd 表**。这意味着 shell 无论怎么
`dup2(fd, 1)` 都没用——内核照旧往屏幕上写。真实 Unix 里 0/1/2 只是"在 tty 上打开的文件"，
所以这一轮把它们变成真正的描述符：

| 改动 | 内容 |
|------|------|
| `drivers/tty_io.c` | 新增 `struct file tty_file`（`f_inode == NULL` 就是"这是控制台"的标记） |
| `kernel/sched.c` | `sched_init` 给 `init_task.filp[0..2]` 装上它；fork 的 `f_count++` 与 exit 的关闭照常生效 |
| `kernel/sys.c` | `sys_read`/`sys_write` 先查 `current->filp[fd]`：没有描述符或 `f_inode == NULL` → 走 tty 分支，否则走文件/管道分支。`close`/`dup2`/`exit` 里 `iput(f->f_inode)` 加 NULL 守卫（控制台没有 inode 可释放） |
| 效果 | `dup2(file_fd, 1)` 之后 `write(1, ...)` 真的写进文件；同理 fd 0 可以被重定向到文件或管道读端 |

**shell 侧**（`user/sh.c` 重写）：`line := pipeline`、`pipeline := command ('|' command)*`、
`command := word+ redirection*`，支持 `<`、`>`、`>>`、最多 4 段管道。实现在 fork 出的子进程里
`dup2` 接线，父进程只保留管道两端并在最后回收所有子进程（退出码取最后一段）。
内建命令（`echo`/`cd`/`pwd`/`help`/`exit`）单独跑时在 shell 进程内执行（`cd` 才能生效），
重定向通过"保存 fd → 应用 → 恢复"实现；内建出现在管道里则在子进程中执行，
所以 `echo hi | wc` 也成立。

顺带：`user/cat.c`、`user/wc.c` 学会 Unix 约定——没有文件名（或 `-`）就读 stdin；
`scripts/qemu-test.py` 补上大写字母与 `| < > ( ) & * ...` 的 sendkey 映射（否则测试根本敲不出管道符）。

**回归**：场景 20 `shpipe`——`echo ... > /p.txt`、`cat /q.txt >> /p.txt`、
`cat /p.txt`、`wc < /p.txt`、`cat /p.txt | wc`，断言数值可手算（`2 4 23 -`）。

## B2 — 硬盘 I/O 中断化 ✅ 已完成

**动因**：`drivers/hd.c` 一直是纯轮询——`hd_wait_drq()` 在 0x1F7 上最多自旋 100000 次，
整个系统陪着等；`hd_interrupt_handler()` 是**空函数、而且永远不可能被调用**，
因为 setup.s 把从片 PIC 整个屏蔽成 0xFF（IRQ8–15 全灭），IRQ14 根本没开。

| 项 | 内容 |
|----|------|
| 打开中断 | `hd_init()`（`main()` 在第一次磁盘访问前调用）把从片掩码 0xFF→0xBF，只放开 IRQ14；主片的级联 IRQ2 本来就是开的 |
| 睡眠等待 | `hd_wait_bits(mask, value, what)`：状态满足即返回，否则把当前任务挂到 `hd_wait` 上 `schedule()`；IRQ14 处理函数 `wake_up(&hd_wait)`。等待期间别的任务可以跑——这正是"为什么要中断"的教科书场景 |
| 不会挂死 | 等待循环同时看 `jiffies` 截止时间（50 tick）和 `schedule()` 的定时器返回：**丢一个中断只是超时**，不是死机。开机路径中断还没开（`sys_setup` 在 `sti()` 之前读超级块），`jiffies` 也不走，此时 `hd_irq_enabled()` 判定为"轮询模式"，退回原来的有界自旋 |
| 加锁 | 任务现在会在驱动里睡着，两个任务同时做磁盘 I/O 会交错发命令——原轮询版没有这个问题（也没处理过）。`hd_lock`/`hd_lock_q` 用 `cli` 保护的测试置位 + `sleep_on(&hd_lock_q)` 把整次操作（发命令 + 传数据）串行化 |
| 顺带修 | `boot/head.s` 的 `hd_interrupt` **EOI 顺序反了**（先主片后从片）。IRQ14 是从片经级联 IRQ2 上来的，必须先给从片 EOI 再给主片，否则可能丢掉挂起的从片中断。以前 IRQ14 全屏蔽，这个错永远暴露不出来 |
| 一处踩坑 | 第一版把"等数据"写成 `(status & (BSY\|DRQ)) == (BSY\|DRQ)`——**错的**：传输期间驱动器是 BSY=0、DRQ=1（原代码的 `!BSY && DRQ` 才对），结果每次读都超时、开机直接找不到根文件系统。等待原语改成 `(status & mask) == value` 后正常 |
| 观测 | `memstat` 增加 `mem: N disk interrupts (IRQ14)`，能直接看到中断真的在发生（开机读出超级块 + `ls` + `cat` 之后是 10 次） |

## B3 — 按需调页的另外半截：页回收 ✅ 已完成

**动因**：M3 的按需调页只解决了"什么时候给页"，没解决"页不够时怎么办"——池子一空就只能
OOM 杀进程。而且 `execve` 仍然是**预先**把整个镜像拷进新页，缺页处理只服务 BSS/堆/栈。

**两部分**：

| 项 | 内容 |
|----|------|
| ① 文件成为镜像页的后备存储 | `execve` 不再拷贝 LOAD 段，只把段的位置记进 `task_struct.exe_regions[]`（va / file_off / filesz / memsz / ELF flags），并持有可执行文件 inode（`exe_inode`，fork 继承、exit iput）。第一次取指就缺页，`page_in_image()` 用 `file_read()` 把这一页从磁盘读进新帧——`file_read` 走平坦的 FS 段写目标，所以直接写物理页即可，不需要任何用户映射。`memstat` 显示 `N image pages read back from the executable`（跑一次 `exec /bin/hello` 是 2 页） |
| ② 回收器 `try_to_free_page()` | `get_free_page()` 在池子空时先尝试回收，**优先选真正能释放帧的页**：全零页（丢弃，下次访问重新变成零页）、只读镜像页（丢弃，下次取指从文件读回）；写时复制的共享页只解除本进程映射（帧留给另一个 owner，`mem_count` 递减），并且**只作为兜底**——因为它不产生空闲帧。私有脏堆/栈页没有后备存储（本内核没有交换区），一律不碰 |
| 扫描范围 | 遍历 `task[]` 里每个进程的用户页表（申请不到页的进程往往不是持有可回收页的那个），游标轮转避免总挑同一个 |
| 观测 | `memstat` 增加 `N pages evicted, M COW mappings dropped`，前 10 次回收打印一行 `evict: zero/text page 0x... from pid=N` |

**踩坑（值得记下来）**：第一版把"COW 共享页"当成首选回收对象，于是每次分配都消耗掉一次
回收、却**一个空闲帧都没换到**，`get_free_page()` 重扫仍然为空 → 6 个子进程全部卡死、
父进程的管道屏障永远等不到（4MB 下 25 秒零进展）。改成"能释放帧的优先、COW 解除映射兜底"
之后，约 1280 页的需求稳稳跑在 695 个帧上。

**另一个顺手修掉的真隐患**：缺页分配原先"先建映射、后填内容"，而填充（从磁盘读）会睡眠——
这期间那一页在回收器眼里就是"全零的只读正文页"，可能被别的进程的缺页抢走并复用，导致
写进别人的帧。现在改成 `get_free_page()` → 填充 → `map_user_page()`，未映射的帧对回收器不可见。

**回归**：场景 22 `evict`（`QEMU_MEM=4M`）：父进程 + 6 个子进程各占 512KB 零 BSS + 256KB
私有堆，子进程触碰完用管道屏障驻留造成真实峰值，父进程随后校验自己的数据再杀掉它们。
断言 6 个子进程都活着、PASS、退出码 0——没有回收时这里会 OOM 杀进程，这三条都不会出现。

## B2' — 信号投递时机：中断返回路径 ✅ 已完成

**动因**：`do_signal` 只在 `ret_from_sys_call`（系统调用返回）被调用。于是**再也不进内核的
进程杀不掉**：纯计算循环收不到 SIGKILL，`alarm` 也不会响。这不是理论问题——B3 的内存压力
测试正是被它卡住的：卡在"纯内存写"缺页循环里的子进程，`kill` 和 `alarm` 都得等它下次做
系统调用才生效（当时只能改测试绕开）。

| 项 | 内容 |
|----|------|
| 改动 | `boot/head.s` 的 `timer_interrupt` 在 `call do_timer` 之后、`popal` 之前，把中断帧指针交给新函数；`kernel/process.c` 的 `do_signal_from_intr()` 负责适配 |
| 为什么要"适配" | 两个入口的帧**顺序不同**：`system_call` 按自定义顺序压栈（ebx 在最低地址），`timer_interrupt` 用 `pushal`（edi 在最低地址）。适配器把中断帧拷进一个"规范化"的 16 字缓冲（布局与 syscall 帧一致），交给 `do_signal()`，再把可能被改写的两个字段（eip、用户 esp）写回 |
| 安全性 | 只对 `cs & 3 == 3`（Ring3 上下文）投递；Ring0 中断帧没有 ss/esp，读了就是垃圾。定时中断里做 `sys_exit`/`schedule` 在本内核本来就是既有行为（`do_timer` 也会切任务） |
| 效果 | 最坏一个 tick（10ms）送达。Linux 正是这么做的（`do_signal` 同时挂在 `ret_from_intr` 与 `ret_from_sys_call` 上） |
| 回归 | 场景 23 `spinkill`（`user/spintest.c`）：`alarm(1)` 之后进入**不含任何系统调用**的死循环，必须约 1 秒后以 142（128+SIGALRM）退出。**反向验证过**：把补丁 stash 掉重跑，进程永远转下去、连 `exit_code` 都不出现 |
| 顺带 | `user/evicttest.c` 里那条"本内核只在系统调用返回时投递信号"的注释同步更新；它循环里的周期性 `times()` 保留（让长循环更早可中断），但不再是"能不能被杀掉"的前提 |

### B5 — 信号屏蔽与 `sigsuspend`：第 1 步 ✅（`sigaction` 待做）

**动因**：临界区必须能把信号挡住再放开，否则"检查标志 → 改数据"随时会被处理器插进来；而
`sigsuspend` 是"原子地换掩码并睡觉"的唯一办法，没有它只能忙等 + 轮询标志。

| 项 | 内容 |
|----|------|
| 状态存放 | `sig_blocked[NR_TASKS]`（按 pid 索引）**故意不放进 `struct task_struct`**：结构体躺在任务页底部，子进程的内核栈就是它上面剩下的空间，`sys_fork` 要把父进程的**活栈**拷进去。三次尝试（每次只给结构体加几到几百字节）都以静默三重故障/重启告终——`sizeof(struct task_struct)` 只有 712 字节，空间是被"父进程活栈"吃掉的。现在 `sys_fork` 先算 `sizeof(struct task_struct) + 活栈` 装不装得下，装不下就打印原因并让 fork 失败，而不是越界 memcpy |
| 继承 | fork 时 `sig_blocked[child] = sig_blocked[parent]`；槽位复用时这条赋值也顺便把上一个进程的掩码覆盖掉 |
| 投递 | `do_signal` 遇到被阻塞的信号**不清 pending 位**：解除阻塞的那次 `sigprocmask` 返回时就地投递，不需要补发信号 |
| `sigsuspend` | 临时掩码一直生效到进入 handler 为止（POSIX：唤醒它的处理器必须在 `sigsuspend` 返回**之前**跑完），旧掩码的恢复推迟到 `do_signal` 里做；`pause()` 也不再把被阻塞的信号当成"有事发生" |
| SIGKILL | 唯一不可屏蔽的信号（本内核没有作业控制，也就没有 SIGSTOP/SIGCONT） |
| 顺带修的老 bug | `sys_sigreturn` 曾以 `movl $0,%eax` 返回，而 `ret_from_sys_call` 会把 eax 存回"被中断系统调用的返回值槽"——于是**每次从 handler 返回，被打断的系统调用的返回值都被清成 0**。`sigdemo` 只检查 `pause()`（返回值没人看），所以这个 bug 一直藏着；现在返回恢复出来的 eax |
| 回归 | 场景 25 `sigblock`（`user/sigblock.c`）：阻塞 → 给自己发信号（handler 不得运行）→ 解除阻塞（pending 信号立刻送达）→ 子进程发信号 + `sigsuspend`（返回 -1、hits=2、掩码随后恢复）→ SIGKILL 仍不可屏蔽 |

**还没做**：`sigaction`（持久处理器 + `sa_mask` 在处理器运行期间屏蔽自身）、可重启的系统调用
（`SA_RESTART`），以及把投递点补到缺页返回路径上（现在靠 tick 兜底，最坏 10ms 延迟）。

## B5.5 — 一次丢失的唤醒：`bh->b_wait` 从来没有人叫过 ✅ 已修复

**症状**（做 B5 时撞上，早于 B5）：批量 fork 出来的子进程**成批卡死**。最小复现
`user/oomdiag.c`（父进程 fork 3 个子进程，每个 `malloc(768KB)`、填满 192 页、`exit(7)`，
父进程 `waitpid(-1)` 回收 3 次）：子进程都打印了第一行，但通常只有最后一个跑完，其余永远
停在 `state=2`（`TASK_UNINTERRUPTIBLE`），父进程只回收到 1 个后永久阻塞。`memstat` 显示
0 swap、0 eviction、0 OOM、内存大量空闲——所以不是缺内存。

**定位过程**（可复用的手法）：

1. `git worktree add <dir> 06183d2` 单独构建 B5 之前的提交做对照，输出逐字相同 ⇒ 排除
   "是 B5 引入的"（**不要**用 stash 在同一棵树里来回切，见下面"构建产物的坑"）；
2. 在 `do_timer()` 里临时打印每个 task 的 `state/counter/signal`（每 5s 一次）⇒ 卡住的
   子进程是 `state=2`，即睡在 `sleep_on()` 里，而不是在用户态空转；
3. 全内核设置 `TASK_UNINTERRUPTIBLE` 的只有三处：`hd_lock_q`（磁盘锁）、`bh->b_wait`
   （缓冲头）、管道 `i_wait`；再 `grep b_wait` ⇒ **`wake_up(&bh->b_wait)` 一次都没出现过**
   （只有 `docs/PREREQ-*.md` 里教了这个模式——文档比代码正确）。

**根因**：`ll_rw_block()` 在本内核里是**同步**的——`hd_read_sectors()` 自己 `sleep_on`
等 IRQ14，返回时 I/O 已经做完。Linux 0.01 的异步块层由请求完成路径（`end_request`）负责
`wake_up(&bh->b_wait)`；这里既然同步，那个唤醒点就落到了 `ll_rw_block()` 末尾，而它被漏掉。
于是 `bread()` / `wait_on_buffer()` 中"发现缓冲头被锁 → 睡到 b_wait"的任务**永远醒不过来**：
第二个读同一块的任务必死，而且睡得不可中断。

**为什么子进程最容易踩**：子进程会执行父进程**没碰过**的代码页（`malloc`/`printf` 内部
路径），这些页是文件后备的 ⇒ 缺页走 `page_in_image()` ⇒ `bread()`。多个子进程同时缺同一个
可执行文件的同一块，第二个就睡死。因此场景 18（`oom`）"10 个子进程各持有 768KB"的前提从来
没成立过：内存压力根本没出现（`memstat` 只有 422 次缺页、0 次换出、0 次 OOM）。

**修复**（`fs/buffer.c`）：

- `ll_rw_block()` 末尾 `bh->b_lock = 0;` 之后补 `wake_up(&bh->b_wait);`——同步块层的完成点
  就是这里；
- `bread()` 与 `wait_on_buffer()` 的"判断-注册"之间加 `cli()/sti()`（Linux 0.01 自己的写法）：
  否则完成中断可能正好落在"查到锁"和"登记为等待者"之间，唤醒照样丢。

**效果**：同一份 `oomtest` 在 4MB 上从"422 次缺页、0 换出、0 OOM"变成 **9803 次缺页、
8908 页回收、525 页换出、1 次 OOM**（`PAGE FAULT: out of memory for pid=11`）——场景 18 终于
在真的测内存耗尽，而不是在测"子进程睡死"。

**顺带暴露的下一步（重要）**：压力真正上来之后，日志里开始出现用户进程"跳到 0"的缺页：

```
PAGE FAULT: addr=0x0 err=0x5 eip=0x0 pid=4        ← 在 0 处取指
PAGE FAULT: addr=0x0 err=0x7 eip=0x8001219 pid=5  ← 用 0 指针写（代码本身还在正文里）
  pde[0x0]=0x101023 pte[0x0]=0x63
```

`err` 的 P 位是 1 ⇒ 地址 0 **是映射的**（内核恒等映射 PDE[0]，PTE 无 U/S 位），用户态访问它
才触发保护异常。所以不是"页表坏了"，而是**用户进程真的拿到了 0**：从 pid=5 那条看，程序在
正常正文地址上执行、却对一个 NULL 指针写——说明**某个用户页（栈或数据）在回收/换入之后变成
了全零**，栈上的返回地址或指针因此成了 0。

这是**新暴露**的问题：只有页回收/换出真正发生时才走得到（修好 `b_wait` 之前，机器从没走到
过 9803 次缺页这个量级）。怀疑方向，按可能性排序：

1. `try_to_free_page()` 的"全零页"识别：读页判断是否全零时，若该页已被换出/已被别人复用，
   可能把**有数据的页**当成零页收走；
2. 换出后帧被复用，但原属主的 PTE 仍然 present（换出路径没有原子地改写属主页表项）；
3. 换入时槽号/页表项解析错误，读回的内容实际是零区。

建议做法：给 `try_to_free_page()` 加一条"只回收 PTE 仍指向该帧、且 present"的断言（或让
`memstat` 打印 `nr_oom` 与换出/换入计数），再用 4MB 的 `oom`/`swap` 场景复现；这类 bug 用
计数器比用眼睛快得多。

**构建产物的坑**（这次浪费了不少时间，务必记住）：工具超时杀掉的 `make` 会留下**半截
`Image`**，而之后 `make` 因为时间戳更新而认为"无需重建"——于是出现"改了几行内核就完全
不启动/子进程全卡死"的假象。判据：完整的 `Image` 必须是 **1474560 字节**；怀疑构建时直接
`rm -f Image && make`。

## B4 — 匿名页换出（swap）：内存回收的最后一块 ✅ 已完成

**动因**：B3 之后能回收的是零页、只读正文页、COW 共享页——**程序真正吃内存的私有脏页
（堆/栈）没有后备存储**，池子一空就只能 OOM 杀进程。补上它，按需调页才是一套完整的内存
管理器。

| 项 | 内容 |
|----|------|
| 交换区 | 镜像布局 `[MINIX fs 1MB][raw swap 2MB]`：`tools/mkminix.c` 在文件系统之后追加归零区域，内核按 `SWAP_START_LBA` 常量直接读写 LBA——**裸设备而非文件**，回收路径不需要缓冲区/inode；512 槽 × 4KB |
| 页表编码 | present=0 + bit11 标记 + 槽号在地址高位：CPU 忽略非存在项的其他位，于是"这页在哪"完全记在页表项里，不需要每页的额外记账 |
| 回收顺序 | 全零页（memset）→ 只读正文页（回读文件）→ **私有脏页换出**（写盘）→ COW 共享页（只解映射，兜底） |
| 缺页换入 | `do_no_page` 先看页表项：是 swap 项就分配帧、从槽读回、归还槽位、建映射；换入失败才杀进程 |
| fork | `copy_page_tables` 遇到已换出的页**先换入再 COW 共享**——否则子进程会把它当成"从未访问"而拿到零页（最容易漏的一处） |
| 退出 | `free_page_tables` 归还槽位（`memstat` 的 `slots free` 回到 512 就是这条的回归） |
| 观测 | `memstat` 增加 `pages swapped out / swapped back in / slots free` |

**过程中踩到并修掉的两个真 bug**：

1. **换出时睡眠导致的 use-after-free**。`swap_out_page()` 要写盘、会睡眠，而回收器正拿着
   **别的进程的页表指针**——睡着的这段时间里那个进程可能退出、地址空间被 `free_user_space`
   释放，醒来继续用 `pt[i]` / `t->pid` / `free_page(pa)` 就是 use-after-free。症状是一条内核
   页错误 `PAGE FAULT: addr=0xffffffff ... pid=-268370093`（垃圾任务指针）。修法：换出前记下
   任务槽位与 `pg_dir`，睡醒后先验证 `task[idx] == t && t->pg_dir == pgdir_before`，不成立
   就归还槽位、放弃这一轮（内容成为孤儿，不碰任何陈旧指针）。
   > B3 的所有回收路径都不睡眠，所以这个坑在 B4 之前根本不存在。
2. **抖动（thrashing）**。修掉 1 之后仍然不收敛：零页与正文页耗尽后，回收器只能挑私有脏页
   换出，而进程立刻又访问同一批页把它们换回来——时间全花在来回搬同一批页上，谁也不前进。
   修法是教科书里的**第二次机会/时钟算法**：页表项的 Accessed 位（硬件每次访问自动置位）
   就是现成的引用位，回收器遇到 A=1 的页清位并跳过，只挑 A=0 的换出；扫完一轮仍找不到就
   再扫一轮、不再给机会（保证有界）。实测换回次数 49→28，场景由"不收敛"变为 2/2 稳定通过。

**回归**：场景 24 `swap`（`QEMU_MEM=4M`）：3 个子进程各填 768KB 堆后持有（`alarm` 自行
退场，142=128+SIGALRM；被 OOM 杀会是 139，测试直接 FAIL），父进程在它们持有期间再填
768KB——峰值超过物理内存，必须靠换出撑住，最终逐字节校验数据。`oom` 场景同步重调：有
swap 后耗尽门槛是**内存+swap**（695 帧 + 512 槽 ≈ 1207 页），子进程数 6→10（1920 页需求）
才越得过去。


---

## 当前状态（一句话）

**67 个系统调用（编号与 1991 Linux 0.01 完全一致）＋ 3 个本内核扩展（67 sigreturn /
68 sigprocmask / 69 sigsuspend）**、23 条 Shell 命令的教学内核：
进程生命周期完整（fork/execve/waitpid/信号/管道）、MINIX FS 增删改查 + 硬链接/重命名 +
**权限模型**、
Ring3 用户态 + 编程工具链（`make prog NAME=xxx` → `exec /xxx`）、内存隔离、chdir。

## Linux 0.01 功能对齐（`39f1b72`→`5426b8b`）

- **系统调用编号 0-66 与 Linux 0.01 的 sys_call_table 完全一致**（unistd.h 同步）
- 新增实现：creat、link（硬链接）、rename、stat/fstat、chmod/chown、access、
  umask、uname、stime、utime、setuid/getuid/setgid/getgid/geteuid/getegid、
  alarm（SIGALRM）、nice、times、setpgid/getpgrp/setsid、chroot、fcntl（F_DUPFD）、
  brk、**pipe（管道，0.01 fs/pipe.c 移植）**
- open 改 3 参数（flag/mode + O_CREAT/O_TRUNC + umask）
- stub 保留 -1 的：break/mount/umount/ptrace/stty/gtty/ftime/prof/acct/phys/
  lock/mpx/ulimit/ustat/ioctl —— **与 Linux 0.01 自身 -ENOSYS 完全一致**
- 比 0.01 强：mknod/rename/chroot 是 0.01 的 stub，我们已真实现；有内存隔离
- 验证：user/sysdemo.c、user/pipedemo.c + shell ln/mv/stat/id 命令

## 后续功能清单（按优先级）

### 1. ~~printf 增强（user/lib.c）~~ ✅ 完成（`e8681e6`）
- 已补：`%ld/%lu/%lx`（long 修饰符）、精度 `%.d`（数字补零 / 字符串截断）、
  宽度 `%Ns`、左对齐 `%-`、`%#x/%#o` 前缀（`0x`/`0`）
- 验证：`user/printf.c` 演示程序（`make prog NAME=printf` → `exec /bin/printf`），回归全过

### 2. ~~readdir 便捷接口（用户库）~~ ✅ 完成（`35ad6e2`）
- `lib.h` 提供 `struct dirent`（d_ino + d_name[15]）、`DIR`、`opendir/readdir/closedir`
- 选择**用户库封装**而非新增 `sys_getdents`（syscall 编号保持与 Linux 0.01 一致，更简单）
- readdir 内部读 16 字节目录项、跳过 ino==0 空槽、name 复制为 NUL 结尾
- 示例：`user/ls.c`（`make prog NAME=ls` → `exec /bin/ls` / `exec /bin/ls /docs`），QEMU 验证通过

### 3. ~~更多 libc（user/lib.c）~~ ✅ 完成（`c483d76`）
- `atoi/strtol`（支持 base 0/2..36、前导空白、符号、0x/0 前缀、endptr 停靠点）
- 字符串函数副本（与内核 lib/string.c 一致）：strcpy/strncpy/strcmp/strncmp/strcat/
  strlen/strchr/strrchr/memcpy/memset/memcmp/memmove
- ctype 副本：isdigit/isspace/isalpha/isalnum/isupper/islower/tolower/toupper
- 全部声明在 user/lib.h（用户 include lib.h 即可，无需 include <string.h>）
- 示例：`user/str.c`（`make prog NAME=str` → `exec /bin/str`），QEMU 验证通过

### 4. ~~SIGCHLD 完整语义（内核）~~ ✅ 完成（`4aca6df`）
- 新增 **syscall 22 `signal(sig, handler)`**（SIG_DFL/SIG_IGN，SIGKILL 不可忽略；
  fork 继承 dispositions——`*p = *current`）
  > M2 更新：`signal()` 现已支持**自定义处理器**（Ring3 函数指针，越界地址被拒），
  > 通过 `sigreturn`（syscall 67）恢复被中断的上下文；dispositions 存于
  > `task_struct.handlers[32]`（取代旧的 `sig_ignore_mask`）。
- `signal(SIGCHLD, SIG_IGN)`：waitpid 立即返回 -1（ECHILD）；子进程 exit 时
  不通知父（不置 SIGCHLD 位/不唤醒）；**调度器 schedule() 自动回收**这些僵尸
  （也顺带清理孤儿僵尸——本内核无收养，父退出后僵尸无人 reap 的问题一并解决；
  清理循环必须跳过 current：sys_exit 正在 schedule() 让出，页不能提前释放）
- 演示：`user/sigchld.c` 三阶段（忽略→ECHILD+自动回收 / 默认→waitpid(-1) /
  WNOHANG→0 非阻塞），QEMU 验证通过
- ⚠️ 教训：head.s system_call 的 syscall 号上限检查 `cmpl $22,%eax; jb` 要同步
  放宽到 23，否则新 syscall 一律返回 -1

### 5. ~~内存隔离（内核，大工程）~~ ✅ 完成（`795364a`）
> **⚠️ M3 已被取代**：下面这套「单页表 + `grant_user_pages` 改 U/S 位」的做法在 M3 整体删除，
> 现在是「内核恒等映射全 0x03 + 每进程私有页表里的 PTE=0x07」。保留这段是因为其中的
> PTE 标志教训（0x06 缺 P 位）和越权处理的行为仍然成立。
- 页表 0 全部 PTE 由 0x07 改 **0x03（P+RW，无 U/S）** —— 0-4MB 默认内核专属
  （⚠️ 教训：0x06=0b110 没有 P 位，会整页 not-present，曾致启动即崩；
   内核页标志是 0x03，不是 0x06）
- `grant_user_pages(from,size)`（mm/memory.c，M3 已删除）：按页把 PTE 置 U/S 位
  （|= 4 → 0x07），并重载 CR3 刷 TLB
- 授权区域（M3 前）：启动时（main.c）堆+栈 [0x310000, 0x400000)；execve 时按
  ELF 段尾授权程序区 [0x200000, max_end)；run_user_program 同
- **效果**：Ring3 只能访问程序/堆/栈页；内核页（含 buffer cache、
  任务页、页表）用户不可访问；越权访问 → page fault → 终止肇事进程
- 验证：`user/bad.c` 读 0x0 → `PAGE FAULT` → 肇事进程 SIGSEGV 终止（exit 139），
  内核继续运行（不再 panic）；hello/printf/ls/catfile/memtest/sigchld + 内建命令全回归

### 6. ~~chdir / 相对路径（内核）~~ ✅ 完成（`81c7f1d`）
- task_struct 加 `struct m_inode *pwd`；init（sched_init 里 FS 挂载后）pwd=根
  inode；fork 继承（`p->pwd->i_count++` 共享引用）；sys_exit iput 释放
- **syscall 23 `chdir(path)`**：namei 解析（相对旧 pwd）、非目录返回 -1、
  iput 旧 pwd 换新
- namei 支持相对路径（非 `/` 开头从 current->pwd 起步，walk 期间多持一
  引用，iput 平衡）；`""`=当前目录；`.` 特判（`./x`、`.`）
- split_path 的 bare name 由 `/`（根）改为 `""`（当前目录）→ touch/mkdir/
  rm/rmdir 相对路径生效
- **mkminix 目录补 `.`/`..` 项**（root 6 项、docs 3 项、注入后 size 刷新
  +2）——否则 `cd /docs` 后 `cd ..` 失败（基础目录无 .. 项）
- shell 加 `cd` 命令；`ls` 默认当前目录
- 验证：`cd /docs`→`ls`→`cd ..`、`touch x`/`rm x` 相对创建删除、
  `mkdir sub`→`cd sub`（. / .. 指向正确）→`rmdir sub`、`exec /bin/ls .` 全通

## 工具链速查（继续工作必备）

```bash
# 构建
make                      # 内核（i686-elf 交叉工具链）
make minix.img            # MINIX 测试盘（注入 hello + 参数程序）
make prog NAME=xxx        # 编译 user/xxx.c 并注入
make Image                # 引导镜像

# 运行/验证
qemu-system-i386 -fda Image -hda minix.img -m 16M -boot a
python3 scripts/qemu-test.py --image Image --hda minix.img --keys $'cmd\n'
make test                   # 一键回归（scripts/regress.sh，25 个场景断言）
make check                  # 静态校验：内存地图 + 文档一致性 + lint 反向自测
make check-layout           # 只校验内存地图（含 _end 未越界）
make check-docs             # 只校验文档里引用的布局常量/场景数与源码一致
make check-docs-selftest    # 反向测试 check-docs（8 个坏样本必须被拦下）
# 注意：QEMU writeback 会把测试中的脏块刷进 minix.img —— 测试前 rm -f minix.img && make minix.img
#       （make test 每个场景自动重建干净盘；定时回写已实现，场景 15 专门验证它）

# 用户程序写法
# user/xxx.c: #include "lib.h"; int main(int argc, char *argv[]) {...}
# make prog NAME=xxx 每次重建 minix.img（含 /hello + /xxx）；多程序用
#   tools/mkminix minix.img user/a.elf:a user/b.elf:b 一次性注入
```

## 已知限制 / 注意事项

1. **内存隔离已实现**：内核页 supervisor-only；用户仅可访问程序/堆/栈页
   （越权访问 → SIGSEGV 终止肇事进程）
2. **内存地图唯一来源**：`include/memlayout.h`（汇编镜像 `include/memlayout.inc`）；
   写死用户区地址会被 `make check-layout` 拦下（见 M1）
3. **chdir 已支持**（syscall 23）；`..` 依赖目录的 `..` 项（mkminix 已写入；
   `mkdir` 建的目录自带 . / ..）
4. **自定义信号处理器已实现**（M2-1）—— 默认动作 + 用户态 handler（`sigreturn` 精确恢复上下文）
5. **目录已支持扩容** —— >64 项时自动分配单间接块（`ensure_dir_block`；
   `rmdir` 释放全部目录 zone）。多级间接（>519 项）不支持。
6. **用户栈**：顶 `USER_STACK_TOP`(0x083FF000)，crt 从 `USER_ARGC_ADDR`/`USER_ARGV_PTR_ADDR`
   读 argc/argv（execve 约定）；栈页按需增长
7. **用户堆**：`[USER_HEAP_START, USER_HEAP_END)` = `[0x08100000,0x08200000)`（lib.c，1MB）
8. **程序链接地址**：`USER_PROG_START`(0x08000000)，固定无 PIE；
   `user/lib.h` 里有链接地址断言（链错地址 = 编译失败）
9. **fork 已是 POSIX 语义**（M3）：用户页父子共享只读 + 写时复制，子进程的写父进程看不见；
   地址空间彼此独立，子进程 execve 不再影响父进程
10. **printf %s 需 NUL 终止**（read 后手动补）

## 关键文件地图

| 文件 | 作用 |
|------|------|
| `include/memlayout.h` | ★ 内存地图唯一权威（内核+用户态共用） |
| `include/linux/memmap.h` | 内核侧边界 + `STATIC_ASSERT` 布局自检 |
| `include/memlayout.inc` | 汇编侧 `.equ` 镜像（与上面同步校验） |
| `mm/memcheck.c` | `mem_check()`：启动自检，不一致即 panic + 地图 dump |
| `scripts/check-layout.py` | 无编译器的静态地图校验（`make check-layout`） |
| `scripts/check-docs.py` | 文档 vs 内存地图一致性校验（`make check-docs`）；`scripts/check-docs-selftest.sh` 反向测试它 |
| `kernel/sys.c` | 系统调用实现（含 sys_execve） |
| `kernel/process.c` | fork/waitpid/exit/do_signal |
| `boot/head.s` | system_call 入口（syscall_cpl 检测）、sys_call_table、setup_paging（4 张内核页表恒等映射 0–16MB） |
| `mm/memory.c` | ★ 页帧分配器 + 每进程页目录 + 按需调页 + COW（`copy_page_tables`/`un_wp_page`）+ `mm_report()` |
| `mm/page.s` | page_fault 处理（do_no_page；记得它也负责丢弃错误码） |
| `fs/*` | MINIX FS（inode.c 的 iget/read_inode 有历史 bug 修复记录） |
| `init/shell.c` | Shell 命令 + run_user_program |
| `user/lib.h/.c` | 用户态库（printf/malloc/syscall 包装） |
| `user/crt.s` | 用户程序入口（读 `USER_ARGC_ADDR`/`USER_ARGV_ADDR`） |
| `user/hello.c … bigalloc.c` | 示例程序（printf / readdir / libc / SIGCHLD / 隔离 / 堆-缓存不重叠） |
| `tools/mkminix.c` | 镜像制作（`tools/mkminix minix.img prog.elf:name` 注入；目录含 . / ..） |
| `tools/build.c` | 引导镜像拼接 |
| `scripts/qemu-test.py` | 无头回归驱动 |
| `docs/LIMITATIONS.md` | 实现边界（权威：源码 > 本文件） |

## 历史 bug 修复备忘（改相关代码前必读）

- **iget 假 bug**（`5cc7871`）：曾被误判为 inode 缓存缺陷，实为 mkminix 的
  imap 写入顺序（put_inode 必须在 memcpy(imap) 之前）
- **schedule current 预赋值**（`372529c`）：switch_to 的 `cmpl %ecx,current; je`
  要求 current 不能在 schedule 里预先赋值
- **init_task.tss.cr3**（`372529c`）：必须显式设置，否则切回 init 时 CR3=0
- **sys_exit use-after-free**（`372529c`）：zombie 语义（`ac0d136`）后由 waitpid 回收任务页
- **fork 帧**：Ring3 16 项（含 ss/esp）、Ring0 14 项；`child_top` 必须赋值
- **syscall 号保存**：system_call 里 CPL 检测必须在 `push %eax`（保存 syscall 号）之后
- **syscall 上限检查**：head.s `cmpl $N,%eax; jb` 的 N 必须与 sys_call_table 项数
  同步（本次新增 signal=22 时忘了改，新 syscall 全被拦返回 -1）
- **页表标志**（`81c7f1d` 后）：内核页 PTE=0x03（P+RW 无 U/S）；写页表标志时
  0x06=0b110 **没有 P 位**（曾致 0-4MB 全 not-present、启动即崩）；授权用户页
  用 `|= 4`（0x03→0x07）
- **缓冲区缓存压在用户堆上**（M1）：`NR_BUFFERS=512` 时缓存 `[0x370000,0x3F2000)` 与
  用户堆 `[0x310000,0x3FE000)`（M3 前布局）重叠；Ring0 无视 PTE 的 U/S 位 → 用户 malloc 的字节
  就是文件系统块缓冲，**写坏文件系统而毫无提示**。修复：缓存条数由内存地图派生 +
  静态断言 + `mem_check()` 启动自检 + `bigalloc` 回归用例
- **fork 子进程用户栈落进缓存区**（M1）：旧锚点 `0x3E0000` 在缓存范围内，
  Ring3 fork 会把用户栈副本写穿文件系统缓存。修复：独立子进程栈区
  `(0x300000,0x340000]` + 上限检查（放不下则 fork 返回 -1）
- **`lib/malloc.c` 上界错误**（M1）：旧上界 `memory_end - 0x200000` 会把内核 bump 堆
  伸进页分配器池（0x18A000 以上）；现为 `KERNEL_HEAP_END`
- **恒等映射下 `get_free_page` 能撞用户区**（M1）：页分配器从 0x100000 顺序扫描，
  不设上限会走到用户程序镜像所在的物理页；现在越界即 `panic`
