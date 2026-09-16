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

### B5 第 3 步 — `sigaction`：持久处理器 + `sa_mask` ✅（同一提交）

| 项 | 内容 |
|----|------|
| 新系统调用 | 70 `sigaction(sig, act, oldact)`：`struct sigaction { sa_handler, sa_mask, sa_flags }`（`include/signal.h`）。成功返回 0（POSIX），旧动作写进 `oldact` |
| 持久处理器 | `signal()` 的处理器跑之前重置成 `SIG_DFL`（posix 之外的经典语义，`sys_signal` 保持原样）；`sigaction()` 装的处理器**保持有效**：内核按 pid 记一个 `sig_sa[]` 位图，`do_signal` 只对没这一位的信号做重置 |
| `sa_mask` | 处理器执行期间额外屏蔽的信号，按 (任务, 信号) 存 16 位（信号 1..17，即本内核定义的那些）；表 64×18×2 = 2304 字节 BSS，仍然**不进 `task_struct`**（原因见上） |
| 自阻塞 | POSIX 要求"正在处理的信号在处理器期间被屏蔽"，所以进入处理器时 `sig_blocked |= (1<<sig) | sa_mask`，处理器因此不会被自己递归打断；旧掩码由 **`sys_sigreturn` 里的一个 C 钩子**（`sigreturn_restore_mask`）恢复——处理器是通过 sigreturn 系统调用返回的，没有别的 C 时机 |
| 回归 | 场景 26 `sigaction`（`user/sigactiontest.c`）：处理器里给自己发 SIGUSR1 与 SIGUSR2 → 都不能在处理器运行期间投递（`usr2_hits=0`）→ 处理器返回后按序补投（usr1=2, usr2=1）→ 再发一次 SIGUSR1 仍进处理器（usr1=3，证明不用重新安装）→ `sigaction(SIG_DFL)` 之后不再进处理器 |

### B5.7 — `sleep()` 与 `select()`：内核第一次有"到期唤醒" ✅（同一提交）

**动因**：在此之前内核没有"过一会儿叫醒我"的手段。`alarm()` 是靠**投递信号**叫醒任务的，而
"超时"不是信号——一个只想等 1 秒、不想被 SIGALRM 打扰的程序没有别的办法（`pause()` 会一直睡
到有输入，循环查表就是忙等）。

| 项 | 内容 |
|----|------|
| 新机制 | `sleep_deadline[NR_TASKS]`：每个任务一个 jiffies 截止时间，`do_timer()` 到期就把该任务置为 RUNNABLE。仍然**不放进 `task_struct`**（原因见 B5） |
| 系统调用 | 71 `sleep(seconds)`、72 `select(nfds, rfds, wfds, efds, timeout)` |
| `sleep` 语义 | 返回**剩余秒数**（POSIX）：睡满返回 0；被信号打断则返回剩余值，处理器随后在系统调用返回路径上运行 |
| `select` 语义 | fd 集合各是一个 `unsigned long`（`nfds <= 32`）；timeout 以 **tick** 为单位（`NULL` 或 0 = 无限等待），不是 `struct timeval`——本内核没有微秒时钟可填；返回就绪个数，超时返回 0，被信号打断返回 -1 |
| 就绪判定 | 控制台看 `read_cnt`；管道看 `i_zone[0]/i_zone[1]` 的 head/tail（空/满）；普通文件恒为就绪（本内核不会在文件上阻塞） |
| 等待与唤醒 | 控制台输入由键盘中断唤醒 `read_waiter`（注册时 `cli()`，避免 B5.5 那类丢唤醒）；管道睡在自己的 `i_wait` 上；两者都由截止时间兜底。一个任务同时只能挂在一个等待队列上（`sleep_on` 用调用者栈串链），所以 select 只挑"最有关"的那个源 |
| 回归 | 场景 27 `seltest`（`user/seltest.c`）：`sleep(2)` 真的睡够；无人输入时 `select(fd0, 1s)` 返回 0 且清空集合；控制台可写所以 `select(fd1)` 立刻返回 1；`alarm(1)` 打断 `sleep(10)` 后返回 9 且处理器已运行 |

**顺带修**：`init_task` 的位置初始化列表早已落后于 `struct task_struct`（M3 加了 `pg_dir` 与
code/data 组、B3 加了镜像表），gcc 报 `braces around scalar initializer`、注释也指错了成员——
改成 C99 指定初始化（`.priority = 15, .counter = 15`），其余全部零初始化。

**还没做**：可重启的系统调用（`SA_RESTART` 现在只是被接受、不生效），以及把投递点补到缺页
返回路径上（现在靠 tick 兜底，最坏 10ms 延迟）。

### B5.8 — Shell 的后台任务与 `wait` ✅（同一提交）

**做法**：`cmd &` 由**子 shell** 执行——父 shell fork 之后立刻回到提示符，子进程照常调用
`run_pipeline()` 再 `exit()`。这样 `run_pipeline()` / `run_one()` 完全不需要知道"后台"这件
事（真实 shell 也是这么分的）。`&` 在**解析之前**从行尾剥掉，解析器仍然只认"单词 / 重定向 /
管道"。

