# 本仓库已知简化与源码事实

> 阅读实现教程前先读本文件，避免把「目标设计」当成「已实现行为」。

## 1. 特权级与用户态

| 项目 | 事实 |
|------|------|
| Shell | `main()` → `shell_main()`，始终在 **Ring 0** |
| `move_to_user_mode` | **已实现**（`run_user_program`）：iret 切到 Ring3 运行嵌入的用户程序（`user` 命令） |
| USER_CS / USER_DS | GDT 中定义（`0x1B` / `0x23`）；system_call 会把 FS 设为 USER_DS |
| 用户程序 | 链接到 0x200000（内核复制后运行）、用户栈 0x3FF000；经 int 0x80 调 write/getpid/time 后 exit；**无 execve**（程序内嵌，不从文件系统加载） |
| 用户态 fork | **未实现**：fork 的栈帧恢复（`syscall_esp` 偏移）基于 Ring0 调用，用户态调用会错 |
| 内存隔离 | 页表 U/S=1，用户可访问全部 0–4MB（教学内核刻意不隔离） |

## 2. 内存

| 项目 | 事实 |
|------|------|
| 恒等映射范围 | **0–4MB**（仅 PDE[0]） |
| `memory_end` | 截断到 `0x400000` |
| COW / 按需换页 | 未实现；`do_no_page` 直接 panic |
| `malloc` | 简单 bump allocator，与 `get_free_page` **无统一协调** |

## 3. 进程与调度

| 项目 | 事实 |
|------|------|
| 调度 | O(N) counter + priority，硬件 `ljmp` TSS 切换（`schedule()` 不预改 current，由 `switch_to` 内 `xchg`） |
| fork | 复制 task_struct + 内核栈帧；`f_count++`；pid == task[] 槽位 |
| exit | 摘掉 task 槽；**不释放任务页**（无 wait/zombie 回收，泄漏 4KB/进程，避免 use-after-free） |
| 信号 | **最小投递已实现**：`sys_kill` 置位 + 唤醒 TASK_INTERRUPTIBLE；`ret_from_sys_call` 调用 `do_signal`；默认动作 SIGINT/SIGQUIT/SIGKILL → exit(128+sig)，其余忽略 |
| 用户态 | 无 `move_to_user_mode`，Shell 在内核态直接调用；**无自定义信号处理器投递**（仅默认动作） |

## 4. 文件系统

| 项目 | 事实 |
|------|------|
| 类型 | MINIX v1 |
| `sys_setup` | 读超级块到 `super_block[0]`（无分区表解析，dev 硬编码 0x301） |
| 写路径 | **已打通**：`file_write` → 脏缓冲 → `sync_dev`/`sys_sync` → `ll_rw_block(WRITE)` → `hd_write_sectors` 落盘；inode 同步经 `write_inode` |
| 缓冲 | `getblk` 复用前回写脏块、并从旧哈希链摘除（避免链环死循环）；`iget` 复用脏 inode 槽前先写盘；**无 writeback 定时器**（需显式 `sync`） |
| Shell ls/cat | **已实现**，走 open/read/close 系统调用；`wtest` 演示写路径 |
| 文件创建 | **已实现**：`sys_mknod`（touch）/ `sys_mkdir` 含 `.`/`..` 项、inode/zone 位图、父目录项；**删除**：`sys_unlink`/`sys_rmdir`（空目录校验、zone 回收、父 nlinks 递减）；目录满/间接块扩容不支持；`lseek`/`dup`/`dup2`/`getppid` 可用 |

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
- HLD/SRS 中的「用户态 / move_to_user_mode / 写文件系统」等表述，可能是目标而非现状
