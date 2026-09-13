# 本仓库已知简化与源码事实

> 阅读实现教程前先读本文件，避免把「目标设计」当成「已实现行为」。

## 1. 特权级与用户态

| 项目 | 事实 |
|------|------|
| Shell | 两个：内核态 `main()` → `shell_main()`（**Ring 0**，救援/调试入口）；**Ring3 的 `/bin/sh`**（`user/sh.c`）——它自己 read 键盘、自己 fork/execve/waitpid，并支持 `<`、`>`、`>>`、最多 4 段 `\|` 管道（内建：cd/pwd/echo/exit/help） |
| `move_to_user_mode` | 无此函数；Ring3 切换靠 `run_user_program`（内嵌程序）/ `sys_execve`（MINIX 里的 ELF32）iret 完成 |
| USER_CS / USER_DS | GDT 中定义（`0x1B` / `0x23`）；system_call 会把 FS 设为 USER_DS |
| 用户程序 | **`sys_execve` 从 MINIX 加载 ELF32**（/bin/xxx）：LOAD 段按页装进**该进程自己的新地址空间**（内核经恒等映射把文件内容填进新帧），纯 BSS 页不预建、首次访问按需分配；argv 写到栈顶尾页 `USER_ARGC_ADDR`/`USER_ARGV_PTR_ADDR`/`USER_ARGV_ADDR`（0x083FF004/8/C）；iret 到入口。内嵌程序（`user` 命令）走 `load_flat_image()`，同样进独立地址空间 |
| inode 缓存 | 正常（曾误判为缺陷：实为 mkminix 的 imap 写入顺序错误——/hello 的 inode 位在 memcpy 后才设置，导致 new_inode 复用其编号；已修复） |
| 用户态 fork | **已实现**：`system_call` 检测调用者 CPL（`syscall_cpl`）；Ring3 调用时 `sys_fork` 构造 16 项恢复帧（含用户 esp/ss），并给子进程一份**新地址空间 + 写时复制的用户页**（`copy_page_tables`），子进程 iret 回 Ring3 运行 |
| 信号 | **自定义处理器已实现**（M2-1）：`signal(sig, handler)` 接受 Ring3 函数指针（越界地址被拒）；投递时在用户栈上构造 `sigframe`（信号号 + 返回地址 `USER_SIGRETURN_ENTRY` + 保存的 80 字节上下文），handler 返回后执行 9 字节 `sigreturn` 桩（syscall 67），内核校验 magic/retaddr/cs/ss/esp 后**精确恢复**被中断的上下文 |
| 内存隔离 | **已实现**：内核恒等映射（0–16MB，PDE[0..3]）在每份页目录里都是 supervisor-only，用户页只存在于 PDE[32] 指向的那张进程私有页表里（PTE=0x07）；Ring3 访问内核地址 → page fault → **终止肇事进程**（SIGSEGV 默认动作，exit 139，内核继续运行） |

## 2. 内存