| 项 | 内容 |
|----|------|
| 记账 | `bg_pid[MAX_BG]`（8 个）；`&` 之后打印 `sh: [pid] running in background` |
| 收尸 | 每次打印提示符前 `waitpid(pid, &code, WNOHANG)` 扫一遍，完成的打印 `sh: [pid] done (status N)` |
| `wait` | 内建命令：`wait` 等所有后台任务，`wait <pid>` 等指定那个。**按 pid 等待**（而不是 `-1`）是关键：否则会吞掉前台命令的退出状态 |
| `sleep` | 内建命令，直接调用 B5.7 的 `sleep()`（系统调用 71），把新的超时能力暴露给使用者 |
| 没有作业控制 | 本内核没有 SIGSTOP/SIGCONT，也没有和终端绑定的进程组，所以后台任务**不能**被挂起或拉回前台；文档与 `help` 都直说 |
| 回归 | 场景 28 `shbg`：`sleep 1`（内建，打印 slept 1 s）、`sleep 2 &`、前台 `echo foreground ran` 在任务结束前就打印、`wait` 收到 `done (status 0)`、shell 正常 `exit` |

> 注意 harness 的断言是"这些行出现过"，**不检查先后顺序**，所以这条场景验证的是功能而不是
> 时序；日志里"前台先跑完、后台后结束"的顺序是肉眼可见的旁证（见 `test-logs/shbg.serial`）。



## 已知问题（已修复）：管道里 `wc` 计数错 + 内核重启 —— 同一个根因

**现象**（做场景 29 时发现，已修）：`cat /p.txt | wc` 报 `1 4 29 -`（应为 `2 4 23 -`），
`cat /p.txt | cat` 会 `sh: /bin/cat: cannot execute` 并让内核重启。

**根因**：管道子进程在 `execve` 失败时**没有退出**，而是掉出 `if (pid == 0)` 继续执行父进程的
管道代码，最终回到 `main()` 变成**第二个 shell**——而它此时的 fd 0/1 已是管道，于是它的输出灌进
管道（被 `wc` 数进去 ⇒ 计数错），它又和第一个 shell 抢控制台（⇒ 内核崩溃）。

**修复**：`run_pipeline()` 与 `run_one()` 在 `execve` 失败后 `exit(1)`（提交 2807046）。

**顺带交付**：`scripts/regress.sh` 新增 `check_no_reboot()`——`run_case`/`run_case2` 在断言之前
统计本次运行的启动横幅条数（单阶段期望 1、两阶段期望 2），多了直接判失败并给注解。从此"崩了
重启却看起来是绿的"不会再溜过去。场景 20 也新增了管道读断言 `cat /q.txt | wc` → `1 1 6 -`。

**未修的残余**：`cat /p.txt | cat` 现在**不再重启**，但第二次并发 exec 同一程序仍会失败
（`namei returned NULL`）——这是 inode 缓存的问题，见文末"最新进展：并发 exec 的根因"一节。

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
   （只有 `docs/prereq/*.md` 里教了这个模式——文档比代码正确）。

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

## B5.6 — 换出途中被双重释放的页：回收器的 PTE 竞态 ✅ 已修复

**症状**：`b_wait` 修好、机器第一次真正跑到内存耗尽之后，日志里开始出现用户进程"跳到 0"：

```
PAGE FAULT: addr=0x0 err=0x5 eip=0x0 pid=4        ← 在 0 处取指
PAGE FAULT: addr=0x0 err=0x7 eip=0x8001219 pid=5  ← 代码本身没问题，是在对 NULL 指针写
  pde[0x0]=0x101023 pte[0x0]=0x63
```

`err` 的 P 位是 1 ⇒ 地址 0 **是映射的**（内核恒等映射 PDE[0]，PTE 无 U/S 位），用户态访问它才
触发保护异常。所以不是"页表坏了"，而是**用户进程真的拿到了 0**——某个用户页（栈/数据）的内
容变成了别人的。

**根因**：`try_to_free_page()` 的 swap 路径先选中一个 present 的 PTE、把页写盘，**之后**才把
PTE 改写成 swap 项；而 `swap_out_page()` 要睡（等磁盘），这期间那个 PTE 一直是 present：

- 属主在这段时间碰了这页 → 硬件置 Accessed/Dirty → 盘上那份已经过期；
- 另一个任务缺页、跑自己的回收 pass，**挑中同一个 PTE** → 两个回收器各装一个 swap 项，并且
  **对同一帧各调用一次 `free_page()`** ⇒ **双重释放** ⇒ 帧同时属于两个属主 ⇒ 数据被对方
  覆盖 ⇒ 栈上的返回地址或指针成了 0。

B4 已经为同一类问题加过**一半**校验（任务是否还活着、`pg_dir` 是否变了——那覆盖的是"属主退出、
页表被回收"），但**没有校验 PTE 本身**。

**修复**（`mm/memory.c`）：睡之前把 PTE 记下来，醒来后要求 `pt[i]` **逐位不变**（硬件只会置
A/D 位，所以"变了"就等于"有人动过这页"），与任务/`pg_dir` 校验并列；不满足就归还槽位、放弃
这次回收，让下一次缺页重试。

**验证**：同一份 `oomtest` 在 4MB 上通过，且 `test-logs/oom.serial` 里 `addr=0x0 err=` 缺页
**从"每次若干个子进程"降到 0**——只剩 6 条真正的 `PAGE FAULT: out of memory for pid=N`；
`make test` **25/25 全过**（M3 以来第一次全绿）。

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

**67 个系统调用（编号与 1991 Linux 0.01 完全一致）＋ 6 个本内核扩展（67 sigreturn /
68 sigprocmask / 69 sigsuspend / 70 sigaction / 71 sleep / 72 select）**、25 条 Shell 命令的教学内核：
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
make disk                 # 单文件自启动镜像 linux.img（内核 + 文件系统同一张盘）

