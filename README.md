# Minimal Linux 0.01 Equivalent Kernel

<p align="center">
  <em>一个可引导、<strong>系统调用接口与 1991 年 Linux 0.01 完全同构</strong>的 i386 教学内核</em><br>
  <sub>C · GNU as (AT&T) · 约 5000 行内核源码 · GPLv2 · 仅 i386</sub>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/arch-i386%2032--bit-blue" alt="arch">
  <img src="https://img.shields.io/badge/syscalls-67%20(0.01--aligned)-green" alt="syscalls">
  <img src="https://img.shields.io/badge/runs-QEMU%203%20platforms-orange" alt="runs">
  <img src="https://img.shields.io/badge/license-GPLv2-lightgrey" alt="license">
</p>

---

**为什么值得看：** 它不只是"能打印 Hello"的玩具内核，而是一个**进程、调度、文件系统、用户态、信号、管道、内存隔离全部真实落地并可运行验证**的微型操作系统。系统调用编号逐项对齐 1991 年 Linux 0.01 的 `sys_call_table`（67 个），`include/unistd.h` 可与你手上的 Linux 0.01 源码对照阅读。

---

## ✨ 核心特性（一览）

| 模块 | 能力 |
|------|------|
| **引导** | BIOS → 实模式引导扇区 → setup（A20 / PIC / GDT）→ 保护模式 → head（分页 / IDT）→ main |
| **进程管理** | `task_struct` 控制块、TSS 硬件上下文切换、最多 64 进程、**zombie + `waitpid` 回收**、SIGCHLD 忽略时自动回收 |
| **用户态** | `execve` 从 MINIX 加载 **ELF32** 并 iret 进 **Ring3**（argc/argv 传递）、**Ring3 fork**、可编程工具链（`make prog`） |
| **调度** | 100Hz 时钟中断、O(N) 优先级轮转、抢占式、`alarm(SIGALRM)` |
| **内存** | 4KB 分页、位图页帧分配器、恒等映射 0–4MB、**内存隔离**（内核页 U/S=0，仅按区授权用户页） |
| **中断** | IDT 256 门、时钟 / 键盘 / 硬盘 / 系统调用（`int 0x80`） |
| **设备** | VGA 80×25 文本控制台、PS/2 键盘（含 Shift）、IDE 硬盘 PIO、COM1 串口镜像 |
| **文件系统** | **MINIX v1 读写**（文件/目录增删、**硬链接、重命名、chroot**）、LRU 块缓冲（脏块回写）、inode 缓存、相对路径 + `chdir` |
| **系统调用** | **67 个，编号与 1991 Linux 0.01 完全一致**（含管道、`stat/fstat`、`signal`、`uid/gid`、`umask`、`uname`…） |
| **Shell** | 25 条命令，覆盖进程 / 信号 / 文件系统 / 管道全链路 |

---

## 🚀 快速开始

### Linux（原生）

```bash
sudo apt install -y build-essential gcc-multilib qemu-system-x86 xorriso
make && make iso
qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
```

### macOS（Homebrew）

```bash
brew install qemu xorriso i686-elf-gcc i686-elf-binutils
make            # Makefile 自动检测 i686-elf-* 交叉工具链
qemu-system-i386 -fda Image -m 4M -boot a
```

### Docker（平台无关）

```bash
docker build -t linux-0.01-builder .
docker run --rm -v $(pwd):/kernel -w /kernel linux-0.01-builder make clean all iso
qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
```

### 构建产物

| 文件 | 说明 |
|------|------|
| `Image` | 1.44MB 软盘镜像（`-fda Image` 直接启动） |
| `kernel.iso` | El Torito 启动光盘（`-cdrom kernel.iso`） |
| `minix.img` | MINIX v1 测试盘（`make minix.img`，挂载真实文件系统用） |

---

## 🎮 实际体验

开机后是一个运行在内核态的 Shell。下面这串命令即可看到这个内核"活"的一面：

```
$ ls                             # MINIX 文件系统
2  hello.txt
3  readme.txt
4  big.txt
5  docs
7  hello

$ cd /docs                       # chdir + 相对路径
$ cat note.txt
A file inside a subdirectory.

$ exec /hello a b                # fork + execve 进 Ring3 跑 ELF32
hello from user program: argc=3
  argv[0] = /hello
  argv[1] = a
  argv[2] = b
exec: child 1 exit_code=42

$ exec /pipedemo                 # 管道：父进程阻塞读子进程写入
pipe: read fd=3 write fd=4
parent read 26 bytes: "hello from child via pipe!"
parent read after EOF: 0

$ ln /hello.txt /hard            # 硬链接（nlink 1→2）
$ stat /hello.txt
stat /hello.txt: ino=2 size=21 mode=0100644 nlink=2 uid=0 gid=0

$ spawn                          # fork 两子进程 + waitpid 回收
[parent] fork #1 -> pid 1
[parent] fork #2 -> pid 2
```

