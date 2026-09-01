# Minimal Linux 0.01 Equivalent Kernel

> 一个可引导、功能等价于 1991 年 Linux 0.01 的极简操作系统内核。
> 用于研究、教学和逆向工程。

![Architecture](docs/HLD.md#1-架构概览)

## 功能特性

| 模块 | 能力 |
|------|------|
| **引导** | BIOS → 实模式引导扇区 → setup（A20/PIC/GDT）→ 保护模式 → head（分页/IDT）→ main |
| **进程管理** | task_struct 控制块、TSS 硬件上下文切换、最多 64 进程、zombie + waitpid 回收（退出码传递、任务页释放）、SIGCHLD 忽略自动回收 |
| **用户态** | `execve` 从 MINIX 加载 ELF32（C 程序，argc/argv 传递，`exec` 命令 = fork+execve+waitpid）、用户态 fork、可编程工具链（`make prog`） |
| **任务调度** | 100Hz 时钟中断、O(N) 优先级轮转、抢占式、alarm(SIGALRM) |
| **内存管理** | 4KB 分页、位图页帧分配器、恒等映射 0-4MB、**内存隔离**（内核页 U/S=0，按区授权用户页） |
| **中断处理** | IDT 256 门、时钟/键盘/硬盘/系统调用 (int 0x80) |
| **设备驱动** | VGA 80×25 文本控制台、PS/2 键盘（含 Shift 处理）、IDE 硬盘 PIO 读写、COM1 串口镜像 |
| **文件系统** | MINIX v1 读写（创建/删除文件与目录、**硬链接、重命名、chroot**）、LRU 块缓冲（脏块回写）、inode 缓存、相对路径 + chdir |
| **系统调用** | **67 个，编号与 1991 Linux 0.01 完全一致**（含 pipe 管道、stat/fstat、signal、uid/gid、umask、uname…见下表） |
| **Shell** | echo / help / ps / clear / exit / pid / time / sys / spawn / sig / ls / cat / sync / wtest / touch / mkdir / rm / rmdir / ppid / fdtest / seektest / wait / user / cd / stat / id / ln / mv |

## 系统调用（编号 = 1991 Linux 0.01 sys_call_table）

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

¹ stub 返回 -1 —— 与 Linux 0.01 自身的 `-ENOSYS` 完全一致（break/mount/umount/ptrace/stty/gtty/ftime/prof/acct/phys/lock/ioctl/mpx/ulimit/ustat）。
mknod/rename/chroot 在 0.01 里也是 stub，本内核已真实现。

## 快速开始

### Linux 原生构建

```bash
# 安装依赖
sudo apt install -y build-essential gcc-multilib qemu-system-x86 xorriso

# 编译并生成 ISO
make && make iso

# 运行
qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
```

### Docker（平台无关）

```bash
docker build -t linux-0.01-builder .
docker run --rm -v $(pwd):/kernel -w /kernel linux-0.01-builder make clean all iso
qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
```

### macOS（Homebrew）

```bash
brew install qemu xorriso i686-elf-gcc i686-elf-binutils
make          # 自动检测 i686-elf-* 交叉工具链
# 或使用 Docker（见上）
```

### 无头自动化验证（可选）

```bash
# 串口捕获 + sendkey 注入，输出精确文本到 stdout
python3 scripts/qemu-test.py --image Image --hda minix.img \
    --keys "ls\ncat /hello.txt\n"
```

### 构建产物

| 文件 | 说明 |
|------|------|
| `Image` | 1.44MB 软盘镜像（可直接 `-fda Image` 启动） |
| `kernel.iso` | El Torito 启动光盘（`-cdrom kernel.iso`） |

## 项目结构

```
linux0.01/
├── boot/          # 引导三阶段
│   ├── boot.s     # 512B 引导扇区，加载 setup+kernel
│   ├── setup.s    # 实模式→保护模式切换
│   └── head.s     # 分页+IDT+系统调用入口+中断处理
├── kernel/        # 内核核心
│   ├── main.c     # 初始化入口
│   ├── sched.c    # O(N) 轮转调度器
│   ├── process.c  # fork 实现
│   ├── sys.c      # 系统调用实现
│   ├── vsprintf.c # printk 格式化输出
│   └── panic.c    # 内核崩溃处理
├── mm/            # 内存管理
│   ├── memory.c   # 页帧分配器
│   └── page.s     # 缺页异常处理
├── fs/            # 文件系统
│   ├── minix.c    # MINIX v1 超级块
│   ├── buffer.c   # LRU 块缓冲 + sleep_on/wake_up
│   ├── bitmap.c   # inode/zone 位图
│   ├── inode.c    # inode 缓存
│   ├── file_dev.c # 文件读写
│   ├── namei.c    # 路径解析（相对/绝对）
│   └── pipe.c     # 管道（Linux 0.01 移植）
├── drivers/       # 设备驱动
│   ├── console.c  # VGA 文本模式
│   ├── keyboard.c # 键盘驱动
│   ├── hd.c       # IDE 硬盘（PIO 读写）
│   ├── serial.c   # COM1 串口（控制台镜像，供无头测试）
│   └── tty_io.c   # TTY 层
├── init/          # 用户态初始化
│   └── shell.c    # Shell（含系统调用/文件系统演示命令 + run_user_program）
├── user/          # 嵌入的用户态程序（user.s → user.bin → C 数组）
├── lib/           # C 标准库子集
├── include/       # 头文件（含 unistd.h：int 0x80 系统调用包装宏）
├── tools/         # 构建工具
│   ├── build.c    # 镜像拼接器
│   └── mkminix.c  # MINIX v1 测试磁盘制作工具
├── scripts/       # 辅助脚本
│   ├── qemu-test.py # 无头 QEMU 测试驱动（串口捕获 + sendkey）
│   └── ppm2png.py   # screendump PPM → PNG
├── docs/          # 教学与设计文档（从 docs/INDEX.md 进入）
│   ├── INDEX.md   # 学习路径总索引
│   ├── TUTORIAL.md / tutorial/  # 源码实现教程
│   ├── PREREQ-*.md # 前置知识
│   ├── LIMITATIONS.md # 已知简化（以源码为准）
│   ├── SRS.md / HLD.md  # 需求与高层设计（背景）
├── DEPENDENCIES.md # 依赖详细说明
├── Dockerfile     # Docker 构建环境
└── Makefile       # 构建系统
```

## 学习路径

1. 本文：编译运行、目录结构  
2. [`docs/INDEX.md`](docs/INDEX.md)：文档地图  
3. `docs/PREREQ-*.md`：汇编 / C / 体系结构 / OS 理论  
4. [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md)：当前实现边界  
5. [`docs/TUTORIAL.md`](docs/TUTORIAL.md) + `docs/tutorial/`：按文件读源码
6. [`docs/NEXT-STEPS.md`](docs/NEXT-STEPS.md)：后续工作清单（保存的 TODO）  

**关键事实：** Shell 在 **内核态** 运行（`main` 直接 `shell_main`）；`execve` 从 MINIX 加载 ELF32 到 0x200000 并 iret 进 **Ring3**（用户栈 0x3FF000），用户程序经 `int 0x80` 调系统调用后 `exit`（`make prog NAME=xxx` 编译并注入用户程序）；
分页恒等映射 **0–4MB**，**内存隔离**（页表默认 U/S=0，仅用户程序/堆/栈页被 `grant_user_pages` 授权，越权访问 → page fault panic）；fork/调度/信号/管道/文件读写均可通过 Shell 命令实测（`spawn`/`sig`/`ls`/`cat`/`wtest`/`exec /pipedemo`）。

## 启动流程

```
BIOS POST
  └─ boot.s 加载到 0x7C00
       ├─ 加载 setup.s → 0x10000（CHS 0,0,2, 4扇区）
       ├─ 加载 kernel  → 0x10800（CHS 0,0,6+）
       └─ 跳转 setup.s
            ├─ 读取硬件参数（INT 15h）
            ├─ 启用 A20 Gate
            ├─ 初始化 8259A PIC
            ├─ 设置临时 GDT
            ├─ 进入保护模式（CR0.PE=1）
            └─ 跳转 head.s（0x10800）
                 ├─ 设置页目录+页表（恒等映射 0-4MB）
                 ├─ 启用分页（CR0.PG=1）
                 ├─ 设置 IDT（256中断门）
                 ├─ 加载内核 GDT
                 └─ call main()
                      ├─ mem_init() / buffer_init() / tty_init()
                      ├─ sys_setup() / sched_init()
                      ├─ sti() 开中断
                      └─ shell_main()   ← 内核态 Shell（不返回）
```

## 系统调用

完整的 67 项编号表见上文（= Linux 0.01 `sys_call_table`）。核心路径：

| 调用 | 功能 |
|------|------|
| `sys_setup` | 挂载 MINIX 文件系统（dev 0x301） |
| `sys_fork` | 创建子进程（Ring3 调用构造 16 项恢复帧 + 用户栈复制） |
| `sys_open` | 打开/创建文件（3 参数 filename/flag/mode；O_CREAT/O_TRUNC；fd 从 3 起） |
| `sys_waitpid` | 等待并回收僵尸（WNOHANG、pid=-1、SIGCHLD 忽略时返回 ECHILD） |
| `sys_execve` | 从 MINIX 加载 ELF32 并在 Ring3 运行（LOAD 段加载、BSS 清零、argc/argv 用户栈构造、授权用户页） |
| `sys_pipe` | 管道（单页环形缓冲、sleep_on 阻塞、EOF/SIGPIPE） |
| `sys_link`/`sys_rename` | 硬链接 / 重命名（0.01 里 rename 是 stub，本内核已实现） |
| `sys_stat`/`sys_fstat` | 文件状态（struct stat，0.01 布局） |
| `sys_signal` | 信号处置（SIG_DFL/SIG_IGN；SIGCHLD 忽略 → 子进程自动回收） |
| `sys_chdir`/`sys_chroot` | 相对路径解析 / 改变根目录 |

Shell 通过 `include/unistd.h` 的 `int $0x80` 包装宏实际调用上述接口
（`sys`/`spawn`/`sig`/`ls`/`cat`/`wtest`/`exec /xxx` 等），系统调用路径可运行验证。
用户程序用 `make prog NAME=xxx` 编译注入，`exec /xxx` 运行。

## 内存布局

```
0x000000 ┌──────────────────┐
         │    BIOS + IVT    │
0x100000 ├──────────────────┤ ← 页目录 (PGDIR)
0x101000 ├──────────────────┤ ← 页表 0 (PGTBL0, 映射 0-4MB)
0x108000 ├──────────────────┤ ← 内核起始 (startup_32)
         │  head.s 代码段    │
         │  kernel 数据段    │
         │  kernel BSS       │
         │  内核堆           │
         │  缓冲池 (~2MB)    │
         │  进程数据         │
0x400000 └──────────────────┘ ← 4MB (默认内存上限)
```

## Shell 命令

```
$ help
  echo    - Echo text
  help    - Show this help
  ps      - List processes
  clear   - Clear screen
  exit    - Exit shell
  pid     - getpid() via int 0x80
  time    - uptime via sys_time
  sys     - syscall path demo (getpid/time/write/open/close)
  spawn   - fork() demo: two children print and exit
  sig     - pause/kill demo: SIGINT kills a child
  ls      - list a directory (default: current, via open/read)
  cd      - change directory (sys_chdir, relative paths OK)
  stat    - stat() a file (ino/size/mode/nlink/uid/gid)
  id      - uid/euid/gid/egid/pgrp
  ln      - hard link (sys_link)
  mv      - rename (sys_rename)
  cat     - print a file (via open/read/close)
  sync    - write back dirty buffers/inodes
  wtest   - write a file [path] through the write path
  touch   - create a file (sys_mknod)
  mkdir   - create a directory (sys_mkdir)
  rm      - delete a file (sys_unlink)
  rmdir   - delete an empty directory (sys_rmdir)
  ppid    - getppid() demo
  fdtest  - dup() demo: fds share the file offset
  seektest- lseek() demo: SEEK_SET / SEEK_END
  wait    - fork + waitpid demo: child exit(42)
  exec    - fork+execve: run an ELF from the MINIX fs
  user    - run the embedded Ring3 user program

$ cd /docs
$ ls
5  .
1  ..
6  note.txt
$ cat note.txt
A file inside a subdirectory.

$ ln /hello.txt /hard
$ stat /hello.txt
stat /hello.txt: ino=2 size=21 mode=0100644 nlink=2 uid=0 gid=0
$ exec /pipedemo
pipe: read fd=3 write fd=4
parent read 26 bytes: "hello from child via pipe!"
```

## 编写并运行你自己的程序

```c
// user/myprog.c —— 写一个 main() 即可
#include "lib.h"
int main(int argc, char *argv[]) {
    printf("hi, %s! argc=%d\n", argv[1], argc);
    return 7;
}
```
```bash
make prog NAME=myprog        # 编译 + 注入 minix.img（保留已有程序）
# QEMU 里：
$ exec /myprog world
hi, world! argc=2
exec: child 1 exit_code=7
```

用户态库 `user/lib.h` 提供：`printf`（%d %u %x %s %c %p + 宽度/精度/long）、
`unistd.h` 的全部系统调用包装（open/read/write/close/fork/waitpid/execve/mkdir/rm/pipe/stat...）、
字符串/ctype/atoi/strtol、`opendir/readdir`。已内置示例：`hello.c`（printf + argv）、
`catfile.c`（读文件）、`memtest.c`（malloc/free）、`printf.c`（格式演示）、`ls.c`（列目录）、
`str.c`（libc 演示）、`sigchld.c`（SIGCHLD 语义）、`pipedemo.c`（管道通信）、`sysdemo.c`（0.01 对齐 syscall）。

## MINIX 测试磁盘

```bash
make minix.img                 # 用 tools/mkminix.c 生成 128KB MINIX v1 镜像
qemu-system-i386 -fda Image -hda minix.img -m 4M -boot a
# 然后可在 Shell 中: ls /  cat /hello.txt  wtest  cat /hello.txt
```

`minix.img` 含根目录（hello.txt / readme.txt / big.txt(18KB，走间接块) / docs/note.txt）。

## 设计原则

1. **忠实复现** — 系统调用编号与接口严格对齐 1991 Linux 0.01
2. **极简可用** — 内核源码约 5000 LOC，无 0.01 之后的现代特性
3. **可验证** — QEMU 单命令启动，进程/管道/文件系统全部可运行演示

## 排除特性

不包含：TCP/IP 网络栈、模块加载 (LKM)、SMP 多核、虚拟文件系统 (VFS)、
写时复制 (COW)、动态链接、图形模式、浮点运算、电源管理、按需调页。
（ELF32 加载已实现——见 `sys_execve`；未实现 a.out。）

## 许可

本项目仅用于学习和研究目的。原始 Linux 0.01 内核代码的版权归 Linus Torvalds 所有。
本项目在 GNU 通用公共许可证 v2（GPLv2）下发布，以尊重原始 Linux 内核的许可条款。