# 运行/验证
qemu-system-i386 -fda Image -hda minix.img -m 16M -boot a
qemu-system-i386 -hda linux.img -m 16M -boot c       # 单文件镜像（等价于 make run-disk）
python3 scripts/qemu-test.py --image Image --hda minix.img --keys $'cmd\n'
python3 scripts/qemu-test.py --disk linux.img --keys $'cmd\n'   # 只挂一张盘
make test                   # 一键回归（scripts/regress.sh，35 个场景断言）
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
| `tools/build.c` | 引导镜像拼接（软盘 `Image`；基准 LBA 显式写 0） |
| `tools/mkdisk.c` | ★ 单文件自启动镜像（算出 fs 基准 LBA → 写进引导扇区 → 拼上文件系统，见 D1） |
| `scripts/qemu-test.py` | 无头回归驱动 |
| `docs/limitations.md` | 实现边界（权威：源码 > 本文件） |

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


---

## P1 — 并发 exec 失败：`iget` 加载竞态 ✅ 已修复（含一次误判的订正）

**症状**：`cat f | cat`（同一个程序被两个进程并发 exec）必然失败，sh 报 `sh: /bin/cat: cannot execute`。
这是当时唯一挡住"日常可用"的正确性缺陷。

**订正上一轮的误判**：上一版把根因写成"inode 槽位被偷用、根 inode 被写成 ino=5/size=1"——那是**读错了
自己的诊断**。当时 `kernel/vsprintf.c` 不支持精度，而且**未识别的说明符不消费参数**，于是同一条 printk
里后续每个 `%d/%ld` 都读错了槽位。那条"证据"
`namei: no entry '%.15s' (len 1072864) in dir ino=5 size=1` 的真实解码是：
`len 1072864` = `name[]` 的**栈地址**、`dir ino=5` = **namelen=5**、`size=1` = **i_num=1（根目录，完全正常）**。
⇒ 不存在"槽位损坏"，也不需要审计 `new_inode`/`iput` 的引用计数。`printk` 的格式能力已补齐
（标志/宽度/精度都会被解析并消费，`%.Ns` 可用），这类"诊断说谎"不会再发生。

**真实根因（一次复现即定位）**：

```
$ cat /p.txt | cat
namei: cannot traverse ino=7 mode=00 (is_dir=0 exec_ok=0, euid=0 uid=0 egid=0 gid=0)
open: namei returned NULL (flag=0x0, inode table 4/64 used)
```

`iget(dev, 7)` 在 `read_inode()`（要睡在磁盘读上）**完成之前**就把槽位（i_dev/i_num/i_count）暴露给了
并发查找；第二个查找命中该槽位，拿到 `i_mode == 0` 的 inode，于是 `namei` 拒绝穿越 `/bin`。
inode 表只用了 **4/64** ⇒ 不是表满；也没有 `hd:` 报错 ⇒ 不是读盘失败。

**修复**（`fs/inode.c`，Linux 0.11 式纪律）：

- 新增 `wait_on_inode()`：`cli()` 包住"判断-注册"，与 `wait_on_buffer` 同款；
- 查找命中后先等 `i_lock`，**复核 `i_dev/i_num` 未变**再 `i_count++`（变了就重扫）；
- 分配路径：`i_lock=1` → `read_inode()` → `i_lock=0` → `wake_up(&inode->i_wait)`；
- `i_wait` 字段此前**从未被初始化**（槽位复用后会解引用垃圾指针 → 三重故障）：现在 `iget` 分配路径与
  `get_empty_inode` 都显式置 `i_wait = NULL`；
- `read_inode()` 读盘失败时 `iget` 返回 NULL，而不是把一个"空壳 inode"交给调用者。

**诊断能力**（这次误判的直接产物）：`iget_used()` 报告 inode 槽占用；`namei` 的**四个** NULL 出口
（起始 inode 取不到 / `find_entry` 未命中 / `iget` 返回 NULL / 无法穿越目录）都有打印，且都带表占用数；
`find_entry` 那条默认静默（`NAMEI_TRACE`），因为 O_CREAT 的"文件不存在"是正常路径。

**回归**：场景 30 `execrace`（`user/execrace.c`）—— 先 fork 4 个子进程，**每个都 execve 同一个**
`/bin/cat`，比 shell 管道确定得多；断言 `execrace: 4/4 children` 与 `PASS`。

## U1 — 默认镜像自带一套用户态工具（可用性）✅ 已完成

**问题**：`make minix.img` 以前只把 `/bin/hello`（mkminix 的默认注入）放进镜像，于是进了 Ring3 shell
（`exec /bin/sh`）之后敲 `ls`/`cat`/`wc` 只会得到 `sh: /bin/ls: cannot execute`：镜像"能启动、不能用"。
从使用者视角看就是"生成出来的 `minix.img` 测试不了"——但镜像本身是好的（默认配置下 Ring0 shell 的
`ls`/`cat`/`wtest` 与 `make test` 全绿），缺的是 `/bin` 里的程序集合。

**改动**：

- `Makefile` 新增 `DEFAULT_USERLAND = ls cat cp grep touch wc sh`，`minix.img` 目标依赖对应的
  `user/*.elf`，并把它们以 `path:name` 注入 `/bin`；
