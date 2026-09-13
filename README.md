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
| **内存** | 4KB 分页、页帧分配器、**每进程独立地址空间**、**按需调页**（含**从可执行文件回读**）、**页回收**（零页/正文页/COW 共享）、**写时复制（COW）**、内核恒等映射 0–16MB 全 supervisor-only（越权只杀肇事进程） |
| **中断** | IDT 256 门、时钟 / 键盘 / **硬盘 IRQ14** / 系统调用（`int 0x80`） |
| **设备** | VGA 80×25 文本控制台（作为 fd 0/1/2 出现在描述符表里）、PS/2 键盘（含 Shift）、**中断驱动 IDE 硬盘**（IRQ14 + `sleep_on`/`wake_up`，超时兜底）、COM1 串口镜像 |
| **文件系统** | **MINIX v1 读写**（文件/目录增删、**硬链接、重命名、chroot**）、**权限模型**（owner/group/other + root，目录 x 位控制查找，Linux 0.01 规则）、LRU 块缓冲（脏块回写 + **每 5 秒定时回写**）、inode 缓存、相对路径 + `chdir` |
| **系统调用** | **67 个，编号与 1991 Linux 0.01 完全一致**（含管道、`stat/fstat`、`signal`、`uid/gid`、`umask`、`uname`…） |
| **Shell** | 内核态 26 条命令 + **Ring3 `/bin/sh`**（内建 cd/pwd/echo/exit/help，其余走 `/bin/<name>`；支持 **`<` `>` `>>` 与 `\|` 管道**） |

---

## 🚀 快速开始

### Linux（原生）

```bash
sudo apt install -y build-essential gcc-multilib qemu-system-x86 xorriso
make && make iso
qemu-system-i386 -cdrom kernel.iso -m 16M -boot d
```

### macOS（Homebrew）

```bash
brew install qemu xorriso i686-elf-gcc i686-elf-binutils
make            # Makefile 自动检测 i686-elf-* 交叉工具链
qemu-system-i386 -fda Image -m 16M -boot a
```

### Docker（平台无关）

```bash
docker build -t linux-0.01-builder .
docker run --rm -v $(pwd):/kernel -w /kernel linux-0.01-builder make clean all iso
qemu-system-i386 -cdrom kernel.iso -m 16M -boot d
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

$ exec /bin/hello a b            # fork + execve 进 Ring3 跑 ELF32（程序在 /bin）
hello from user program: argc=3
  argv[0] = /bin/hello
  argv[1] = a
  argv[2] = b
exec: child 1 exit_code=42

$ exec /bin/pipedemo             # 管道：父进程阻塞读子进程写入
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

**唯一权威：`include/memlayout.h`**（内核与用户态共用；汇编侧镜像 `include/memlayout.inc`）。
编译器期 `STATIC_ASSERT`、启动期 `mem_check()`、CI 里 `make check-layout`
三层把关，任何区域重叠都会**编译失败或 panic**，而不是静默写坏文件系统。

```
物理内存（恒等映射，内核视角；每个进程的页目录都含这部分，只读给内核用）
0x000000 ┌──────────────────┐
         │   BIOS + IVT     │
0x108000 ├──────────────────┤ ← 内核起始 (startup_32)
         │  内核代码/数据/BSS│  _end ≈ 0x29E68（上限 KERNEL_IMAGE_LIMIT = 0x30000）
         │  内核 bump 堆     │ ← lib/malloc.c，[0x30000, 0x40000)
0x100000 ├──────────────────┤ ← 页目录 (PGDIR) + 4 张内核页表（恒等映射 0-16MB）
0x105000 ├──────────────────┤ ← 页帧池：任务页 / 管道页 / **所有用户页**
         │  （空闲）         │  ← 16MB 内存下约 3700 页
0x350000 ├──────────────────┤ ← 缓冲区缓存窗口（实际落在 [0x3ADC00, 0x3F0000)）
0x3F0000 ├──────────────────┤ ← 缓存以上的空闲页
         │  mem_map 位图     │  ← 末尾几 KB