> 更多命令见下方 [Shell 命令](#shell-命令)。

---

## 🏗️ 架构鸟瞰

### 启动流程

```
BIOS POST
  └─ boot.s (0x7C00)
       ├─ 加载 setup.s → 0x10000 · kernel → 0x10800
       └─ 跳 setup.s
            ├─ 读硬件参数 (INT 15h) · 启用 A20 · 初始化 PIC · 设临时 GDT
            ├─ 进保护模式 (CR0.PE=1) → 跳 head.s (0x10800)
            └─ 建页目录+页表 · 开分页 (CR0.PG=1) · 设 IDT · 加载内核 GDT
                 └─ call main()
                      ├─ mem_init / buffer_init / tty_init
                      ├─ sys_setup (挂载 MINIX) / sched_init
                      ├─ sti() 开中断
                      └─ shell_main()   ← 内核态 Shell（不返回）
```

### 内存布局

```
0x000000 ┌──────────────────┐
         │   BIOS + IVT     │
0x100000 ├──────────────────┤ ← 页目录 (PGDIR)
0x101000 ├──────────────────┤ ← 页表 0 (仅 PDE[0]，恒等映射 0-4MB)
0x108000 ├──────────────────┤ ← 内核起始 (startup_32)
         │  内核代码/数据/BSS│
         │  内核堆           │
         │  缓冲池 (512KB)   │
         │  0x200000 用户程序 │ ← execve 加载 ELF32 到此处
         │  0x310000 用户堆   │ ← user/lib.c malloc
         │  0x3FF000 用户栈   │ ← argc/argv 在 0x3FF004/0x3FF008
0x400000 └──────────────────┘ ← 4MB 上限
```

### 关键事实（读源码前先记住）

1. **Shell 在内核态**（`main` 直接 `shell_main`）；用户程序经 `execve`/`run_user_program` iret 进 **Ring3**，`int 0x80` 自动切回内核栈
2. **67 个系统调用，编号 = Linux 0.01**；`include/unistd.h` 提供 `int $0x80` 包装宏
3. **内存隔离**：页表默认 `0x03`（P+RW 无 U/S），仅用户程序/堆/栈页被 `grant_user_pages` 授权（`0x07`）；Ring3 越权访问 → 终止肇事进程（SIGSEGV），内核继续运行
4. **段选择子**：`KERNEL_CS=0x08` `KERNEL_DS=0x10` `USER_CS=0x1B` `USER_DS=0x23`
5. **MINIX FS** 挂 `minix.img`（dev 0x301）后 `ls`/`cat`/`wtest` 可实测读写

---

## 🔧 系统调用

### 编号表（= 1991 Linux 0.01 `sys_call_table`）

| 编号 | 调用 | 编号 | 调用 | 编号 | 调用 | 编号 | 调用 |
|------|------|------|------|------|------|------|------|
| 0 | setup | 17 | break¹ | 34 | nice | 51 | acct¹ |
| 1 | exit | 18 | stat | 35 | ftime¹ | 52 | phys¹ |
| 2 | fork | 19 | lseek | 36 | sync | 53 | lock¹ |
| 3 | read | 20 | getpid | 37 | kill | 54 | ioctl¹ |
| 4 | write | 21 | mount¹ | 38 | rename | 55 | fcntl |
| 5 | open | 22 | umount¹ | 39 | mkdir | 56 | mpx¹ |
| 6 | close | 23 | setuid | 40 | rmdir | 57 | setpgid |
| 7 | waitpid | 24 | getuid | 41 | dup | 58 | ulimit¹ |
| 8 | creat | 25 | stime | 42 | **pipe** | 59 | uname |
| 9 | link | 26 | ptrace¹ | 43 | times | 60 | umask |
| 10 | unlink | 27 | alarm | 44 | prof¹ | 61 | chroot |
| 11 | execve | 28 | fstat | 45 | brk | 62 | ustat¹ |
| 12 | chdir | 29 | pause | 46 | setgid | 63 | dup2 |
| 13 | time | 30 | utime | 47 | getgid | 64 | getppid |
| 14 | mknod | 31 | stty¹ | 48 | signal | 65 | getpgrp |
| 15 | chmod | 32 | gtty¹ | 49 | geteuid | 66 | setsid |
| 16 | chown | 33 | access | 50 | getegid | | |

¹ stub 返回 `-1` —— 与 Linux 0.01 自身的 `-ENOSYS` 完全一致（break / mount / umount / ptrace / stty / gtty / ftime / prof / acct / phys / lock / ioctl / mpx / ulimit / ustat）。
> **本内核比 0.01 强的一点**：`mknod`、`rename`、`chroot` 在 0.01 里也是 stub，这里都已真实现。

### 核心路径解析

| 调用 | 功能 |
|------|------|
| `sys_setup` | 挂载 MINIX 文件系统（dev 0x301） |
| `sys_fork` | Ring3 调用构造 16 项恢复帧 + 用户栈复制 |
| `sys_open` | 3 参数（filename / flag / mode）；`O_CREAT`/`O_TRUNC` + umask；fd 从 3 起 |
| `sys_waitpid` | 回收僵尸（WNOHANG / pid=-1；SIGCHLD 忽略时返回 ECHILD） |
| `sys_execve` | 加载 ELF32（LOAD 段拷贝、BSS 清零、argc/argv 用户栈构造、授权用户页） |
| `sys_pipe` | 单页环形缓冲、`sleep_on` 阻塞、EOF/SIGPIPE |
| `sys_signal` | `SIG_DFL`/`SIG_IGN`；SIGCHLD 忽略 → 子进程自动回收 |
| `sys_chdir`/`sys_chroot` | 相对路径解析 / 改变根目录 |
| `sys_stat`/`sys_fstat` | `struct stat`（0.01 布局） |

---

## 🗂️ 源码结构

```
linux0.01/
├── boot/          # 引导三阶段
│   ├── boot.s     # 512B 引导扇区
│   ├── setup.s    # 实模式→保护模式
│   └── head.s     # 分页 + IDT + system_call 入口 + sys_call_table
├── kernel/        # 内核核心
│   ├── main.c     # 初始化入口
│   ├── sched.c    # O(N) 轮转调度器 + do_timer
│   ├── process.c  # sys_fork / sys_exit / sys_waitpid / do_signal
│   ├── sys.c      # 系统调用实现（67 个）
│   ├── vsprintf.c # printk 格式化
│   └── panic.c    # 内核崩溃处理
├── mm/            # 内存管理
│   ├── memory.c   # 页帧分配器 + grant_user_pages（内存隔离授权）
│   └── page.s     # page_fault 处理
├── fs/            # 文件系统
│   ├── minix.c    # 超级块 + sys_setup
│   ├── buffer.c   # LRU 块缓冲 + sleep_on/wake_up
│   ├── bitmap.c   # inode/zone 位图
│   ├── inode.c    # inode 缓存
│   ├── file_dev.c # 文件读写
│   ├── namei.c    # 路径解析（相对/绝对）
│   └── pipe.c     # 管道（0.01 移植）
├── drivers/       # 设备驱动
│   ├── console.c  # VGA 文本模式
│   ├── keyboard.c # PS/2 键盘
│   ├── hd.c       # IDE 硬盘 PIO
│   ├── serial.c   # COM1 串口（无头测试）
│   └── tty_io.c   # TTY 层
├── init/          # 用户态初始化
│   └── shell.c    # 内核态 Shell（含 run_user_program）
├── user/          # 用户态编程工具链（lib.h/lib.c/crt.s + 示例程序）
├── lib/           # 内核侧 C 子集（string/ctype/malloc）
├── include/       # 头文件（含 unistd.h：int 0x80 包装宏）
├── tools/         # build.c（镜像拼接）· mkminix.c（MINIX 测试盘）
├── scripts/       # qemu-test.py（无头验证）· regress.sh（一键回归）· ppm2png.py（截图）
├── docs/          # 教学与设计文档（含 GIT-WORKFLOW.md 分支/版本规范）
└── Makefile       # 构建系统（工具链自动检测）
```

---

## 📝 编写并运行你自己的程序

这是本项目的核心玩法：**写一个 `main()`，`make prog`，在 QEMU 里 `exec` 它。**

```c
// user/myprog.c
#include "lib.h"

int main(int argc, char *argv[]) {
    printf("hi, %s! argc=%d\n", argv[1], argc);
    return 7;
}
```

```bash
make prog NAME=myprog        # 编译 myprog + 注入一个含 /hello 与 /myprog 的新镜像
```

`make prog` **每次都会重新生成 `minix.img`**（保留默认 `/hello` + 当前程序）。若要
一次性注入多个程序，直接调用 `tools/mkminix`：

```bash
tools/mkminix minix.img user/a.elf:a user/b.elf:b   # /a 和 /b 都注入
```

QEMU 里：

```
$ exec /myprog world
hi, world! argc=2
exec: child 1 exit_code=7
```

**用户态库 `user/lib.h`** 提供：
- `printf`（`%d %u %x %s %c %p` + 宽度/精度/`long` 修饰符）
- `unistd.h` 全部系统调用包装（`open/read/write/close/fork/waitpid/execve/pipe/stat/...`）
- `malloc/free`（0x310000–0x3FE000，first-fit + bump）、`opendir/readdir`
- 字符串 / `ctype` / `atoi`/`strtol`

**已内置示例**：`hello`（argv）· `catfile`（读文件）· `memtest`（堆复用）· `printf`（格式演示）· `ls`（列目录）· `str`（libc 演示）· `sigchld`（SIGCHLD 语义）· `pipedemo`（管道通信）· `sysdemo`（0.01 对齐 syscall）· `bigdir`（目录扩容）。
**基础应用程序**：`cat`（读文件输出）· `wc`（统计行/词/字节）· `grep`（行内搜索）· `cp`（复制文件）· `touch`（创建空文件）。

---

## 🧪 自动化验证

**一键回归**（8 个核心场景：exec / 管道 / chdir / 硬链接 / fork-waitpid / 信号 / 系统调用 / 内存隔离）：

```bash
make test                    # 等价于 scripts/regress.sh
```

手动无头验证（串口捕获 + sendkey 注入，输出精确文本到 stdout）：

```bash
python3 scripts/qemu-test.py --image Image --hda minix.img \
    --keys "ls\ncat /hello.txt\n"
```

> 注意：QEMU 的 writeback 会把测试期间的脏块刷进 `minix.img`，**每次测试前先**
> `rm -f minix.img && make minix.img` 重新生成干净磁盘（`make test` 已自动处理）。

---

## 🧭 学习地图

| 顺序 | 开始读 |
|------|--------|
| 1 | [`docs/INDEX.md`](docs/INDEX.md) — 全局地图与学习路径 |
| 2 | `docs/PREREQ-*.md` — 汇编 / C / 体系结构 / OS 理论前置（可选但推荐） |
| 3 | [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) — 先知道"做了什么 / 没做什么" |
| 4 | [`docs/TUTORIAL.md`](docs/TUTORIAL.md) + `docs/tutorial/` — 按文件逐行读源码 |
| 5 | [`docs/HLD.md`](docs/HLD.md) / [`docs/SRS.md`](docs/SRS.md) — 高层设计 / 需求背景 |
| 6 | [`docs/GIT-WORKFLOW.md`](docs/GIT-WORKFLOW.md) — 分支 / 提交 / 版本规范 |
| 7 | [`docs/NEXT-STEPS.md`](docs/NEXT-STEPS.md) — 保存的后续工作清单 |

> **权威顺序**：源码 > LIMITATIONS/TUTORIAL > HLD/SRS。文档与代码冲突时以源码为准。

---

## 🏳️ 设计原则

1. **忠实复现** — 系统调用编号与接口严格对齐 1991 年 Linux 0.01
2. **极简可用** — 内核源码约 5000 LOC，无 0.01 之后的现代特性堆砌
3. **可验证** — QEMU 单命令启动，进程 / 管道 / 文件系统 / 内存隔离全部可运行演示

## 🚫 排除特性（有意为之）

TCP/IP 网络栈 · 模块加载 (LKM) · SMP 多核 · 虚拟文件系统 (VFS) · 写时复制 (COW) · 动态链接 · 图形模式 · 浮点运算 · 电源管理 · 按需调页。
（ELF32 加载**已实现**——见 `sys_execve`；未实现 a.out。）

## 📄 许可

本项目仅用于学习和研究目的。原始 Linux 0.01 内核代码版权归 Linus Torvalds 所有。
本项目在 **GNU 通用公共许可证 v2 (GPLv2)** 下发布，以尊重原始 Linux 内核的许可条款。