- 注入名用 `$(foreach ...)` 生成，**不能用 `patsubst`**：`$(patsubst %,user/%.elf:%,...)` 只会替换
  替换串里的**第一个** `%`，名字会变成字面量 `%`（实测真的注入成了 `/bin/%`，程序全挤在一个名字上）。
  踩过一次，所以写在这里；
- `tools/mkminix` 里那条注入日志此前写成 `as /<name>`（参数名也叫 `root_zone`），而所有调用方传的都是
  `bin_zone` —— 打印与注释一起订正为 `/bin/<name>`，避免下一个人被误导。

**后续补齐（用户态真正能用的最后两块）**：`ls`/`cat`/`cp`/`wc`/`touch` 有了之后，用户态仍然缺
`mkdir` 和 `rm` —— 内核态 shell 有这两个内建命令，所以过去只有 Ring0 能建/删目录。现在：

- `user/mkdir.c`（系统调用 39）、`user/rm.c`（10 `unlink` / 40 `rmdir`，`-r` 递归）写进默认用户态；
- `user/mv.c`（`link` + `unlink` —— 这个内核没有 `rename` 系统调用，而"同一文件系统内改名"在
  POSIX 下的等价做法正是这两步；目录会明确报错而不是假装成功）、`user/ln.c`（`link` 9）、
  `user/head.c`（逐字节读、读够 N 行就停，所以 `head -n 1 /big.txt` 不会把整个文件拖进内存）；
- `rm` 的判定顺序是刻意的：先 `unlink`（普通文件）、再 `rmdir`（空目录）、都失败且带 `-r` 才
  `opendir`/`readdir` 深度优先删空。**没有 `stat(2)` 可用**（Linux 0.01 就没有这个系统调用），
  所以"这是不是目录"只能从这三个调用的返回值推出来，而不是先查属性；
- 场景 31 因此加了两段断言：`echo nested > /d/f` + `cat /d/f` 证明 **mkdir** 造出的目录真能进出读写；
  删掉 `/c2` 之后 `ls /c2` 必须报 `ls: /c2: cannot open` —— 没有 `stat`，这是从外部观察
  "文件确实被 unlink 了"的唯一办法。

**影响面仅限 `make minix.img`**：场景 prep 一律直接调 `tools/mkminix` 并自带 `path:name`；mkminix 只在
`user/hello.elf` 存在时额外注入 `/bin/hello`，不存在则静默跳过 —— 所以既有场景的镜像内容不变。

**回归**：场景 31 `userland` —— 用默认镜像启动，`exec /bin/sh` 后连跑
`cat` / `wc <` / `cp` / `touch` / `mkdir` / `rm` / `ls /` / `ls /docs`，断言各程序**真实输出**
（`Hello from MINIX v1!`、`3 19 129 -`、`cp: /hello.txt -> /c2 done`、`t3`、`nested`、
`ls: /c2: cannot open`、`note.txt`），而不是只断言"文件存在"。

## U2 — `make iso` 把整个仓库塞进 ISO，第二次运行还会读自己在写的文件 ✅ 已修复

`scripts/mkiso.sh` 的 xorriso 分支用 `.`（项目根目录）当源、却把输出 `kernel.iso` 写在这个目录里：

- ISO 里装的是**整个仓库**（约 13MB，而不是 1.5MB）——而 guest 里根本没有 ISO9660 驱动，这些内容
  一份也用不上；
- 第二次运行起，xorriso 还会去读它正在写的那个 ISO：退出码 32 → `make iso` 失败，同时文件每跑一次
  翻一倍（22MB → 44MB）。另外两个分支（genisoimage/mkisofs）本来就是用 `.iso_tmp` 干净的，只有
  xorriso 这条不是。

**修法**：xorriso 分支也改成先把补齐后的软盘放进 `.iso_tmp/boot/floppy.img`、再对 `.iso_tmp` 建 ISO，
跑完删掉临时目录（三次调用共用同一段结构）。ISO 从 ~13MB 降到 ~1.5MB，`make iso` 可以反复运行。

**未做（明确的边界）**：ISO 仍然只含内核，仍然需要 `-hda minix.img`。让 ISO 自带文件系统需要
**ATAPI + ISO9660 驱动**（内核只有 IDE PIO），那是另一个量级的工程；El Torito 的"无仿真"模式也救不了：
`boot.s` 起来后要按 EDD 从引导设备读扇区，而光盘不是 EDD 磁盘。

## 已知问题：大文件在管道/多次 read 下的字节数不对（**未修复**，新发现）

在给 `grep` 换成"按块流式读"时撞出来的，**与本次改动无关**（用的是已提交的 `wc`/`cat` 和内核文件读路径）：

```
$ wc < /big.txt            → 1008 1009 18432 -      ← 文件本身：18432 字节
$ cat /big.txt | wc        → 1010 1018 18478 -      ← 多了 2 行 / 46 字节
$ grep -c 0123456789 /big.txt → 1                    ← 只看到第一行
$ grep -n 0123456789 /big.txt → 只打印 1:0123456789abcdef
```

- `/big.txt` 是 mkminix 造的 18KB 文件（每行 `0123456789abcdef`），`wc < /big.txt` 的 1008/18432 与文件属性一致，
  所以**文件内容与单次顺序读是对的**；
- 但 `cat | wc` 会多出 2 行、46 字节，说明管道这条路掺进了额外数据；
- 而 `grep` 是**直接读文件**（不经管道）却只看到第一行，所以这很可能是**两个不同的问题**：
  多次 `read()` 在同一 fd 上的推进，以及管道读写在大数据量下的一致性；