0x400000 └──────────────────┘   … 4MB 以上到 16MB 同样恒等映射、同样可分配
```

```
用户地址空间（每个进程一份页目录，PDE[32]；虚拟地址，不是物理地址）
0x08000000 ┌──────────────────┐ ← USER_PROG_START（ELF 链接地址）
           │  程序镜像         │  1MB 上限，按 ELF LOAD 段按需建页
0x08100000 ├──────────────────┤ ← 用户堆（malloc）
           │  …               │  首次访问时才分配（按需调页）
0x08200000 ├──────────────────┤
           │  保护空洞         │  访问这里 = SIGSEGV
0x08300000 ├──────────────────┤ ← 用户栈下界
           │  用户栈（向下长） │  首次访问时才分配
0x083FF000 ├──────────────────┤ ← 栈顶：argc/argv/sigreturn stub 所在尾页
0x08400000 └──────────────────┘ ← USER_WINDOW_TOP（正好一个页目录项）
```

> **M3 的地址空间拆分**：内核恒等映射 0–16MB（PDE[0..3]，全部 supervisor-only），
> 用户区只占 PDE[32] 一项，所以每个进程的额外开销是**两张页表页**（页目录 + 用户页表）。
> 于是 execve 可以把新镜像装进全新的物理页——这正是「Ring3 shell 能跑子程序」的前提。


> 内核映像在磁盘上约 93KB，但 `_end` 落在 0x29E68（约 168KB）——差别全在 BSS
> （页分配器位图、inode/file 表、tty 缓冲等静态数组）。`KERNEL_IMAGE_LIMIT`
> 曾按二进制大小误设为 0x18A000，被 `make check-layout` 的 `_end` 校验抓出来后修正。

> 缓存条数不是手写的：`NR_BUFFERS` 由内存地图派生（`include/linux/fs.h`），
> 所以缓存永远塞不进用户堆里。历史上 `NR_BUFFERS = 512` 曾把缓存放到 `~0x370000`
> 压在用户堆上——Ring0 无视 PTE 的 U/S 位，用户 `malloc` 的字节会和文件系统块缓冲
> 变成同一批物理页，写坏文件系统却毫无提示（见 `docs/LIMITATIONS.md` §2）。

### 关键事实（读源码前先记住）

1. **两个 Shell**：内核态 `$`（`main` 直接 `shell_main`，救援/调试入口）与 Ring3 的 `/bin/sh`
   （`exec /bin/sh`，自己 read 键盘、自己 fork+execve）；`int 0x80` 自动切回内核栈
2. **67 个系统调用，编号 = Linux 0.01**；`include/unistd.h` 提供 `int $0x80` 包装宏
3. **每进程独立地址空间**（M3）：内核恒等映射 0–16MB 全 supervisor-only，用户区在 PDE[32]；
   进程页首次访问才分配（按需调页），fork 用**写时复制**（`copy_page_tables`/`un_wp_page`）；
   Ring3 越权访问 → 终止肇事进程（SIGSEGV），内核继续运行。`memstat` 可看空闲页与
   缺页/COW 计数
4. **段选择子**：`KERNEL_CS=0x08` `KERNEL_DS=0x10` `USER_CS=0x1B` `USER_DS=0x23`
5. **MINIX FS** 挂 `minix.img`（dev 0x301）后 `ls`/`cat`/`wtest` 可实测读写；
   脏缓冲由**定时回写任务**每 5 秒刷盘（M2-3），不再依赖显式 `sync`

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
│   ├── memory.c   # ★ 页帧分配器 + 每进程页目录 + 按需调页 + COW + memstat
│   ├── page.s     # page_fault 处理（do_no_page）
│   └── memcheck.c # ★ 启动自检：内存地图不一致就 panic（带地图 dump）
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
│   ├── memlayout.h    # ★ 内存地图唯一权威（内核+用户态共用）
│   └── memlayout.inc  # ★ 汇编侧镜像（.equ 常量，与上面同步校验）
├── tools/         # build.c（镜像拼接）· mkminix.c（MINIX 测试盘）
├── scripts/       # qemu-test.py（无头验证）· regress.sh · check-layout.py（静态地图校验）· ppm2png.py
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
make prog NAME=myprog        # 编译 myprog + 注入一个含 /bin/hello 与 /bin/myprog 的新镜像
```

