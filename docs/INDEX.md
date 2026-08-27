# 学习文档索引

> **以源码为准。** 若文档与代码冲突，以仓库当前源码为准。

## 推荐阅读路径

```
第 0 步  README.md          项目是什么、如何编译运行
第 1 步  本文件 INDEX.md    建立全局地图
第 2 步  PREREQ 四件套      补齐硬件/语言/OS 理论
第 3 步  TUTORIAL.md        按章节读源码实现
第 4 步  LIMITATIONS.md     明确本仓库「做了什么 / 没做什么」
第 5 步  HLD.md / SRS.md    （可选）设计与需求背景
```

### 前置知识（按顺序）

| 顺序 | 文档 | 学完应能 |
|------|------|----------|
| 1 | [PREREQ-x86-asm.md](PREREQ-x86-asm.md) | 读懂 boot/head 汇编与内联 asm |
| 2 | [PREREQ-c-language.md](PREREQ-c-language.md) | 理解内核 C 惯用法与宏 |
| 3 | [PREREQ-computer-arch.md](PREREQ-computer-arch.md) | 理解 PIC/PIT/IDE/VGA/分页硬件 |
| 4 | [PREREQ-os-theory.md](PREREQ-os-theory.md) | 理解进程/调度/FS/系统调用概念 |

### 实现教程

| 文档 | 内容 |
|------|------|
| [TUTORIAL.md](TUTORIAL.md) | 总目录 + 引导全流程（boot/setup/head/main） |
| [tutorial/07-sched.md](tutorial/07-sched.md) | 调度器 sched.c |
| [tutorial/08-process.md](tutorial/08-process.md) | fork/exit process.c |
| [tutorial/09-syscalls.md](tutorial/09-syscalls.md) | 系统调用 sys.c / vsprintf / panic |
| [tutorial/10-mm.md](tutorial/10-mm.md) | 内存管理 memory.c / page.s |
| [tutorial/11-fs.md](tutorial/11-fs.md) | 文件系统全部 fs/* |
| [tutorial/12-drivers.md](tutorial/12-drivers.md) | 设备驱动 drivers/* |
| [tutorial/13-shell-lib.md](tutorial/13-shell-lib.md) | Shell 与 lib/* |
| [tutorial/14-headers-build.md](tutorial/14-headers-build.md) | 头文件、链接脚本、Makefile |
| [tutorial/15-scenarios.md](tutorial/15-scenarios.md) | 端到端场景（按键/echo/读盘） |
| [LIMITATIONS.md](LIMITATIONS.md) | 已知简化与源码事实 |

## 源码 → 文档对照表

| 源码 | 文档 |
|------|------|
| `boot/boot.s` | TUTORIAL §2 |
| `boot/setup.s` | TUTORIAL §4 |
| `boot/head.s` | TUTORIAL §5 |
| `tools/build.c` | TUTORIAL §3 |
| `kernel/main.c` | TUTORIAL §6 |
| `kernel/sched.c` | tutorial/07-sched.md |
| `kernel/process.c` | tutorial/08-process.md |
| `kernel/sys.c` `vsprintf.c` `panic.c` `asm.s` | tutorial/09-syscalls.md |
| `mm/*` | tutorial/10-mm.md |
| `fs/*` | tutorial/11-fs.md |
| `drivers/*` | tutorial/12-drivers.md |
| `init/shell.c` `lib/*` | tutorial/13-shell-lib.md |
| `include/*` `kernel.ld` `Makefile` | tutorial/14-headers-build.md |

## 本仓库关键事实（先记住）

1. **分页只映射 0–4MB**（head.s 仅填 PDE[0] + 一张页表）
2. **段选择子**：`KERNEL_CS=0x08` `KERNEL_DS=0x10` `USER_CS=0x1B` `USER_DS=0x23`
3. **Shell 运行在内核态**：`main()` 直接 `shell_main()`，**没有** `move_to_user_mode`
4. **系统调用路径已实现且可实测**：`include/unistd.h` 提供 `int 0x80` 包装宏；Shell 的
   `sys`/`spawn`/`sig`/`ls`/`cat`/`wtest` 命令全部走系统调用，fork/调度/信号/文件读写可运行验证
5. **MINIX FS 已打通读写**：挂 `minix.img`（`make minix.img`）后 `ls`/`cat` 可用，`wtest`+`sync` 演示写回
6. **修复过的内核级 bug**（读源码时留意注释）：schedule 预改 current 导致 ljmp 被跳过；
   `init_task.tss.cr3=0` 导致切回父进程 CR3 归零；exit 释放自身任务页的 use-after-free；
   `sys_open` 从 fd 0 分配撞上 stdin；getblk 复用缓冲未摘旧哈希链的链环死循环

## 设计文档（背景，非源码权威）

| 文档 | 说明 |
|------|------|
| [SRS.md](SRS.md) | 需求规格（可能含未实现目标） |
| [HLD.md](HLD.md) | 高层设计（可能与当前实现有差异） |