- 已尝试的对照：把 grep 的读块从 1024 改到 256 没有任何变化（仍是 1），所以不是"块大小"这么简单；
- 下一步：先用一个最小用户态探针打印每次 `read()` 的返回值序列（对 /big.txt 直接读、经管道读各一次），
  分清"fd 偏移推进"与"管道内容"两条路；`user/cp.c` 复制 /big.txt 是否 18432 字节也是一个便宜的判据。

这条不阻塞任何现有场景（`wc <` 那条是场景 29 的断言，它是对的），但**它说明大于一个块的顺序读还没有被真正验证过**。

## D1 — 单文件自启动镜像：一张盘启动整个系统 ✅ 已完成

**问题**：到今天为止，系统要**两个文件**才能跑起来：`Image`（1.44MB 软盘：引导扇区 + setup +
内核）和 `minix.img`（3MB 裸镜像 = 1MB MINIX v1 文件系统 + 2MB swap），后者被内核当作**第一块
IDE 盘**（dev 0x301）挂成根文件系统：

```bash
qemu-system-i386 -fda Image -m 16M -boot a -hda minix.img
```

"软盘 + 硬盘"是今天的运行方式，也是使用者第一次跑这个内核最容易漏掉的一件事（漏挂 `-hda` 就
没有文件系统，见文末的已知问题）。现在有一个文件就够：

```bash
make disk
qemu-system-i386 -hda linux.img -m 16M -boot c     # 或 make run-disk
```

`Image`、`minix.img`、`make run`、`make run-cd`、`make debug` 与整套回归的**行为**一行没改：
单文件镜像用的是**同一个** `kernel/system.bin`，`Image` 仍是 1474560 字节、仍走原来那条软盘 CHS
载入路径（引导扇区多了"介质是硬盘"时的一段分支和 4 字节基准 LBA 字段，软盘分支逐条未动）。

**布局**（`tools/mkdisk` 写出；单位都是 512 字节扇区，数字取自本机的实际构建）：

| 区域 | 位置 | 内容 |
|------|------|------|
| 引导扇区 | LBA 0 | `Image` 的引导扇区（同一段代码）+ 写进 0x1F4 的基准 LBA |
| setup | LBA 1..4 | 同 `Image` |
| 内核 | LBA 5..188 | `kernel/system.bin`（184 扇区） |
| 根文件系统 | LBA 192 起 | MINIX v1（1MB）+ 裸 swap（2MB），即 `disk-fs.img` |
| 补齐 | 到 LBA 7056 | 全零，凑满整柱面 |

**基准 LBA 是算出来的，不是硬编码的**：`tools/mkdisk` 取"内核最后一扇区之后的下一个 1KB 边界"
（`fs_base`），**同时**把文件系统放在那里、把这个数字写进**它自己正在生成的那张镜像的引导扇区**
（偏移 0x1F4）。传递链只有一条：

```
boot/boot.s     从自己的引导扇区读出 fs_base_lba，用 %ebx 交给 setup.s
setup.s         mov %ebx,(4) —— 存进 0x10004，紧挨着 INT 15h 结果所在的 0x10002
kernel/main.c   从 BOOT_FS_BASE_ADDR(0x10004) 读出 → hd_set_root_lba() 装进驱动
drivers/hd.c    hd_read_sectors/hd_write_sectors 里 lba += root_lba
```

镜像里写着偏移、内核从镜像里读偏移，两边不可能对不上——这也是**同一份内核二进制**能被软盘
`Image`（基准 0，因为 `minix.img` 是独立设备、文件系统就在它的 LBA 0）和单文件镜像（基准 192）
同时使用的原因。内核变大导致 `fs_base` 变化时，`boot.s` 与内核里也没有任何需要跟着改的常量。

**为什么选"可配置基准 LBA"而不是分区表**（两条路都可行，这里是选型理由）：

- 内核**完全没有分区表解析**（`sys_setup()` 直接 `bread(0x301, 1)`）。走分区表要先教 setup.s /
  内核读 MBR、再教块层"分区内偏移 + 分区边界"两件事，而这里真正需要的只是一个**偏移量**；
- 而且无论哪条路，偏移量都得从镜像传给内核——**传递通道才是要设计的东西**。分区表只是把同一个
  数字抄了第二份，多出一个可能与"实际布局"不一致的来源；
- 把数字放进**引导扇区**（而不是编译进内核）还带来一个额外好处：同一个内核二进制既能被软盘引导
  也能被单文件盘引导，`Image` 这张软盘的大小/布局/软盘载入路径都不变，31 个回归场景走的还是
  原来那条代码路径（`hd.c` 里那次加法在基准 0 时是空操作）；
- 代价：这张盘**没有 MBR 分区表**，靠 BIOS 直接执行 LBA 0（SeaBIOS/QEMU 就是这样）。要"活动分区 +
  VBR"那种分区链路才肯引导的软件不支持——见 `docs/limitations.md`。

**硬盘怎么读**：软盘的 CHS 路径（2 磁头 / 18 扇区每道）对硬盘不成立（内核的 IDE 驱动按
**16 磁头 / 63 扇区每道**算 CHS），所以 `boot/boot.s` 对 `%dl >= 0x80` 的介质改走
**INT 13h AH=42h（EDD LBA 读）**，与几何无关。仍然一次读一扇区（理由和 CHS 路径一样：SeaBIOS 的
多扇区读出现过"报告成功但少传"），循环状态同样全部放在内存里（INT 13h 会破坏寄存器）。