| 项目 | 事实 |
|------|------|
| 恒等映射范围 | **0–16MB**（PDE[0..3]，`boot/head.s` 建 4 张内核页表）；`KERNEL_IDENTITY_TOP = 0x01000000`。QEMU 统一 `-m 16M` |
| 内存地图唯一来源 | **`include/memlayout.h`**（内核与用户态共用；`include/linux/memmap.h` 是内核侧视图，`include/memlayout.inc` 是汇编侧镜像）。**任何文件都不许再硬编码用户区地址**——`scripts/check-layout.py` 会扫描并让 CI 失败 |
| 编译器期校验 | `include/linux/memmap.h` 的 `STATIC_ASSERT`（区域有序、用户区必须落在同一个 PDE 窗口内、缓存装得下）；布局写错 = 编译失败 |
| 启动期校验 | `mm/memcheck.c` 的 `mem_check()`（`kernel/main.c` 在 `mem_init`/`buffer_init` 之后立即调用）：内核页表页与缓存页是否真的在 mem_map 里被保留、用户区是否有序、内核页目录是否有用户窗口、空闲页数 → 不一致即 `panic` + 地图 dump |
| 用户区（虚拟地址，每进程私有） | 窗口 `[0x08000000,0x08400000)` = **一个页目录项**：程序镜像 `0x08000000`、堆 `0x08100000`、保护空洞 `0x08200000`、栈下界 `0x08300000`、栈顶 `0x083FF000`、尾页 `[0x083FF000,0x08400000)`（argc/argv/sigreturn stub） |
| 缓冲区缓存 | 在 `BUFFER_CACHE_FLOOR`(0x350000) 与 `BUFFER_CACHE_TOP`(0x3F0000) 之间向下生长：运行期为 `[0x3ADC00,0x3F0000)`（256 × 1KB + 256 × 32B 头，约 265KB）。**缓存的条数由内存地图派生**（`include/linux/fs.h`），不再手工挑选 |
| **已修复：缓存/堆重叠** | 旧值 `NR_BUFFERS=512` 把缓存放到 `~0x370000`，**正好压在用户堆上**：Ring0 无视 PTE 的 U/S 位，用户 malloc 的字节与文件系统块缓冲会是同一批物理页，写坏文件系统而毫无提示。M1 用静态断言 + 启动自检 + 回归场景 11（`user/bigalloc.c`）守住；M3 之后用户页来自页帧池，缓存页在 mem_map 里是 USED，**结构上不可能重叠** |
| 内核堆 | `lib/malloc.c` 的 bump 分配器，区间 `[KERNEL_HEAP_START, KERNEL_HEAP_END)` = `[0x30000,0x40000)`；越界返回 NULL（此前上界写成 `memory_end-0x200000`，会伸进页分配器池）。M4 把映像上限从 `0x2B000` 抬到 `0x30000`——此前只剩 4.5KB 余量，加一个功能就会撞线 |
| 页分配器 | `get_free_page()` 从 mem_map 顺序扫描第一个空闲页并清零；`mem_init()` 保留内核页表页、`buffer_init()` 保留缓存页、`mem_map` 自身保留；**没有硬上限**，耗尽时返回 0，缺页处理据此打印 OOM 并只杀肇事进程（回归场景 18） |
| 剩余页池 | 16MB 下约 3700 页（`memstat` 可查）；任务页/管道页/所有用户页共用 |
| COW / 按需调页 | **已实现**（M3/B3）：用户页首次访问才分配（`do_no_page` 按区域校验后建页）；fork 让父子共享只读页并在 PTE 上打软件 COW 位，写缺页时 `un_wp_page` 复制。区域外的访问仍然杀进程 |
| 页回收（B3） | `get_free_page()` 在池子空时调用 `try_to_free_page()`：**全零页**与**只读镜像页**可丢弃（后者下次取指由 `page_in_image()` 从可执行文件读回——文件就是它的后备存储），**COW 共享页**只解除映射（帧留给另一个 owner）且仅作兜底（不产生空闲帧）；私有脏堆/栈页没有交换区，不回收。`memstat` 报 `pages evicted` / `COW mappings dropped` |
| 程序镜像 | `execve` **不再预拷贝** LOAD 段：把段位置记进 `exe_regions[]` 并持有可执行文件 inode，缺页时才从文件读进新帧（`N image pages read back from the executable` 可见）。镜像页因此天然可回收，无需交换区 |
| 用户堆 | `user/lib.c` 的 first-fit + bump（`[0x08100000,0x08200000)`，1MB），页由内核按需提供；`sys_brk` 只记录 `task_struct.brk`，堆边界由用户库自己管 |
| fork 的用户栈 | 与父进程**共享只读页 + 写时复制**，不再有独立「子进程栈区」，也没有栈大小上限（受限于物理页） |

## 3. 进程与调度

