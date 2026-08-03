# 本仓库已知简化与源码事实

> 阅读实现教程前先读本文件，避免把「目标设计」当成「已实现行为」。

## 1. 特权级与用户态

| 项目 | 事实 |
|------|------|
| Shell | `main()` → `shell_main()`，始终在 **Ring 0** |
| `move_to_user_mode` | **源码中不存在** |
| USER_CS / USER_DS | GDT 中有定义（`0x1B` / `0x23`），system_call 会把 FS 设为 USER_DS |
| 真实用户进程 | 未做完整 Ring3 落地（无独立用户栈切换进 shell） |

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
| 调度 | O(N) counter + priority，硬件 `ljmp` TSS 切换 |
| fork | 复制 task_struct + 内核栈帧；`f_count++` |
| exit | 摘掉 task 槽并 `free_page`，无 wait/zombie 回收 |
| 信号 | `signal` 字段存在，未实现投递 |

## 4. 文件系统

| 项目 | 事实 |
|------|------|
| 类型 | MINIX v1 |
| `sys_setup` | 读超级块到 `super_block[0]` |
| 写路径 | `file_write` / `new_block` 存在，但 `ll_rw_block` **只处理 READ** |
| Shell ls/cat | 打印 “not yet implemented” |

## 5. 设备

| 项目 | 事实 |
|------|------|
| 控制台 | VGA 文本 0xB8000 |
| 键盘 | PS/2 扫描码 + Shift |
| 硬盘 | IDE PIO 读；从 PIC 启动时 mask=0xFF，读路径为轮询 |
| 串口 | 未实现 |

## 6. 构建与运行

| 项目 | 事实 |
|------|------|
| 目标 | i386 32-bit freestanding |
| macOS | 需 Docker 或 `i386-elf-gcc` |
| 运行 | QEMU `-fda Image` 或 `-cdrom kernel.iso`，内存 4M |

## 7. 与文档/设计稿的关系

- **权威顺序**：源码 > LIMITATIONS/TUTORIAL > HLD/SRS
- HLD/SRS 中的「用户态 / move_to_user_mode / 写文件系统」等表述，可能是目标而非现状