**镜像尺寸为什么要凑整柱面**：`linux.img` 被补齐到**整数个柱面**（16 × 63 = 1008 扇区）。
QEMU 对没在命令行指定几何的 IDE 盘，按 `柱面数 = 总扇区 / 1008`（向下取整）猜几何，内核的
CHS 也正好是 16/63——于是**超出最后一个整柱面的尾巴根本读不到**，而 swap 区恰恰在镜像末尾：
旧的 `minix.img`（6144 扇区 = 6.09 个柱面）就丢了最后 96 扇区 ≈ 12 个 swap 槽。`tools/mkdisk`
把镜像补到 7056 扇区（7 个整柱面），swap 的每一槽都落在可寻址范围内。

**验证**（两条都能自己复现；本机留的全文在 `test-logs/`，该目录不入库）：

- 无头冷启动 `python3 scripts/qemu-test.py --disk linux.img --keys $'ls\ncat /hello.txt\nexec /bin/sh\nls /bin\nwc < /readme.txt\nexit\n'`
  的串口输出里只有**一条**启动横幅（这正是 `check_no_reboot()` 的判据，复位循环跑不出来），
  并依次出现 `MINIX: root filesystem at LBA 192 (single-image disk)`、
  `MINIX: superblock loaded, magic=0x137f`、`hd: IRQ14 enabled`，随后
  `ls`、`cat /hello.txt`、`exec /bin/sh`、`ls /bin`、`wc < /readme.txt` 全部正常；
- swap 区专测：`tools/mkminix test-logs/sw-fs.img user/swaptest.elf:swaptest` 造一张带 swaptest 的
  文件系统，`tools/mkdisk test-logs/sw-disk.img Image test-logs/sw-fs.img` 拼进单文件镜像，
  再用 `--disk` + `--mem 4M` 跑 `exec /bin/swaptest`——仍然 `PASS`，且
  `114 pages swapped out, 50 swapped back in, 512 slots free`。这条专盯"镜像末尾的裸 swap 区在
  单文件布局里真的可寻址"：常规套件跑的是 `Image` + `minix.img`，覆盖不到单文件镜像的高 LBA；
- 既有两条启动路径未受影响：`Image` + `minix.img` 与 `-cdrom kernel.iso -hda minix.img`（El Torito 软盘
  仿真 ⇒ `%dl=0` ⇒ CHS 路径、基准 0）冷启动都是**一条**横幅、**一行** `buffer cache:`、**0 次** `PAGE FAULT`；
- 回归：`TEST_SKIP_HEAVY=1 scripts/regress.sh` 28/28，全量 `scripts/regress.sh` 32/32，`make check` 全绿。

**新增/改动的文件**：`tools/mkdisk.c`（新，镜像组装）、`boot/boot.s`（EDD 路径 + 转发基准 LBA）、
`boot/setup.s`（把 %ebx 存进启动参数块）、`include/linux/memmap.h`（`BOOT_FS_BASE_ADDR`）、
`drivers/hd.c` + `include/linux/hdreg.h`（`hd_set_root_lba`/`hd_root_lba`，偏移只在驱动里加一次）、
`kernel/main.c`（读取参数）、`fs/minix.c`（打一行基准 LBA）、`tools/build.c`（显式把软盘的基准写成 0）、
`Makefile`（`disk` / `run-disk`）、`scripts/qemu-test.py`（新增 `--disk`，便于无头验证）。

## B2'' — 一个会话里的第二个管道：静默三重故障 ✅ 已修复（根因两个）

**最小复现**（`exec /bin/sh` 之后）：

```
cat /hello.txt | wc      ← 第一条管道
cat /hello.txt | wc      ← 第二条管道：内核立刻重启（串口出现第二条启动横幅）
```

崩溃是**静默三重故障**：没有 `PAGE FAULT`、没有 `panic`、没有 `hd:` 打印，只有重启横幅。它此前被 P1
挡着（第一条管道必然失败，没人走到第二步），而套件里没有任何场景在一个会话里连跑两条管道，所以一直是绿的。

**取证过程**（值得记住的方法）：三重故障会把证据全抹掉，所以先用 QEMU 自己的异常日志
（`--extra "-d int,cpu_reset -D qemu.log"`）把异常链抓出来：

```
4160: v=0e (#PF) IP=0008:00011034 CR2=0002661d  CR3=0010f000
check_exception old: 0xe new 0xe
4161: v=08 (#DF) IP=0008:00011034 CR2=00026a73
check_exception old: 0x8 new 0xe
Triple fault
```

符号对照（`nm -n kernel/system`）：`0x11034 = schedule+0x22b`，正是 `switch_to()` 尾部那条
**`ljmp *0x8(%esp)`**（硬件任务切换的远跳）；`CR2=0x2661d = _gdt+0x68`（GDT 里 task2 的 LDT 描述符）——
远跳去读 GDT 描述符时就页错误了；紧接着投递 #PF 时要取 `_idt+0x70`（#PF 的门）也失败。也就是说：**那一刻
CR3 指向的页目录已经不含内核恒等映射**，内核连自己的 GDT/IDT 都读不到，第一次异常就直接升级成三重故障。

**根因一：`wake_up()` 不清队列头**（`fs/buffer.c`）。它只把 `(*p)->state` 置回 `TASK_RUNNING`，
**没有** `*p = NULL`。`sleep_on()` 用这个队列头串起睡眠者，于是已唤醒的任务仍被队列指着；等它退出、
任务页被回收、又被 `get_free_page()` 当成**页目录**发放之后，下一次 `wake_up()` 就把 0 写进了该页的
偏移 0 —— 那正是页目录的 **PDE[0]**。实测探针：

