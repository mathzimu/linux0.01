# 本仓库已知简化与源码事实

> 阅读实现教程前先读本文件，避免把「目标设计」当成「已实现行为」。

## 1. 特权级与用户态

| 项目 | 事实 |
|------|------|
| Shell | `main()` → `shell_main()`，始终在 **Ring 0** |
| `move_to_user_mode` | 无此函数；Ring3 切换靠 `run_user_program`（内嵌程序）/ `sys_execve`（MINIX 里的 ELF32）iret 完成 |
| USER_CS / USER_DS | GDT 中定义（`0x1B` / `0x23`）；system_call 会把 FS 设为 USER_DS |
| 用户程序 | **`sys_execve` 从 MINIX 加载 ELF32**（/hello，链接 0x200000）：LOAD 段加载到 vaddr、BSS 清零、argc/argv 放用户栈顶之上（0x3FF004+，避开向下生长的用户栈）、授权用户页（`grant_user_pages`）、iret 到入口；内嵌程序（user 命令）保留 |
| inode 缓存 | 正常（曾误判为缺陷：实为 mkminix 的 imap 写入顺序错误——/hello 的 inode 位在 memcpy 后才设置，导致 new_inode 复用其编号；已修复） |
| 用户态 fork | **已实现**：`system_call` 检测调用者 CPL（`syscall_cpl`）；Ring3 调用时 `sys_fork` 构造 16 项恢复帧（含用户 esp/ss）并把用户栈复制到子进程区域（0x3E0000 下），子进程 iret 回 Ring3 运行 |
| 内存隔离 | **已实现**：页表默认 U/S=0（PTE=0x03 = P+RW），仅用户程序/堆/栈页经 `grant_user_pages` 置 U/S（0x07）；越权访问 → page fault → do_no_page **panic**（教学行为，非 EFAULT 返回） |

## 2. 内存

| 项目 | 事实 |
|------|------|
| 恒等映射范围 | **0–4MB**（仅 PDE[0]） |
| `memory_end` | 截断到 `0x400000` |
| COW / 按需换页 | 未实现；`do_no_page` 直接 panic |
| 用户堆 | `user/lib.c` 的 bump+freelist（0x310000–0x3FE000），与内核 `get_free_page` **无统一协调**（内核分配页 U/S=0，用户堆区页启动时预授权） |

## 3. 进程与调度

| 项目 | 事实 |
|------|------|
| 调度 | O(N) counter + priority，硬件 `ljmp` TSS 切换（`schedule()` 不预改 current，由 `switch_to` 内 `xchg`） |
| fork | 复制 task_struct + 内核栈帧；`f_count++`；pid == task[] 槽位 |
| exit | 转为 **TASK_ZOMBIE**（保留 task[] 槽与任务页，发 SIGCHLD 唤醒父）；由父 `waitpid` 回收（退出码经 `*stat_addr` 传出 + 释放任务页）；init(task[0]) 保持空闲锚点不退出 |
| 信号 | **投递已实现**：`sys_kill` 置位 + 唤醒 TASK_INTERRUPTIBLE；`ret_from_sys_call` 调用 `do_signal`；默认动作 SIGINT/SIGQUIT/SIGKILL/SIGPIPE/SIGALRM → exit(128+sig)，其余忽略；**`signal()` syscall**（SIG_DFL/SIG_IGN）——SIGCHLD 忽略时子进程由调度器自动回收，waitpid 返回 ECHILD |
| 用户态 | **无自定义信号处理器**（`signal()` 只接受 SIG_DFL/SIG_IGN，其余 -1）；Shell 在内核态 |

## 4. 文件系统

| 项目 | 事实 |
|------|------|
| 类型 | MINIX v1 |
| `sys_setup` | 读超级块到 `super_block[0]`（无分区表解析，dev 硬编码 0x301） |
| 写路径 | **已打通**：`file_write` → 脏缓冲 → `sync_dev`/`sys_sync` → `ll_rw_block(WRITE)` → `hd_write_sectors` 落盘；inode 同步经 `write_inode` |
| 缓冲 | `getblk` 复用前回写脏块、并从旧哈希链摘除（避免链环死循环）；`iget` 复用脏 inode 槽前先写盘；**无 writeback 定时器**（需显式 `sync`） |
| Shell ls/cat | **已实现**，走 open/read/close 系统调用；`wtest` 演示写路径 |
| 文件创建 | **已实现**：`sys_open(O_CREAT)`/`sys_creat`（touch）/ `sys_mkdir` 含 `.`/`..` 项、inode/zone 位图、父目录项；**删除**：`sys_unlink`/`sys_rmdir`（空目录校验、zone 回收、父 nlinks 递减）；**硬链接** `sys_link`（nlinks++）、**重命名** `sys_rename`（跨目录同设备）、**chroot**、**chdir 相对路径**、`stat/fstat`、`chmod/chown`、`lseek`/`dup`/`dup2` 可用 |
| 管道 | **已实现**（`sys_pipe`，fs/pipe.c 移植 0.01）：单页环形缓冲、sleep_on 阻塞、写端关闭 → 读 EOF、无读者写 → SIGPIPE；缓冲 4KB，写满阻塞（无 O_NONBLOCK） |
| 系统调用 | **67 个，编号 = Linux 0.01**；stub 返回 -1 的与 0.01 自身 -ENOSYS 一致（break/mount/umount/ptrace/stty/gtty/ftime/prof/acct/phys/lock/ioctl/mpx/ulimit/ustat） |

## 5. 设备

| 项目 | 事实 |
|------|------|
| 控制台 | VGA 文本 0xB8000 |
| 键盘 | PS/2 扫描码 + Shift；IRQ 处理时**排空 8042 输出缓冲**（快速连击不丢键） |
| 硬盘 | IDE PIO 读写；从 PIC 启动时 mask=0xFF，读写路径为轮询 |
| 串口 | **COM1 已实现**：控制台输出镜像，供 `-serial file:` 无头测试捕获精确文本 |

## 6. 构建与运行

| 项目 | 事实 |
|------|------|
| 目标 | i386 32-bit freestanding |
| macOS | Homebrew `i686-elf-gcc` + `i686-elf-binutils` 直接构建（Makefile 自动检测），或 Docker |
| 运行 | QEMU `-fda Image` 或 `-cdrom kernel.iso`，内存 4M；MINIX 测试盘 `make minix.img` + `-hda minix.img` |
| 自动化 | `scripts/qemu-test.py` 无头驱动（串口文本 + sendkey），`scripts/ppm2png.py` 转截图 |

## 7. 与文档/设计稿的关系

- **权威顺序**：源码 > LIMITATIONS/TUTORIAL > HLD/SRS
- HLD/SRS 是早期设计稿，部分表述（如 move_to_user_mode、syscall 编号）与当前实现
  有差异；以源码与本文档为准。当前内核已实现 0.01 对齐的 67 个系统调用（编号见 README）