| 项目 | 事实 |
|------|------|
| 调度 | O(N) counter + priority，硬件 `ljmp` TSS 切换（`schedule()` 不预改 current，由 `switch_to` 内 `xchg`） |
| fork | 复制 task_struct + 内核栈帧；新建地址空间并把用户页挂成 COW 共享；`f_count++`；pid == task[] 槽位 |
| exit | 先切回内核页目录并 `free_user_space()` 释放地址空间（页按引用计数递减），再转为 **TASK_ZOMBIE**（保留 task[] 槽与任务页，发 SIGCHLD 唤醒父）；由父 `waitpid` 回收（退出码经 `*stat_addr` 传出 + 释放任务页）；init(task[0]) 保持空闲锚点不退出 |
| 信号 | **投递已实现**：`sys_kill` 置位 + 唤醒 TASK_INTERRUPTIBLE；`ret_from_sys_call` 调用 `do_signal`；默认动作 SIGINT/SIGQUIT/SIGKILL/SIGPIPE/SIGALRM → exit(128+sig)，其余忽略；**`signal()` syscall**（SIG_DFL/SIG_IGN/SIGKILL 不可捕获）——SIGCHLD 忽略时子进程由调度器自动回收，waitpid 返回 ECHILD |
| 用户态 | **自定义信号处理器已实现**（M2-1，见 §1）；**Ring3 `/bin/sh` 可以跑子程序了**（M3）：子进程 execve 装进自己的新地址空间，父 shell 的代码页不受影响；fork 的写时复制让父子内存真正隔离（回归场景 14/16） |

## 4. 文件系统

| 项目 | 事实 |
|------|------|
| 类型 | MINIX v1 |
| `sys_setup` | 读超级块到 `super_block[0]`（无分区表解析，dev 硬编码 0x301） |
| 写路径 | **已打通**：`file_write` → 脏缓冲 → `sync_dev`/`sys_sync` → `ll_rw_block(WRITE)` → `hd_write_sectors` 落盘；inode 同步经 `write_inode` |
| 缓冲 | `getblk` 复用前回写脏块、并从旧哈希链摘除（避免链环死循环）；`iget` 复用脏 inode 槽前先写盘；**定时回写已实现**（M2-3）：`do_timer` 置标志并唤醒专用回写任务（`kernel/sync.c`，占最后一个任务槽，保持用户 pid 从 1 开始），每 5 秒 `sync_dev()` 一次，只在真的写了块时打印 `sync: N block(s) written back`。缓存条数 256（见 §2：条数由内存地图派生，写死会压到用户堆） |
| Shell ls/cat | **已实现**，走 open/read/close 系统调用；`wtest` 演示写路径 |
| 文件创建 | **已实现**：`sys_open(O_CREAT)`/`sys_creat`（touch）/ `sys_mkdir` 含 `.`/`..` 项、inode/zone 位图、父目录项；**删除**：`sys_unlink`/`sys_rmdir`（空目录校验、zone 回收、父 nlinks 递减）；**硬链接** `sys_link`（nlinks++）、**重命名** `sys_rename`（跨目录同设备）、**chroot**、**chdir 相对路径**、`stat/fstat`、`chmod/chown`、`lseek`/`dup`/`dup2` 可用 |
| 权限模型 | **已实现**（M4）：`permission(inode, mask)`（`fs/namei.c`，规则与 Linux 0.01 相同：owner/group/other 三段 + root 覆盖 + 已删除 inode 谁也不能访问）。检查点：`open`（按 O_RDONLY/O_WRONLY/O_TRUNC 要 r/w）、`execve`（要 x）、`access`（真正的 R/W/X 检查，不再只查存在）、`chdir`（目录要 x）、创建/删除/链接/改名（父目录要 w）、`chmod`（仅 owner 或 root）、`chown`（仅 root）、`utime`（owner 或可写）。**路径遍历**中每一级目录都要 x（0.01 的 `dir_namei` 语义），所以 0700 目录对别人等于不存在。新建文件的 `i_uid/i_gid` 取 `current->euid/egid` |
| 管道 | **已实现**（`sys_pipe`，fs/pipe.c 移植 0.01）：单页环形缓冲、sleep_on 阻塞、写端关闭 → 读 EOF、无读者写 → SIGPIPE；缓冲 4KB，写满阻塞（无 O_NONBLOCK） |
| 系统调用 | **67 个，编号 = Linux 0.01**；stub 返回 -1 的与 0.01 自身 -ENOSYS 一致（break/mount/umount/ptrace/stty/gtty/ftime/prof/acct/phys/lock/ioctl/mpx/ulimit/ustat） |