```
alloc 0x10f000            ← 新页目录建好，PDE[0] 正常
wakeup ptr=0x10f000 is pgdir of pid=2 queue=0x2e874 caller=0x1cbab
switch to pid=2 pgdir=0x10f000 pde0=0x0     ← 被写零，随后 switch_to 崩
```

`0x2e874 = hd_lock_q`、`0x1cbab` 落在 `hd_unlock()` 里：第一条管道的磁盘 I/O 把某个进程挂在
`hd_lock_q` 上，醒来后队列没清，于是第二次 `hd_unlock()` 的 `wake_up()` 命中了一个**已经变成页目录**的
回收页。Linux 0.01 原版就是 `(**p).state = 0; *p = NULL;` —— 缺的正是第二句。修复即补上它。

**根因二（独立，同一条管道上的另一个症状）：`sys_open()` 先占槽位后查名字**。它在调用 `namei()` **之前**
就挑好了 `file_table[]` 槽位，直到最后才置 `f_count = 1`；而 `namei()` 会睡在磁盘 I/O 上，于是两个并发
`open()` 挑中同一个条目、共用一个 `struct file`，后写者的 `f_inode` 覆盖前者（实测 `wc` 那一级读回了
`cat` 的 inode）：镜像页是按 `current->exe_inode` 按需调入的，所以那一级实际执行的是另一个程序——这就是
`cat /hello.txt | wc` 只打印 `cat` 的内容、没有 `wc` 那一行的原因。修法：查完之后再占用槽位并立刻标记。

**回归**：场景 34 `pipe2` —— 一个会话里连跑三条 `cat /hello.txt | wc`，断言每条都打印 `1 4 21 -`
（根因二会吃掉这行）、`check_no_reboot()` 要求只有一次启动（根因一被修前必然重启）。修复后实测 3/3：
1 条横幅、1 条 `buffer cache:`、0 个 `PAGE FAULT`、每次管道都有 `1 4 21 -`。

**顺带留下的永久诊断**：`schedule()` 在 `switch_to()` 前检查目标 `tss.cr3` 的 `PDE[0]`，缺失时打印
`switch: task N has cr3=... pde[0]=...`（见 A1 节）。正是它把这次的"静默重启"变成了可读证据。

## A1 — 无根文件系统时进不了 shell（复位循环）✅ 已修复

> ⚠️ 下面"真正的根因"里的三条机制说法（驱动睡进 `schedule()`、`0xffffffff` 是 BSS 垃圾、带盘时
> "读先完成所以通常没事"）后来被独立复核用 `-d int,cpu_reset` 对照日志**证伪**，正确版本见本节末尾的
> **订正**。修复本身有效，经复核确认（无盘 ×3、带盘 ×3 全部 1 横幅 / 0 页错误），只是故事讲错了两处、
> 并漏了第四处修复（`read_inode()` 的槽位释放）。

**现象**：只挂 CD、不挂 MINIX 盘启动（这正是 `make run-cd` 修改前的写法，也是 README 早先的快速开始）：

```
$ qemu-system-i386 -cdrom kernel.iso -m 16M -boot d
buffer cache: 256 buffers (265KB) at [0x3adc00, 0x3f0000)
mem_check: 3732 free pages (14928KB) for tasks, pipes and user pages, starting at 0x105000
hd: IRQ14 enabled (slave mask 0xff -> 0xbf)
buffer cache: …            ← 早启动的打印又来一遍：机器复位了
```

启动横幅（`Minimal Linux 0.01 Equivalent Kernel`，由 `shell_main()` 打印）**从未出现**；串口里也
**没有** `hd: … timeout`、**没有** `MINIX: bad magic`、**没有** `main.c` 那句
`Warning: no root filesystem found`。

**真正的根因**：不是"盘不在"，而是**启动期睡进了一个还没初始化的调度器**：

1. `main()` 的顺序是 `hd_init()` → `sys_setup()` → `sched_init()`，读超级块时任务表还不存在；
2. `hd_lock()` 拿到锁后会 `sti()`，所以 `hd_wait_bits()` 里的 `hd_irq_enabled()`（读 IF 位）**已经是真**，
   驱动于是走"睡等 IRQ14"那条分支 → `schedule()`；
3. 而 `sched_init()` 要等 `sys_setup()` 之后才跑，`task[]` 此时还是 BSS 的 0，`schedule()` 扫到某个
   槽位把里面的 `0xffffffff` 当成任务指针解引用：
   `PAGE FAULT: addr=0xffffffff err=0x0 eip=0x10eb4`（`nm -n kernel/system` → `schedule+0xc7`，
   指令 `mov (%eax),%eax`）；
4. 内核态页错误 → 三重故障 → CPU 复位。

**为什么一直没被发现**：复位发生在**第一条启动横幅之前**，而 `check_no_reboot()` 只数横幅 —— 带盘启动
时磁盘读偶尔在驱动入睡之前就完成了（不睡 ⇒ 不复位），于是"通常能起来"、套件 30/30 全绿；无盘时每次
都睡、每次都崩，才暴露成"根本起不来"。

**修复**（三处，缺一不可）：