`make prog` **每次都会重新生成 `minix.img`**（保留默认 `/bin/hello` + 当前程序）。若要
一次性注入多个程序，直接调用 `tools/mkminix`：

```bash
tools/mkminix minix.img user/a.elf:a user/b.elf:b   # /a 和 /b 都注入
```

QEMU 里：

```
$ exec /bin/myprog world
hi, world! argc=2
exec: child 1 exit_code=7
```

**用户态库 `user/lib.h`** 提供：
- `printf`（`%d %u %x %s %c %p` + 宽度/精度/`long` 修饰符）
- `unistd.h` 全部系统调用包装（`open/read/write/close/fork/waitpid/execve/pipe/stat/...`）
- `malloc/free`（虚拟 `0x08100000`–`0x08200000`，first-fit + bump；页由内核按需提供）、`opendir/readdir`
- 字符串 / `ctype` / `atoi`/`strtol`

**已内置示例**：`hello`（argv）· `catfile`（读文件）· `memtest`（堆复用）· `printf`（格式演示）· `ls`（列目录）· `str`（libc 演示）· `sigchld`（SIGCHLD 语义）· `pipedemo`（管道通信）· `sysdemo`（0.01 对齐 syscall）· `bigdir`（目录扩容）· `bigalloc`（堆与缓冲区缓存不重叠）· `sigdemo`（自定义信号处理器）· `cowtest`（fork 写时复制隔离）· `demandtest`（按需调页）· `oomtest`（内存耗尽只杀肇事进程）· `sh`（Ring3 shell）· `echotest`（Ring3 stdin）。
**基础应用程序**：`cat`（读文件输出）· `wc`（统计行/词/字节）· `grep`（行内搜索）· `cp`（复制文件）· `touch`（创建空文件）。

---

## 🧪 自动化验证

**一键回归**（23 个场景：exec / 管道 / chdir / 硬链接 / fork-waitpid / 信号 / 系统调用 / 内存隔离 / 目录扩容 / 基础应用 / 堆与缓存不重叠 / 启动自检 / 自定义信号处理器 / **Ring3 shell** / **定时回写** / **写时复制** / **按需调页** / **内存耗尽** / **文件权限** / **shell 管道与重定向** / **中断驱动磁盘** / **内存压力下的页回收** / **信号投递时机**）：

```bash
make test                    # 等价于 scripts/regress.sh（23 个场景，TCG 下约 10.5 分钟）
make test-fast               # 快集：跳过 autosync/oom/evict 三个重场景（CI 的 PR 跑这个）
```

**静态校验**（纯 Python，不需要编译器，CI 里在构建之前先跑）：

```bash
make check                   # 下面三件的合集
make check-layout            # 区域重叠/硬编码地址/缓存装不下 → 非零退出
python3 scripts/check-layout.py --kernel kernel/system   # 另校验链接期 _end
make check-docs              # 文档里引用的布局常量/场景数/QEMU 内存与源码一致
make check-docs-selftest     # 反向测试：8 个已知坏样本必须被 check-docs 拦下
```

> `check-docs` 存在的原因：M3 把用户地址空间从固定物理地址搬到每进程窗口后，代码全改了、
> 几章教程没改——在**教学仓库**里，教一个内核已经没有的内存模型是最严重的文档 bug。
> 规则很简单：旧地址/旧宏只允许出现在**带历史标注**（`M3 前`、`历史`、`原实现`…）的行里，
> 文档里出现的 `0x08xxxxxx` 必须是 `include/memlayout.h` 里的常量，场景数必须等于
> `scripts/regress.sh` 实际跑的条数。

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