## 5. 设备

| 项目 | 事实 |
|------|------|
| 控制台 | VGA 文本 0xB8000；**作为 fd 0/1/2 出现在描述符表里**（`struct file tty_file`，`f_inode == NULL` 即"控制台"），所以 `dup2` 能把 stdout 换成文件或管道——重定向是靠这一点成立的，而不是靠 `sys_write` 里的硬编码分支 |
| 键盘 | PS/2 扫描码 + Shift；IRQ 处理时**排空 8042 输出缓冲**（快速连击不丢键） |
| 硬盘 | IDE PIO 读写；**中断驱动**（B2）：`hd_init()` 打开 IRQ14（从片掩码 0xFF→0xBF，此前整片屏蔽、`hd_interrupt_handler` 是死代码），发命令的任务 `sleep_on(&hd_wait)` 让出 CPU，由 IRQ14 唤醒；`jiffies` 截止时间 + 定时器保证"丢中断"只是超时而不是死机；开机阶段（`sti()` 之前）中断未开、`jiffies` 不走，此时退回有界轮询。因为任务会在驱动里睡眠，整次操作由 `hd_lock` 串行化。`memstat` 会打印累计 IRQ14 次数 |
| 串口 | **COM1 已实现**：控制台输出镜像，供 `-serial file:` 无头测试捕获精确文本 |

## 6. 构建与运行

| 项目 | 事实 |
|------|------|
| 目标 | i386 32-bit freestanding |
| macOS | Homebrew `i686-elf-gcc` + `i686-elf-binutils` 直接构建（Makefile 自动检测），或 Docker |
| 运行 | QEMU `-fda Image` 或 `-cdrom kernel.iso`，内存 **16M**（内核页表恒等映射 16MB）；MINIX 测试盘 `make minix.img` + `-hda minix.img` |
| 自动化 | `scripts/qemu-test.py` 无头驱动（串口文本 + sendkey，含大写与 `\| < > ( ) & *` 等需要 shift 的键；`--mem` 缩小客户机内存以制造压力；`--type-delay` 调打字速度；`--min-wait` 让需要观察周期性事件的用例不被“输出静止”提前收尾），`scripts/regress.sh` **22 个场景**（`make test`；`make test-fast` / `TEST_SKIP_HEAVY=1` 跳过 autosync/oom/evict 三个重场景，CI 的 PR 跑快集），`scripts/ppm2png.py` 转截图 |
| 回归耗时 | 全集在本机容器内（TCG，无 KVM，与 CI 同模式）实测 **约 10.5 分钟**，而 CI 作业预算 30 分钟（含 apt/构建/静态检查）。时间几乎都花在 harness 按键上（每字符 `TEST_TYPE_DELAY` 默认 0.5 秒）——**不要靠压低它省时间**：低于 ~0.2 秒实测会丢键（8042 只有一个字节的输出缓冲，guest 跟不上就整条命令丢掉），所以拆成快集/全集而不是压速度 |
| 静态校验（无需编译器） | `make check` = `check-layout` + `check-docs` + `check-docs-selftest`。`scripts/check-layout.py`：内存地图有序/不重叠、用户区必须整体落在一个页目录项内、`memlayout.inc` 与 `memlayout.h` 一致、缓存装得进窗口、**用户区地址没有被硬编码到布局头之外**，已构建 `kernel/system` 时还校验链接期 `_end` 未越界。`scripts/check-docs.py`：文档里的旧地址/旧宏必须带历史标注、`0x08xxxxxx` 必须是布局常量、场景数必须等于 `regress.sh` 实际条数、`-m` 参数必须与测试驱动一致 |

## 7. 与文档/设计稿的关系

- **权威顺序**：源码 > LIMITATIONS/TUTORIAL > HLD/SRS
- HLD/SRS 是早期设计稿，部分表述（如 move_to_user_mode、syscall 编号）与当前实现
  有差异；以源码与本文档为准。当前内核已实现 0.01 对齐的 67 个系统调用（编号见 README）