- `sched_init_early()`（`kernel/sched.c`）：把"任务表 + `current` + init task"从 `sched_init()` 里拆出来，
  `main()` 在 `hd_init()`/`sys_setup()` **之前**调用。只有 `task[0]` 且它就是 `current` 时 `schedule()`
  以 `next == current_idx` 收尾、不会走 `switch_to()`，所以这一步不需要 TSS/LDT 也是安全的；
- `hd_wait_bits()` 改判 **`sched_ready`**（`sched_init()` 编好 8253 后才置 1）而不是 IF 位：睡下去要靠
  jiffies 判超时，定时器没起来就不能睡、只能轮询 —— 于是"控制器在、盘不在"时会打印
  `hd: read data: timeout while polling, status 0x00` 并正常返回失败，而不是永远 hlt；
- `shell_main()` 在没有 `pwd`（根文件系统没挂上）时明确打印
  `No root filesystem: ls, cat, cd and exec /bin/... will not work.`，而不是让人对着
  `open: permission denied (mode=00 uid=0 mask=0x4 euid=0)` 猜。

**回归**：场景 32 `nodisk`（harness 新增 `QEMU_HDA=`，置空即"不挂盘"）—— 断言只启动一次、不出现
`PAGE FAULT`（`check_no_reboot()` 现在同时数 `buffer cache:` 那行，正是为了抓"横幅前复位"）、打印
warning 并进 shell 执行 `help`/`exit`。实测修好后：无盘 0 次页错误、正常进 shell；**带盘 0 次页错误、
0 次复位**（此前每次启动都会悄悄复位 2–3 次）。

### 订正：三条机制说法被独立复核证伪（含第四处修复）

修复本身经独立复核确认有效（无盘 ×3、带盘 ×3：1 横幅 / 1 条 `buffer cache:` / 0 次页错误；快速集
28/0），但本节最初写的机制有**三处是错的**，用 `-d int,cpu_reset` 的前后对照日志证伪：

1. **第一次 `schedule()` 不是驱动"睡"出来的**。它来自 `do_timer()`：`setup.s` 清了 IF，而 `hd_lock()`
   拿到锁时会 `sti()`——挂起的定时器中断**在这一刻**就被投递，早于 `hd_wait_bits()` 执行。此时
   `current == NULL`，`do_timer()` 读 `current->counter`（物理地址 4，实模式 IVT 的 `0xF000FF53`）→
   `schedule()`。
2. **那个 `0xffffffff` 不是"BSS 里的垃圾"**。`schedule()` 发现 `task[]` 全空 → 走 `c < 0` 分支 → 在
   中断处理程序里、IF=1 的情况下执行 `hlt`（`0x10f63`）。之后的每一次时钟节拍都在**同一个中断帧里再嵌一层**
   （每层约 0x9c 字节），栈一路走进 `.bss` 并把 `task[]` 连同 `schedule()` 自己的局部变量（`c`/`next`）
   覆盖成了 `0xffffffff` —— 第 182 个事件才是那条致命的 `v=0e CR2=ffffffff IP=0008:00010eb4`。
3. **"带盘时读常常先完成所以通常没事"是错的**。用 `-no-reboot`（不重试）跑带盘启动，串口输出与无盘
   启动**逐字节相同**、同一处崩溃——因为故障发生在 `hd_lock()` 的 `sti()`，任何磁盘 I/O 都还没开始。
   带重试时之所以"通常能起来"，靠的是 CPU 复位**不清 .bss**：`jiffies`/`current`/`task[]` 留了下来，
   第 3 次恰好凑对。所以每一次启动其实都是掷硬币。

**第四处修复**：`shell_main()` 里那句"没有根文件系统"提示最初是**死代码**——`read_inode()` 在
`if (!sb) return;`（设备没有超级块）这条路径上**没有释放槽位**（`i_dev`/`i_count` 原样留着），于是
`iget()` 的 `if (inode->i_dev != dev) return NULL;` 守卫永远不会触发，它把一个"已认领但零化"的 inode
（`i_mode == 0`）交给调用者，`current->pwd` 因此**不是** NULL，提示永远不打印，`ls` 依旧报那句误导性的
`open: permission denied (mode=00 uid=0 mask=0x4 euid=0)`。修法是与 `bread()` 失败路径保持一致：

```c
sb = get_super(inode->i_dev);
if (!sb) { inode->i_dev = 0; inode->i_count = 0; return; }
```

修后无盘启动的 `ls` 变成诚实的 `open: namei returned NULL (flag=0x0, inode table 0/64 used)`。

**另外留了一道永久诊断**（`kernel/sched.c`）：`switch_to()` 的下一条指令就是硬件任务切换的远跳，它要读
GDT 里的 TSS 描述符。若目标（或当前）地址空间丢了内核恒等映射，这一次读取就会触发页错误并升级成
#PF → #DF → 三重故障、毫无输出。现在切换前会检查目标 `tss.cr3` 的 `PDE[0]`，一旦缺失就打印
`switch: task N has cr3=... pde[0]=... (running task M, cr3=... pde[0]=...)`。健康启动下它静默
（实测 0 条），而这正是把 B 的"静默重启"变成一行可读诊断的原因。

**顺带修好**：`make run-cd` 与 README/Docker 快速开始此前都**只挂 CD**，照文档做出来的系统没有文件系统
——用户看到的"`ls` 不能用"就是这个。三处现在都补了 `-hda minix.img`（`run-cd` 还依赖 `minix.img` 目标），
`scripts/mkiso.sh` 也会打印正确的运行命令并提示 ISO 只含内核。
