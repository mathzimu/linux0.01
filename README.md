# Minimal Linux 0.01 Equivalent Kernel

> 一个可引导、功能等价于 1991 年 Linux 0.01 的极简操作系统内核。
> 用于研究、教学和逆向工程。

![Architecture](docs/HLD.md#1-架构概览)

## 功能特性

| 模块 | 能力 |
|------|------|
| **引导** | BIOS → 实模式引导扇区 → setup（A20/PIC/GDT）→ 保护模式 → head（分页/IDT）→ main |
| **进程管理** | task_struct 控制块、TSS 硬件上下文切换、最多 64 进程 |
| **任务调度** | 100Hz 时钟中断、O(N) 优先级轮转、抢占式 |
| **内存管理** | 4KB 分页、位图页帧分配器、恒等映射 0-4MB |
| **中断处理** | IDT 256 门、时钟/键盘/硬盘/系统调用 (int 0x80) |
| **设备驱动** | VGA 80×25 文本控制台、PS/2 键盘（含 Shift 处理）、IDE 硬盘 PIO |
| **文件系统** | MINIX v1 只读、LRU 块缓冲、inode 缓存、路径解析 |
| **系统调用** | setup/exit/fork/read/write/open/close/getpid/pause/time |
| **Shell** | echo / help / ps / clear / exit |

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
brew install qemu xorriso
# 编译推荐使用 Docker（见上）
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
│   ├── buffer.c   # LRU 块缓冲
│   ├── inode.c    # inode 缓存
│   ├── file_dev.c # 文件读写
│   └── namei.c    # 路径解析
├── drivers/       # 设备驱动
│   ├── console.c  # VGA 文本模式
│   ├── keyboard.c # 键盘驱动
│   ├── hd.c       # IDE 硬盘
│   └── tty_io.c   # TTY 层
├── init/          # 用户态初始化
│   └── shell.c    # Shell
├── lib/           # C 标准库子集
├── include/       # 头文件
├── tools/         # 构建工具
│   └── build.c    # 镜像拼接器
├── scripts/       # 辅助脚本
├── docs/          # 设计文档
│   ├── SRS.md     # 需求规格说明书
│   └── HLD.md     # 高层次架构设计
├── DEPENDENCIES.md # 依赖详细说明
├── Dockerfile     # Docker 构建环境
└── Makefile       # 构建系统
```

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
                      ├─ mem_init()
                      ├─ buffer_init()
                      ├─ tty_init()
                      ├─ sched_init()
                      ├─ sti() 开中断
                      ├─ move_to_user_mode() → 用户态(ring3)
                      └─ shell_main()
```

## 系统调用

| 编号 | 调用名 | 功能 |
|------|--------|------|
| 0 | `sys_setup` | 挂载 MINIX 文件系统 |
| 1 | `sys_exit`  | 进程退出 |
| 2 | `sys_fork`  | 创建子进程（TSS 切换） |
| 3 | `sys_read`  | 读取文件/键盘输入 |
| 4 | `sys_write` | 写入文件/控制台输出 |
| 5 | `sys_open`  | 打开文件 |
| 6 | `sys_close` | 关闭文件 |
| 7 | `sys_getpid`| 获取进程 ID |
| 8 | `sys_pause` | 进程休眠 |
| 9 | `sys_time`  | 获取系统时间 |

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
  echo   - Echo text
  help   - Show this help
  ps     - List processes
  clear  - Clear screen
  exit   - Exit shell

$ echo Hello World
Hello World

$ ps
PID   STATE   COUNTER
0     0       15
1     0       15
```

## 设计原则

1. **忠实复现** — 严格以 Linux 0.01 和 MINIX 设计为唯一参考
2. **极简可用** — 代码量约 4000 LOC，无任何 0.01 后特性
3. **可验证** — QEMU 单命令启动，可运行至少 2 个并发进程

## 排除特性

不包含：TCP/IP 网络栈、模块加载 (LKM)、SMP 多核、虚拟文件系统 (VFS)、
写时复制 (COW)、ELF 加载器、动态链接、图形模式、浮点运算、电源管理。

## 许可

本项目仅用于学习和研究目的。原始 Linux 0.01 内核代码的版权归 Linus Torvalds 所有。
本项目在 GNU 通用公共许可证 v2（GPLv2）下发布，以尊重原始 Linux 内核的许可条款。
