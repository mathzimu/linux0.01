# Linux 0.01 内核实现教程

## 目录

- [前言：学习目标与前置知识](#前言学习目标与前置知识)
- [第一章：x86 架构基础](#第一章x86-架构基础)
  - [1.1 寄存器体系](#11-寄存器体系)
  - [1.2 实模式与保护模式](#12-实模式与保护模式)
  - [1.3 分段机制](#13-分段机制)
  - [1.4 分页机制](#14-分页机制)
  - [1.5 中断与异常](#15-中断与异常)
  - [1.6 I/O 端口](#16-io-端口)
- [第二章：引导流程（Boot Process）](#第二章引导流程boot-process)
  - [2.1 BIOS 与 MBR](#21-bios-与-mbr)
  - [2.2 boot.s — 第一阶段引导](#22-boots--第一阶段引导)
  - [2.3 tools/build.c — 镜像构建工具](#23-toolsbuildc--镜像构建工具)
  - [2.4 setup.s — 实模式到保护模式的桥梁](#24-setups--实模式到保护模式的桥梁)
  - [2.5 head.s — 32 位内核入口](#25-heads--32-位内核入口)
  - [2.6 完整引导流程总结](#26-完整引导流程总结)
- [第三章：保护模式核心机制](#第三章保护模式核心机制)
  - [3.1 GDT（全局描述符表）](#31-gdt全局描述符表)
  - [3.2 IDT（中断描述符表）](#32-idt中断描述符表)
  - [3.3 TSS（任务状态段）](#33-tss任务状态段)
  - [3.4 LDT（局部描述符表）](#34-ldt局部描述符表)
  - [3.5 页目录与页表](#35-页目录与页表)
- [第四章：中断处理系统](#第四章中断处理系统)
  - [4.1 8259A PIC 初始化](#41-8259a-pic-初始化)
  - [4.2 时钟中断（IRQ0）](#42-时钟中断irq0)
  - [4.3 键盘中断（IRQ1）](#43-键盘中断irq1)
  - [4.4 硬盘中断（IRQ14）](#44-硬盘中断irq14)
  - [4.5 系统调用入口（int 0x80）](#45-系统调用入口int-0x80)
  - [4.6 页错误处理](#46-页错误处理)
  - [4.7 异常处理](#47-异常处理)
- [第五章：内核初始化（main.c）](#第五章内核初始化mainc)
  - [5.1 主函数的结构](#51-主函数的结构)
  - [5.2 内存检测](#52-内存检测)
  - [5.3 各子系统初始化](#53-各子系统初始化)
  - [5.4 进入用户态](#54-进入用户态)
- [第六章：进程管理](#第六章进程管理)
  - [6.1 task_struct 数据结构](#61-task_struct-数据结构)
  - [6.2 进程初始化](#62-进程初始化)
  - [6.3 fork 系统调用实现](#63-fork-系统调用实现)
  - [6.4 exit 系统调用实现](#64-exit-系统调用实现)
  - [6.5 进程状态管理](#65-进程状态管理)
- [第七章：调度器](#第七章调度器)
  - [7.1 调度器设计](#71-调度器设计)
  - [7.2 时钟中断处理](#72-时钟中断处理)
  - [7.3 上下文切换](#73-上下文切换)
  - [7.4 sleep_on / wake_up 机制](#74-sleep_on--wake_up-机制)
- [第八章：内存管理](#第八章内存管理)
  - [8.1 物理内存布局](#81-物理内存布局)
  - [8.2 页框分配器](#82-页框分配器)
  - [8.3 页表管理](#83-页表管理)
  - [8.4 段内存访问](#84-段内存访问)
- [第九章：文件系统](#第九章文件系统)
  - [9.1 MINIX v1 文件系统布局](#91-minix-v1-文件系统布局)
  - [9.2 超级块管理](#92-超级块管理)
  - [9.3 缓冲区缓存](#93-缓冲区缓存)
  - [9.4 Inode 缓存](#94-inode-缓存)
  - [9.5 文件读写](#95-文件读写)
  - [9.6 路径解析（namei）](#96-路径解析namei)
  - [9.7 Bitmap 分配器](#97-bitmap-分配器)
- [第十章：设备驱动](#第十章设备驱动)
  - [10.1 控制台驱动（VGA 文本模式）](#101-控制台驱动vga-文本模式)
  - [10.2 键盘驱动](#102-键盘驱动)
  - [10.3 硬盘驱动（IDE PIO）](#103-硬盘驱动ide-pio)
  - [10.4 TTY 子系统](#104-tty-子系统)
- [第十一章：系统调用](#第十一章系统调用)
  - [11.1 系统调用机制](#111-系统调用机制)
  - [11.2 各系统调用详解](#112-各系统调用详解)
- [第十二章：用户态 Shell](#第十二章用户态-shell)
  - [12.1 Shell 初始化](#121-shell-初始化)
  - [12.2 命令实现](#122-命令实现)
- [第十三章：构建系统与运行](#第十三章构建系统与运行)
  - [13.1 Makefile 构建流程](#131-makefile-构建流程)
  - [13.2 链接脚本（kernel.ld）](#132-链接脚本kernelld)
  - [13.3 QEMU 运行与调试](#133-qemu-运行与调试)

---

## 前言：学习目标与前置知识

### 为什么要学习这个项目？

Linux 0.01 是 Linus Torvalds 于 1991 年发布的第一个公开 Linux 内核版本。它是最小的、可启动的、多任务的操作系统内核，代码总量约 4000 行，是理解操作系统核心原理的最佳入口。

本教程逐行分析 Linux 0.01 的完整实现，帮助你从零理解一个操作系统是如何构建的。学完本教程后，你将能够：

- 理解 x86 CPU 从通电到运行用户程序的完整流程
- 掌握保护模式、分段、分页等核心硬件机制
- 实现进程管理、调度、内存管理、文件系统等 OS 核心子系统
- 独立编写一个可启动的微型操作系统

### 需要的前置知识

| 知识领域 | 具体要求 |
|----------|---------|
| C 语言 | 指针、结构体、位运算、函数指针、内联汇编 |
| x86 汇编 | AT&T 语法（或 Intel 语法并能转换），寄存器、指令格式 |
| 计算机组成原理 | 内存编址、I/O 端口、中断向量表、DMA/PIO 概念 |
| 操作系统概念 | 进程、调度、分页、文件系统、缓冲区等基本概念 |

### 学习建议

1. **搭建环境先行**：先按 README 搭建 Docker/QEMU 环境，确保能编译运行
2. **从引导流程开始**：按本教程顺序逐章阅读，每章配合源码理解
3. **动手修改验证**：尝试修改代码、添加 printk，观察运行结果
4. **对比 Linux 0.01 原版**：本实现基于原版做了现代化改进，可对照学习

---

## 第一章：x86 架构基础

在开始阅读代码之前，必须先理解 x86 CPU 的基本工作机制。

### 1.1 寄存器体系

x86 32 位（i386）CPU 包含以下寄存器组：

**通用寄存器（8 个 32 位）：**

```
寄存器     名称                    16位模式偏移  低8位    高8位+(8位扩展)
EAX      累加器 (Accumulator)       AX           AL       AH
EBX      基址寄存器 (Base)           BX           BL       BH
ECX      计数器 (Counter)           CX           CL       CH
EDX      数据寄存器 (Data)          DX           DL       DH
ESI      源变址 (Source Index)     SI           —        —
EDI      目标变址 (Dest Index)     DI           —        —
EBP      基址指针 (Base Pointer)   BP           —        —
ESP      栈指针 (Stack Pointer)    SP           —        —
```

每个 32 位寄存器的低 16 位可独立访问（如 AX），AX/BX/CX/DX 的低 8 位和高 8 位也可独立访问。

**段寄存器（6 个 16 位）：**

```
寄存器     名称              用途
CS        代码段 (Code)      指向当前执行的代码段
DS        数据段 (Data)      通用数据段
SS        栈段 (Stack)       指向当前栈
ES        附加段 (Extra)     通用附加段
FS        附加段 F           通用附加段
GS        附加段 G           通用附加段
```

在实模式下，段寄存器直接存储段基址的高 16 位（实地址 = 段寄存器 × 16 + 偏移）。
在保护模式下，段寄存器存储**段选择子**，指向 GDT/LDT 中的描述符。

**控制寄存器：**

```
CR0    系统控制标志（PE=保护模式使能, PG=分页使能, WP=写保护, ...）
CR1    保留
CR2    页错误线性地址（发生页错误时硬件自动填入）
CR3    页目录基址（PDBR）
CR4    架构扩展标志（PAE, PSE, ...）
```

**其他关键寄存器：**

```
EIP     指令指针（下一条指令的地址）
EFLAGS  标志寄存器（IF=中断使能, ZF=零标志, CF=进位标志, ...）
GDTR    全局描述符表寄存器（基址 + 界限）
IDTR    中断描述符表寄存器（基址 + 界限）
LDTR    局部描述符表寄存器
TR      任务寄存器（指向当前 TSS）
```

### 1.2 实模式与保护模式

CPU 上电后默认工作在**实模式**：

**实模式（Real Mode）：**
- 16 位操作，最大寻址 1MB（0x00000-0xFFFFF）
- 地址计算：`物理地址 = 段寄存器 × 16 + 偏移`
- 无内存保护，任何程序可访问任意地址
- 中断向量表固定在 0x00000（IVT，256 个 4 字节入口）
- 可直接调用 BIOS 中断服务（int 0x10 显示, int 0x13 磁盘, int 0x15 内存检测...）

**保护模式（Protected Mode）：**
- 32 位操作，最大寻址 4GB
- 段寄存器存储选择子，通过描述符表间接访问
- 4 级特权级（Ring 0-3），操作系统运行在 Ring 0，用户程序在 Ring 3
- 硬件内存保护（段限长检查、特权级检查、页级保护）
- 支持分页机制，实现虚拟内存

**模式切换：**
从实模式切换到保护模式需要以下步骤：

```
1. CLI               // 关中断（保护模式下需要 IDT，先用 CLI 阻止中断）
2. LGDT [gdt_descr]  // 加载全局描述符表
3. MOV CR0, PE=1     // 设置 CR0 的保护模式使能位
4. JMP far           // 远跳转刷新预取队列，加载 CS 选择子
```

### 1.3 分段机制

在保护模式下，每个内存访问都经过段机制转换：

```
逻辑地址 (Selector:Offset)
     │
     ▼
从 GDT/LDT 中根据 Selector 查找段描述符
     │
     ▼
段描述符中包含：
    - 段基址 (Base Address, 32 位)
    - 段限长 (Limit, 20 位 + G 粒度)
    - 访问权限 (DPL, Type, ...)
     │
     ▼
线性地址 = 段基址 + 偏移
     │
     ▼
(如果启用分页) → 物理地址
```

**段选择子结构（16 位）：**

```
Bit 15-3    Bit 2    Bit 1-0
  索引      TI 位    RPL
  └ 在 GDT/LDT 中的位置
            └ 0=GDT, 1=LDT
                     └ 请求特权级
```

**段描述符结构（8 字节）：**

```
字节 7:   Base[31:24]  G D/B 0 AVL  Limit[19:16]
字节 6:   P DPL S  Type[3:0]
字节 5:   Base[23:16]
字节 4:   Base[15:0]
字节 3:   Limit[15:0]
字节 2:   Limit[15:0]
字节 1:   Base[15:0]
字节 0:   Base[15:0]
```

关键字段说明：
- **P (Present)**：段是否存在（1=有效）
- **DPL (Descriptor Privilege Level)**：描述符特权级（0-3）
- **S (System)**：0=系统段（TSS/LDT），1=代码/数据段
- **Type**：代码段（可执行、可读、一致...）/ 数据段（可写、向下扩展...）
- **G (Granularity)**：0=字节粒度，1=4KB 粒度
- **D/B**：0=16位，1=32位

在本项目中，我们使用**平坦内存模型**（Flat Model）：所有段的基址都为 0，限长为 4GB，这样逻辑地址 = 线性地址，分段机制"透明"。

### 1.4 分页机制

分页将线性地址映射到物理地址。启用分页需要设置 CR0.PG = 1。

**两级页表结构：**

```
线性地址 (32 位):
┌──────────┬──────────┬────────────┐
│ Dir(10位)│ Table(10)│ Offset(12) │
│ PDE 索引  │ PTE 索引  │ 页内偏移    │
└──────────┴──────────┴────────────┘
```

- **页目录（Page Directory）**：1024 个 PDE（Page Directory Entry），每个 4 字节
- **页表（Page Table）**：每个页表 1024 个 PTE（Page Table Entry），每个 4 字节
- **页**：4KB（4096 字节）
- 一个页目录可映射 1024 × 1024 × 4KB = 4GB 地址空间

**PDE/PTE 结构（32 位）：**

```
Bit 31-12       Bit 11-9  Bit 8  Bit 7  Bit 6  Bit 5  Bit 4  Bit 3  Bit 2  Bit 1  Bit 0
物理页框地址[31:12] AVL    G     PS    D     A     PCD    PWT    U/S    R/W    P
                                     └1=4MB └脏位  └访问位
```

- **P (Present)**：页是否存在
- **R/W (Read/Write)**：0=只读，1=可写
- **U/S (User/Supervisor)**：0=超级用户(Ring 0-2)，1=用户(Ring 3)
- **A (Accessed)**：CPU 自动设置，表示页被访问过
- **D (Dirty)**：CPU 自动设置，表示页被写过

CR3 寄存器保存页目录的物理地址。

**地址转换流程：**

```
1. 从线性地址取 Bit 31-22 作为 PDE 索引
2. 用 CR3 中的页目录基址 + 索引 × 4 找到 PDE
3. 从 PDE 取 Bit 31-12 作为页表基址
4. 从线性地址取 Bit 21-12 作为 PTE 索引
5. 用页表基址 + 索引 × 4 找到 PTE
6. 从 PTE 取 Bit 31-12 作为物理页框地址
7. 物理地址 = 页框地址 + 线性地址 Bit 11-0（页内偏移）
```

**TLB（Translation Lookaside Buffer）**：页表缓存，修改页表后需要刷新：
- 刷新单个地址：`invlpg [addr]`
- 刷新全部：重新加载 CR3（`mov cr3, eax`）

### 1.5 中断与异常

**中断（Interrupt）**：由外部设备通过 INTR 引脚或软件 int 指令触发。

**异常（Exception）**：由 CPU 在执行指令过程中检测到错误条件触发。

x86 定义了 256 个中断/异常向量（0-255）：

```
向量    类型      说明
0      异常      #DE  除法错误
1      异常      #DB  调试异常
2      中断      NMI  不可屏蔽中断
3      异常      #BP  断点（int 3）
4      异常      #OF  溢出（into）
5      异常      #BR  越界（bound）
6      异常      #UD  无效操作码
7      异常      #NM  设备不可用（无 FPU）
8      异常      #DF  双重错误
9      异常      —    FPU 段溢出（386）
10     异常      #TS  无效 TSS
11     异常      #NP  段不存在
12     异常      #SS  栈段错误
13     异常      #GP  一般保护错误
14     异常      #PF  页错误
16     异常      #MF  FPU 错误
32-47  中断      IRQ0-IRQ15（由 8259A PIC 映射）
128    中断      int 0x80  Linux 系统调用
```

响应中断/异常时，CPU 自动执行：

```
1. 如果发生特权级切换（如 Ring 3 → Ring 0）：
   a. 从 TSS 加载 SS0 和 ESP0（Ring 0 栈）
   b. 压入旧 SS 和 ESP
2. 压入 EFLAGS
3. 压入 CS 和 EIP
4. 压入错误码（仅部分异常）
5. 加载 IDT 中对应门描述符的 CS:EIP
6. 跳转到处理程序
```

### 1.6 I/O 端口

x86 通过独立的 I/O 地址空间（与内存地址空间分离）访问外设：

```
IN  AL/AX/EAX, port      // 从端口读取
OUT port, AL/AX/EAX      // 向端口写入
INSB/INSW/INSL           // 从端口读取字符串
OUTSB/OUTSW/OUTSL        // 向端口写入字符串
```

本项目使用的关键 I/O 端口：

```
端口          设备              用途
0x20/0x21    主 PIC (8259A)    中断控制
0xA0/0xA1    从 PIC (8259A)    中断控制
0x40-0x43    PIT (8253/8254)   定时器
0x60/0x64    键盘控制器        键盘数据/状态
0x3D4/0x3D5  VGA CRT 控制器   光标位置
0x1F0-0x1F7  IDE 主控制器     硬盘读写
0x80-0x8F    DMA 页寄存器      -
0x92         系统控制端口 A    A20 门控制
```

---

## 第二章：引导流程（Boot Process）

引导流程是操作系统启动的第一步，也是最关键的一步。本节将详细解释从 CPU 上电到跳转到 C 语言 `main()` 函数的全过程。

### 2.1 BIOS 与 MBR

**CPU 上电后的初始状态：**

```
CS = 0xF000     → 物理地址 0xFFFF0（实模式：0xF000 × 16 + 0xFFF0）
IP = 0xFFF0     → 第一条指令在 ROM BIOS 中
CR0.PE = 0      → 实模式
EFLAGS = 0x00000002（保留的 Bit 1 = 1）
```

**BIOS POST（Power-On Self Test）流程：**

1. 检测硬件（内存、键盘、磁盘等）
2. 初始化中断向量表（IVT）到 0x00000
3. 初始化 BIOS 数据区（BDA）到 0x00400
4. 扫描可引导设备（按 CMOS 设置的引导顺序）
5. 找到引导设备后，读取第一个扇区（512 字节）到 0x7C00
6. 验证最后两字节是否为 0x55 0xAA（引导签名）
7. 跳转到 0x0000:0x7C00 执行引导代码

**为什么是 0x7C00？**
这是历史原因。最早的 IBM PC（1981）需要为 BIOS 和 DOS 保留内存，0x7C00 = 32KB - 1024 字节是一个合理的边界。后来的标准保持了这一惯例。

**BIOS 提供的初始环境（在跳转到 0x7C00 时）：**

```
CS:IP = 0x0000:0x7C00  或  0x07C0:0x0000（两种可能）
DL     = 引导驱动器号（0x00=软盘A, 0x80=硬盘C）
其余段寄存器 = 0x0000（不保证）
栈     = 未定义，需要自行设置
实模式中断 = BIOS 已初始化，可直接调用
```

### 2.2 boot.s — 第一阶段引导

`boot/boot.s` 是 512 字节的引导扇区代码，它的任务是：
1. 将自己从 0x7C00 重定位到 0x90000
2. 通过 BIOS int 0x13 从磁盘读取 setup 和 system 到内存
3. 跳转到 setup 代码（0x90200）

**代码逐段分析：**

```as
SYSSIZE = 0x1000        // 系统最大 4MB（0x1000 个扇区 × 512B）
SETUPLEN = 4            // setup 占 4 个扇区
BOOTSEG  = 0x07C0       // 原始加载地址（段形式）
INITSEG  = 0x9000       // 重定位目标地址
SETUPSEG = 0x9020       // setup 加载地址
SYSSEG   = 0x1000       // system 加载地址（64KB 边界）
ENDSEG   = SYSSEG + SYSSIZE
```

**技巧：段地址 + 偏移**
实模式下物理地址 = 段 × 16 + 偏移。我们使用 0x9000:0x0000 表示物理地址 0x90000。用段:偏移的形式可以访问超过 64KB 的数据。

**重定位代码段：**

```as
_start:
    mov ax, #BOOTSEG          // ax = 0x07C0
    mov ds, ax                // ds = 0x07C0
    mov ax, #INITSEG          // ax = 0x9000
    mov es, ax                // es = 0x9000
    mov cx, #256              // 复制 256 个字 = 512 字节
    sub si, si                 // si = 0 (源: ds:si = 0x07C0:0x0000)
    sub di, di                 // di = 0 (目标: es:di = 0x9000:0x0000)
    rep
    movw                       // 将自身从 0x7C00 复制到 0x90000
```

这是实模式下最常用的内存块复制模式：`REP MOVSW` 重复执行 `CX` 次字复制。

**跳转到新位置继续执行：**

```as
    jmpi    go, INITSEG      // 段间跳转：CS=INITSEG, IP=go
go: mov ax, cs               // 设置 DS, SS, SP
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, #0xFF00          // 栈顶设在 0x9FF00
```

跳转到新位置后，CS 变为 0x9000，所有后续代码都在 0x90000 附近运行。设置栈指针到 0x9FF00，向下增长。

**加载 setup 模块：**

```as
load_setup:
    mov dx, #0x0000          // DH=磁头0, DL=驱动器号
    mov cx, #0x0002          // CH=柱面0, CL=扇区2
    mov bx, #0x0200          // 加载到 es:bx = 0x9000:0x0200 = 0x90200
    mov ax, #0x0200 + SETUPLEN  // AH=02(读), AL=4(扇区数)
    int 0x13                 // BIOS 磁盘读
    jnc ok_load_setup        // CF=0 表示成功
    mov dx, #0x0000          // 失败则复位磁盘
    mov ax, #0x0000
    int 0x13
    jmp load_setup           // 重试
```

**BIOS int 0x13 AH=02h 参数说明：**

```
AH = 02h         功能号：读扇区
AL = 扇区数      要读取的扇区数（1-128）
CH = 柱面低8位  柱面号的低 8 位
CL = 扇区号      低 6 位是扇区号（1-63），高 2 位是柱面号的高 2 位
DH = 磁头号     磁头号
DL = 驱动器号    0x00=软盘A, 0x80=硬盘C
ES:BX = 缓冲区   数据加载目标
```

**加载 system 模块：**

```as
load_system:
    mov dx, #0x0000
    mov cx, #0x0005          // 从柱面0, 扇区5开始
    mov ax, #SYSSEG          // es = 0x1000
    mov es, ax
    mov bx, #0x0000          // 加载到 0x1000:0x0000 = 0x10000

rp_read:
    mov ax, es
    cmp ax, #ENDSEG          // 检查是否读完
    jb ok1_read              // 如果 es < ENDSEG，继续读
    ret
```

**多扇区读取循环：**

```as
ok1_read:
    mov ax, #0x0080          // 一次读 128 个扇区（64KB）
    sub ax, bx               // 计算还有多少字节到达 64KB 边界
    shr ax, #1               // 转换为扇区数
    jz cls_buffer            // 如果是 0，跳转
    xor ah, ah
    sub ax, cx
    // ... 计算要读的扇区数

    mov ah, #2               // 读扇区
    int 0x13
    jnc next_seg             // 成功，处理下一段
```

这个循环一次读取 128 个扇区（64KB），因为实模式下段大小限制为 64KB。每读完 64KB 就切换 ES 到下一个段。

**启动扇区签名：**

```as
    .org 510
    .word 0xAA55             // BIOS 引导签名
```

`.org 510` 将位置计数器设为 510，确保 0xAA55 出现在扇区末尾两字节，BIOS 才能识别这是有效的引导扇区。

### 2.3 tools/build.c — 镜像构建工具

`tools/build.c` 将三个模块合并为一个可启动的磁盘镜像：

```
┌────────────────────┐  扇区 0
│   boot.bin (512B)  │  引导扇区
├────────────────────┤  扇区 1-4
│   setup.bin        │  setup 模块（4 个扇区）
├────────────────────┤  扇区 5+
│   system.bin       │  内核镜像（原始位置无关代码）
└────────────────────┘
```

**关键实现细节：**

```c
#define SETUP_SECTORS 4
#define BOOT_SIZE 512
#define SECTOR_SIZE 512

// 计算内核占用扇区数
setup_sectors = (setup_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
kernel_sectors = (system_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

// 将扇区计数写入 boot 扇区的固定位置
buf[0x1F0] = setup_sectors & 0xFF;       // setup 扇区数（低字节）
buf[0x1F1] = (setup_sectors >> 8) & 0xFF;// setup 扇区数（高字节）
buf[0x1F2] = kernel_sectors & 0xFF;      // kernel 扇区数（低字节）
buf[0x1F3] = (kernel_sectors >> 8) & 0xFF;// kernel 扇区数（高字节）
```

boot.s 在偏移 0x1F0 处读取这些值来确定 setup 和 system 的大小与位置。

**构建产物：** 最终输出的 `Image` 文件是一个可启动的软盘镜像，可直接用 `qemu-system-i386 -fda Image` 启动。

### 2.4 setup.s — 实模式到保护模式的桥梁

`boot/setup.s` 运行在 0x90200，此时仍在 16 位实模式下。它是从实模式切换到保护模式的桥梁。

**主要任务：**

1. 获取系统内存大小（BIOS int 0x15）
2. 获取光标位置
3. 获取硬盘参数
4. 重映射 8259A PIC（将 IRQ 从 0x08-0x0F 移到 0x20-0x27）
5. 启用 A20 地址线
6. 加载 GDT/IDT 描述符
7. 设置 CR0.PE 进入保护模式
8. 跳转到 head.s（0x0000:0x10000）

**内存检测：**

```as
    mov ax, #0xE801          // BIOS 功能：获取内存大小
    int 0x15
    // 返回：
    // AX = 1MB-16MB 之间的内存，单位 1KB
    // BX = 16MB-4GB 之间的内存，单位 64KB
```

然后将结果写入特定的内存位置（0x90000+偏移），供后续内核代码读取。

**8259A PIC 重映射：**

这是 setup.s 中最关键的代码之一。在 IBM PC 上，8259A PIC 默认将 IRQ0-IRQ7 映射到中断向量 0x08-0x0F，IRQ8-IRQ15 映射到 0x70-0x77。但这与 x86 保留的异常向量（0x00-0x1F）冲突。

```as
    mov al, #0x11            // ICW1: 边沿触发, 级联模式, 需要 ICW4
    out 0x20, al             // 主 PIC
    .word 0x00eb, 0x00eb     // jmp 延迟（等待 I/O）

    mov al, #0x20            // ICW2: 主 PIC 中断向量基址 = 0x20
    out 0x21, al

    mov al, #0x04            // ICW3: 主 PIC IRQ2 连接从 PIC
    out 0x21, al

    mov al, #0x01            // ICW4: 8086 模式
    out 0x21, al

    // 从 PIC 类似，向量基址 = 0x28
    mov al, #0x11
    out 0xA0, al
    ...
    mov al, #0x28            // ICW2: 从 PIC 中断向量基址 = 0x28
    out 0xA1, al
```

**结果映射表：**

```
IRQ0  (时钟)      → 中断向量 0x20
IRQ1  (键盘)      → 中断向量 0x21
IRQ2  (级联)      → 中断向量 0x22
...
IRQ8  (RTC)       → 中断向量 0x28
IRQ14 (硬盘)      → 中断向量 0x2E
```

**启用 A20 地址线：**

在 8086 时代，地址总线只有 20 位。当地址超过 0xFFFFF 时，"回绕"到 0x00000（这是 1MB 边界）。80286+ 引入了 A20 门来兼容这一行为。

```as
    in al, #0x92             // 系统控制端口 A
    or al, #0x02             // 设置 A20 位
    out 0x92, al             // 启用第 21 条地址线（A20）
```

**加载 GDT 并进入保护模式：**

setup.s 定义了一个临时的 GDT，只包含必要的段：

```as
gdt:
    .word 0,0,0,0            // 描述符 0: 空描述符（必须）
    
    .word 0x07FF             // 代码段：限长 8MB (0x7FF × 4KB 粒度)
    .word 0x0000             // 基址低 16 位 = 0
    .word 0x9A00             // P=1, DPL=0, S=1, Type=1010(代码/可执行/可读)
    .word 0x00C0             // G=1(4KB粒度), D=1(32位), 基址高 8 位 = 0
    
    .word 0x07FF             // 数据段：限长 8MB
    .word 0x0000             // 基址低 16 位
    .word 0x9200             // P=1, DPL=0, S=1, Type=0010(数据/可读写)
    .word 0x00C0             // G=1, D=1, 基址高 8 位
```

注意 setup.s 使用临时 GDT，因为此时还没有页表支持。进入 head.s 后会重新设置完整的 GDT。

**进入保护模式的关键指令：**

```as
    cli                      // 关中断（此时没有 IDT）
    lgdt gdt_48              // 加载 GDT 描述符
    mov ax, #0x0001          // 设置 CR0.PE = 1
    lmsw ax                  // 加载机器状态字（CR0 的低 16 位）
    jmpi 0,8                 // 段间跳转到代码段选择子 8
```

`jmpi 0,8` 至关重要。跳转后 CS = 8（GDT 索引 1，即代码段选择子），刷新了预取队列。此时 EIP = 0，但实际物理地址是 0x00000 而非 0x90200（因为代码段基址 = 0）。

实际上，setup.s 中的 `jmpi 0,8` 跳转到一个临时处理程序，执行另一个 `jmpi` 跳转到 head.s 的入口 0x10000。这是因为之前的实模式代码还在内存低地址，需要正确导航。

### 2.5 head.s — 32 位内核入口

`boot/head.s` 是进入 32 位保护模式后执行的第一段代码，入口标签为 `startup_32`（由链接脚本 `kernel.ld` 指定）。

与 setup.s 不同，head.s 是整个内核镜像的一部分，与内核 C 代码链接在一起。它的任务是：

1. 重新加载段寄存器为内核段选择子
2. 设置页目录和页表（恒等映射低 4MB）
3. 启用分页
4. 设置完整的中断描述符表（IDT）
5. 设置完整的全局描述符表（GDT）
6. 编写中断处理程序桩（stubs）
7. 跳转到 C 语言的 `main()` 函数

**段寄存器重新加载：**

```as
startup_32:
    movl $0x10, %eax         // 选择子 0x10 = 内核数据段（GDT[2]）
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    lss _stack_start, %esp   // 加载内核栈
```

`_stack_start` 是链接脚本中定义的符号，标志内核栈的起始位置。`LSS` 加载 SS:ESP。

**为什么 CS 不需要手动设置？**
CS 已经通过之前的 `jmpi`（远跳转）设置为正确的代码段选择子（0x08）。

**页目录和页表设置：**

这是 head.s 中代码量最大的部分。目标是为内核建立恒等映射（线性地址 = 物理地址）。

```
物理内存布局：
0x100000  ┌──────────────────┐  ← 页目录（1 页 = 4KB）
          │  pg_dir          │
0x101000  ├──────────────────┤  ← 页表 0（映射 0-4MB）
          │  pg0             │
0x102000  ├──────────────────┤  ← 页表 1（映射 4-8MB）
          │  pg1             │
0x103000  ├──────────────────┤  ← 页表 2（映射 8-12MB）
          │  pg2             │
0x104000  ├──────────────────┤  ← 页表 3（映射 12-16MB）
          │  pg3             │
0x105000  ├──────────────────┤
          │  (未使用)         │
0x108000  ├──────────────────┤  ← 内核代码开始
          │  kernel code     │
          └──────────────────┘
```

**页目录初始化：**

```as
setup_paging:
    movl $1024*5, %ecx       // 清空 5 页（目录 + 4 个页表）
    xorl %eax, %eax
    xorl %edi, %edi
    cld
    rep; stosl                // 全部填 0

    // 设置页目录条目
    movl $pg0 + 7, pg_dir     // PDE[0] → pg0 + P=1 + R/W=1 + U/S=0
    movl $pg1 + 7, pg_dir+4   // PDE[1] → pg1 + P=1 + R/W=1 + U/S=0
    movl $pg2 + 7, pg_dir+8   // PDE[2] → pg2 + P=1 + R/W=1 + U/S=0
    movl $pg3 + 7, pg_dir+12  // PDE[3] → pg3 + P=1 + R/W=1 + U/S=0
```

每个 PDE 的值为 `页表物理地址 | 0x007`：
- Bit 0 (P) = 1：页存在
- Bit 1 (R/W) = 1：可读写
- Bit 2 (U/S) = 0：仅超级用户可访问

**页表条目填充：**

```as
    movl $pg3 + 4092, %edi   // 从 pg3 最后一页开始
    movl $0xFFF007, %eax     // 最后一项映射到 0xFFF000
    std                        // 反向填充
1:  stosl                      // 每次减 4 字节（上一页）
    subl $0x1000, %eax        // 递减线性地址
    jge 1b
```

这段代码从高地址向低地址填充 PTE：
- pg3 映射 0xC00000-0xFFFFFF（3MB-4MB）
- pg2 映射 0x800000-0xBFFFFF（2MB-3MB）
- pg1 映射 0x400000-0x7FFFFF（1MB-2MB）
- pg0 映射 0x000000-0x3FFFFF（0-1MB）

每个 PTE 的值为 `物理页框地址 | 0x007`。

**关键设计：恒等映射**
在本内核中，线性地址 = 物理地址。这样设计的原因是：
1. 简化内存管理，无需维护虚拟地址到物理地址的转换表
2. 内核可以安全地使用物理地址访问硬件
3. 用户程序虽然使用不同的段，但通过分页共享相同的物理内存

**启用分页：**

```as
    xorl %eax, %eax
    movl %eax, %cr3           // CR3 = 页目录物理地址（pg_dir 在 0x100000）
    movl %cr0, %eax
    orl $0x80000000, %eax     // 设置 PG 位
    movl %eax, %cr0
    ret                        // 这一步会刷新指令流水线
```

`RET` 指令通过修改 EIP 强制刷新指令预取队列，使分页立即生效。

**设置 IDT：**

head.s 为 256 个中断向量设置统一的中断处理程序。由于我们还没有页错误按需加载机制，所有中断都使用简单的处理程序桩。

```as
setup_idt:
    lea ignore_int, %edx      // 默认处理程序地址
    movl $0x00080000, %eax    // 选择子 0x0008（内核代码段）
    movw %dx, %ax              // 处理程序偏移低 16 位
    movw $0x8E00, %dx          // P=1, DPL=0, 类型=中断门

    lea _idt, %edi
    mov $256, %ecx
rp_sidt:
    movl %eax, (%edi)
    movl %edx, 4(%edi)
    addl $8, %edi
    dec %ecx
    jne rp_sidt
```

中断门描述符结构（8 字节）：

```
字节 0-1:  处理程序偏移[15:0]
字节 2-3:  段选择子
字节 4:    保留（0）
字节 5:    P(1) DPL(2位) 0 D(1) 1 1 Type(110=中断门)
字节 6-7:  处理程序偏移[31:16]
```

**默认中断处理程序：**

```as
ignore_int:
    cld
    pushl %eax
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10, %eax           // 内核数据段
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    pushl $msg_ignore_int
    call printk                // 打印中断信息
    popl %eax
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret
```

`ignore_int` 首先切换到内核数据段（0x10），然后调用 C 语言的 `printk` 函数输出中断信息，最后用 `IRET` 恢复所有寄存器并返回。

**设置特殊中断处理程序：**

head.s 为几个关键中断设置专门的入口：

```as
    // 系统调用（int 0x80）→ _system_call
    lea _system_call, %edx
    movw $0xEF00, %dx          // DPL=3（用户态可调用）
    movl %eax, _idt + 0x80*8
    movl %edx, _idt + 0x80*8 + 4

    // 时钟中断（int 0x20）→ _timer_interrupt
    lea _timer_interrupt, %edx
    ...

    // 键盘中断（int 0x21）→ _keyboard_interrupt
    lea _keyboard_interrupt, %edx
    ...

    // 页错误（int 0x0E）→ _page_fault
    lea _page_fault, %edx
    ...
```

注意系统调用门（int 0x80）的 DPL 设为 3，允许 Ring 3 的用户态程序通过 `int 0x80` 触发系统调用。

**系统调用入口（_system_call）：**

```as
_system_call:
    cmpl $9, %eax              // 检查系统调用号是否有效（0-9）
    ja bad_sys_call
    pushl %ebx                  // 保存寄存器
    pushl %ecx
    pushl %edx
    pushl %esi
    pushl %edi
    pushl %ebp

    call *sys_call_table(,%eax,4)  // 通过函数指针表调用

    popl %ebp                   // 恢复寄存器
    popl %edi
    popl %esi
    popl %edx
    popl %ecx
    popl %ebx
    iret                        // 返回用户态
```

`sys_call_table` 是一个函数指针数组，包含 10 个系统调用处理函数（sys_setup, sys_exit, sys_fork, sys_read, sys_write, sys_open, sys_close, sys_getpid, sys_pause, sys_time）。

**最终跳转到 C main()：**

```as
after_page_tables:
    pushl $0                    // 参数：envp（空）
    pushl $0                    // 参数：argv（空）
    pushl $0                    // 参数：argc（空）
    pushl $main                 // 压入 main 地址
    jmp setup_idt               // 先设置 IDT
    // setup_idt 返回后...
    pushl $L6                   // 压入返回地址
    pushl $main
    ret                         // 间接跳转到 main()
L6:
    jmp L6                      // main 返回后自旋
```

这里使用 `RET` 而非 `CALL` 来跳转到 `main()`，因为 `RET` 会从栈顶弹出地址并跳转，这在汇编中常用于实现间接跳转。

### 2.6 完整引导流程总结

```
BIOS POST
  │
  ▼
读取 MBR (扇区 0) 到 0x7C00
  │
  ▼
验证 0xAA55 签名
  │
  ▼
跳转 0x0000:0x7C00  → boot.s
  │
  ├─ 将自身从 0x7C00 重定位到 0x90000
  ├─ 设置栈 SS:SP = 0x9000:0xFF00
  ├─ INT 0x13 读取 setup (扇区 1-4) 到 0x90200
  ├─ INT 0x13 读取 system (扇区 5+) 到 0x10000
  │
  ▼
跳转 0x9020:0x0000  → setup.s
  │
  ├─ INT 0x15 检测内存
  ├─ 重映射 PIC (IRQ0→0x20, IRQ8→0x28)
  ├─ 启用 A20 门
  ├─ LGDT 加载临时 GDT
  ├─ 设置 CR0.PE=1 (进入保护模式)
  │
  ▼
跳转 0x0008:0x10000 → head.s (32 位代码)
  │
  ├─ 设置 DS/ES/FS/GS = 0x10 (内核数据段)
  ├─ 设置栈 LSS _stack_start
  ├─ 设置页目录 (0x100000) 和 4 个页表
  ├─ 填充 1024×4 个 PTE (恒等映射 0-16MB)
  ├─ 设置 CR3 = 0x100000
  ├─ 设置 CR0.PG = 1 (启用分页)
  ├─ 设置 IDT (256 个中断门)
  │   ├─ 0x00-0x1F: ignore_int
  │   ├─ 0x0E: page_fault
  │   ├─ 0x20: timer_interrupt
  │   ├─ 0x21: keyboard_interrupt
  │   └─ 0x80: system_call (DPL=3)
  ├─ LIDT 加载 IDT
  ├─ 设置完整 GDT
  ├─ LGDT 加载 GDT
  │
  ▼
CALL main() (kernel/main.c)
  │
  ├─ mem_init()
  ├─ buffer_init()
  ├─ hd_init()
  ├─ tty_init()
  ├─ sched_init()
  ├─ STI (开中断)
  ├─ move_to_user_mode()
  │
  ▼
fork() → shell (用户态 init 进程)
```

---

## 第三章：保护模式核心机制

本章深入分析 head.s 中设置的核心保护机制：GDT、IDT、TSS、LDT 和分页。

### 3.1 GDT（全局描述符表）

GDT 是保护模式下内存分段的基础，定义了所有任务共享的段描述符。

**本项目 GDT 布局：**

```
GDT 索引  选择子    描述符类型         基址      限长      DPL
─────────────────────────────────────────────────────────────
0        0x0000  空描述符（必须）       —         —        —
1        0x0008  内核代码段            0x00000000 4GB      0
2        0x0010  内核数据段            0x00000000 4GB      0
3        0x0018  (未使用)              —         —        —
4        0x0020  用户代码段            0x00000000 4GB      3
5        0x0028  用户数据段            0x00000000 4GB      3
6-7      0x0030  (保留)                —         —        —
8        0x0040  TSS (任务 0)         &init_task —        0
9        0x0048  LDT (任务 0)         &init_ldt  —        0
10       0x0050  TSS (任务 1)         &task[1]   —        0
11       0x0058  LDT (任务 1)         &ldt[1]    —        0
...      ...     ...                  ...        ...      ...
(N-2)    —       TSS (任务 N)         —          —        0
(N-1)    —       LDT (任务 N)         —          —        0
```

每对 TSS/LDT 占用 2 个 GDT 条目。

**代码段描述符解析（以内核代码段为例）：**

```as
.align 8
.word 0xFFFF        // 限长[15:0] = 64KB × 4KB 粒度 = 4GB
.word 0x0000        // 基址[15:0] = 0
.word 0x9A00        // 基址[23:16] P(1) DPL(00) S(1) Type(1010)
.word 0x00CF        // 基址[31:24] G(1) D/B(1) 0 AVL(0) 限长[19:16]
```

Type = 1010 的含义：
- Bit 3 = 1（代码段）
- Bit 2 = 0（非一致代码段，特权级检查严格）
- Bit 1 = 1（可读）
- Bit 0 = 0（已访问位，初始为 0）

**平坦模型的设计理念：**
所有段的基址为 0、限长为 4GB，意味着任何线性地址都可以通过任何段访问。这通过分页机制来提供保护——即使段允许访问，页表也可以阻止对特定页的访问。

### 3.2 IDT（中断描述符表）

IDT 定义了 256 个中断/异常处理程序的入口。

**中断门描述符字段：**

```
偏移 0-1:  处理程序偏移[15:0]
偏移 2-3:  段选择子（必须是代码段）
偏移 4:    0 (保留)
偏移 5:    标志字节
           Bit 7 (P)    = 1 (存在)
           Bit 6-5 (DPL) = 特权级
           Bit 4 (0)    = 0 (系统段)
           Bit 3-0 (Type)= 0xE (32位中断门)
偏移 6-7:  处理程序偏移[31:16]
```

**中断门 vs 陷阱门：**
- **中断门**（Type=0xE）：进入时自动清除 IF（关中断）
- **陷阱门**（Type=0xF）：进入时不修改 IF
- 本项目全部使用中断门

**为什么系统调用门的 DPL=3？**
用户态程序在 Ring 3，而中断门的 DPL 限制谁能通过软件 `INT` 指令触发该中断。DPL=3 意味着 Ring 3 的代码可以调用 `int 0x80`。如果 DPL=0，用户态调用会产生 #GP 异常。

硬件中断（由 PIC 触发）不受 DPL 限制，只受 IDT 是否加载的影响。

### 3.3 TSS（任务状态段）

TSS 是 x86 硬件任务切换的核心数据结构。当发生特权级切换（如 Ring 3 → Ring 0）时，CPU 从当前任务的 TSS 中加载 SS0 和 ESP0。

**TSS 结构（104 字节）：**

```
偏移   大小   字段
0x00   2      back_link (前一个任务的 TSS 选择子)
0x02   2      保留
0x04   4      ESP0 (Ring 0 栈指针)
0x08   2      SS0  (Ring 0 栈段选择子)
0x0A   2      保留
0x0C   4      ESP1
0x10   2      SS1
0x12   2      保留
0x14   4      ESP2
0x18   2      SS2
0x1A   2      保留
0x1C   4      CR3 (页目录基址)
0x20   4      EIP
0x24   4      EFLAGS
0x28   4      EAX
0x2C   4      ECX
0x30   4      EDX
0x34   4      EBX
0x38   4      ESP
0x3C   4      EBP
0x40   4      ESI
0x44   4      EDI
0x48   2      ES
0x4A   2      保留
0x4C   2      CS
0x4E   2      保留
0x50   2      SS
0x52   2      保留
0x54   2      DS
0x56   2      保留
0x58   2      FS
0x5A   2      保留
0x5C   2      GS
0x5E   2      保留
0x60   2      LDTR (局部描述符表选择子)
0x62   2      保留
0x64   2      I/O 位图基址 + 保留
```

**TSS 在本项目中的用途：**
- 用于特权级切换（Ring 3 → Ring 0）
- 用于任务切换（通过 `switch_to` 宏）
- 每个进程拥有独立的 TSS

**设置 TSS 描述符的宏（set_tss_desc）：**

```c
#define set_tss_desc(n, addr) \
__asm__("movw $104, %1\n\t"     /* 限长 = 104 字节 */ \
        "movw %%ax, %2\n\t"     /* 基址低 16 位 */ \
        "rorl $16, %%eax\n\t"   \
        "movb %%al, %3\n\t"     /* 基址中 8 位 */ \
        "movb $0x89, %4\n\t"    /* P=1 DPL=0 Type=1001(32位TSS可用) */ \
        "movb $0x00, %5\n\t"    /* */ \
        "movb %%ah, %6\n\t"     /* 基址高 8 位 */ \
        "rorl $16, %%eax"       \
        ::"a"(addr), "m"(*(n)), "m"(*(n+2)), "m"(*(n+4)), \
          "m"(*(n+5)), "m"(*(n+6)), "m"(*(n+7)))
```

### 3.4 LDT（局部描述符表）

LDT 为每个进程提供私有的段描述符。在 Linux 0.01 中，每个进程有一个 LDT，包含两个描述符：

- LDT[0]: 用户代码段（基址 = 进程地址空间起始，限长 = 640KB）
- LDT[1]: 用户数据段（同上）

```c
init_task.ldt[0].a = 0x0000FFFF;  // 限长[15:0] + 基址[15:0]
init_task.ldt[0].b = 0x00CFFA00;  // P(1) DPL(3) S(1) Type(1010=代码) G(1) D(1)
init_task.ldt[1].a = 0x0000FFFF;
init_task.ldt[1].b = 0x00CFF200;  // P(1) DPL(3) S(1) Type(0010=数据/可写) G(1) D(1)
```

基址设为一个很高的值（如 0x400000），每个进程有自己的 LDT，实现进程间地址空间隔离。

### 3.5 页目录与页表

**页目录基址（pg_dir）：** 物理地址 0x100000

**页表布局：**

```
页目录条目    指向      映射范围          内容
────────────────────────────────────────────────
PDE[0]      →   pg0    0x000000-0x3FFFFF  恒等映射(内核+用户)
PDE[1]      →   pg1    0x400000-0x7FFFFF  恒等映射(用户空间)
PDE[2]      →   pg2    0x800000-0xBFFFFF  恒等映射(未用)
PDE[3]      →   pg3    0xC00000-0xFFFFFF  恒等映射(未用)
PDE[4-1023] →   0       —                  不存在
```

**页表项标志设计：**

```c
#define PAGE_PRESENT  (1 << 0)  // 0x001  页存在
#define PAGE_RW       (1 << 1)  // 0x002  可读写
#define PAGE_USER     (1 << 2)  // 0x004  用户可访问
#define PAGE_ACCESSED (1 << 5)  // 0x020  已访问
#define PAGE_DIRTY    (1 << 6)  // 0x040  已修改
```

组合标志：
- 内核页：`PAGE_PRESENT | PAGE_RW` = 0x003（超级用户读写）
- 用户页：`PAGE_PRESENT | PAGE_RW | PAGE_USER` = 0x007（用户可读写）

**初始化代码的巧妙之处：**

```as
    movl $pg3 + 4092, %edi   // 从最后一个 PTE 开始
    movl $0xFFF007, %eax     // 最后一页的 PTE 值
    std                        // 设置方向标志（反向）
1:  stosl
    subl $0x1000, %eax
    jge 1b
```

这段代码一次性填充 4 个页表中的所有 PTE：

1. 从 pg3 的最后一个条目（地址 0x100000 + 4×4096 - 4 = 0x103FFC）开始
2. 每个 PTE = 物理页框地址 | 0x007
3. 用 `STD` 设置反向填充，每次 `STOSL` 后 EDI 自动减 4
4. 循环直到 EAX 变为负数（处理完所有 4096 个 PTE）

这样的代码非常紧凑，用 7 条指令实现了 4096 个 PTE 的初始化。

---

## 第四章：中断处理系统

### 4.1 8259A PIC 初始化

8259A 可编程中断控制器管理 15 个硬件中断源（2 个 8259A 级联）。

**端口地址：**

```
主 PIC: 命令 0x20, 数据 0x21
从 PIC: 命令 0xA0, 数据 0xA1
```

**初始化命令字（ICW）序列：**

setup.s 中的初始化代码向 PIC 发送 4 个 ICW：

```
ICW1 = 0x11  边沿触发模式, 级联模式, 需要 ICW4
ICW2 = 0x20  主 PIC 向量基址 = 0x20（IRQ0→INT 0x20）
ICW3 = 0x04  主 PIC IRQ2 连接从 PIC
ICW4 = 0x01  8086/8088 模式（非 8085）
```

**中断映射结果：**

```
主 PIC (端口 0x20/0x21):
IRQ0  → INT 0x20  系统定时器
IRQ1  → INT 0x21  键盘
IRQ2  → INT 0x22  级联到从 PIC
IRQ3  → INT 0x23  串口 2
IRQ4  → INT 0x24  串口 1
IRQ5  → INT 0x25  并口 2 / 声卡
IRQ6  → INT 0x26  软盘控制器
IRQ7  → INT 0x27  并口 1

从 PIC (端口 0xA0/0xA1):
IRQ8  → INT 0x28  RTC 时钟
IRQ9  → INT 0x29  ACPI / 通用
IRQ10 → INT 0x2A  通用
IRQ11 → INT 0x2B  通用
IRQ12 → INT 0x2C  PS/2 鼠标
IRQ13 → INT 0x2D  FPU 协处理器
IRQ14 → INT 0x2E  主 IDE 控制器
IRQ15 → INT 0x2F  从 IDE 控制器
```

**中断结束（EOI）信号：**

处理完中断后，必须向 PIC 发送 EOI 信号：

```c
// 主 PIC 中断处理完：
outb(0x20, 0x20);

// 从 PIC 中断处理完（需同时通知两个 PIC）：
outb(0xA0, 0x20);
outb(0x20, 0x20);
```

### 4.2 时钟中断（IRQ0）

时钟中断由 PIT（8253/8254 可编程间隔定时器）产生。

**PIT 初始化：**

```c
__asm__ volatile(
    "movb $0x36, %%al\n\t"  // 通道 0, 读写高低字节, 模式 3(方波), 二进制
    "outb %%al, $0x43\n\t"  // 命令寄存器
    "movb $0x9b, %%al\n\t"  // 计数器低字节
    "outb %%al, $0x40\n\t"  // 通道 0 数据口
    "movb $0x2e, %%al\n\t"  // 计数器高字节
    "outb %%al, $0x40\n\t"  // 通道 0 数据口
    : : : "al"
);
```

**频率计算：**

```
PIT 输入频率 = 1.193182 MHz
计数值 = 0x2E9B = 11931
输出频率 = 1,193,182 / 11,931 ≈ 100 Hz
时间片 ≈ 10 ms
```

**时钟中断处理流程：**

```
IRQ0 触发
  → 向量 0x20
  → _timer_interrupt (head.s)
  → pusha / 保存段寄存器
  → call do_timer (kernel/sched.c)
  → movb $0x20, $0x20 (向 PIC 发送 EOI)
  → pop 段寄存器 / popa
  → iret
```

`do_timer` 函数实现：

```c
void do_timer(void)
{
    jiffies++;                    // 全局时钟滴答计数器

    if (current->counter > 0) {
        current->counter--;       // 递减当前进程的时间片
    }

    if (current->counter > 0) return;  // 时间片未用完，继续执行

    schedule();                   // 时间片用完，重新调度
}
```

`jiffies` 是内核的全局时钟滴答数，每次时钟中断加 1。由于中断频率为 100Hz，1 jiffy = 10ms。系统调用 `sys_time()` 返回 `jiffies / HZ` 作为秒数。

### 4.3 键盘中断（IRQ1）

**键盘中断处理程序：**

```as
_keyboard_interrupt:
    pusha
    ...
    inb $0x60, %al       // 读取键盘扫描码
    movb %al, scan_code  // 保存到全局变量
    call do_keyboard
    movb $0x20, $0x20    // 发送 EOI
    ...
    popa
    iret
```

**PS/2 键盘扫描码处理：**

键盘驱动程序处理两类键：
1. **字符键**：A-Z, 0-9 等 → 根据 Shift 状态转换为 ASCII
2. **控制键**：Shift, Ctrl 等 → 修改状态标志

扫描码通过**扫描码映射表**转换为 ASCII：

```c
// 基本 ASCII 映射表（未按下 Shift）
static char scancode_table[] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',  /* 0x00-0x09 */
    '9', '0', '-', '=', '\b',                           /* 0x0A-0x0E */
    '\t', 'q', 'w', 'e', 'r',                           /* 0x0F-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',       /* 0x14-0x1D */
    // ... 持续到 0x3F
    // 0x1E = 'a', 0x2C = 'z', 0x39 = ' '
};

// Shift 修饰后的映射表
static char shift_map[] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    // ...
};
```

**Shift 状态管理：**

```c
// 按下 Shift 键：扫描码 0x2A (左) 或 0x36 (右)
if (scan_code == 0x2A || scan_code == 0x36)
    shift_pressed = 1;

// 释放 Shift 键：扫描码 0xAA (左释放) 或 0xB6 (右释放)
if (scan_code == 0xAA || scan_code == 0xB6)
    shift_pressed = 0;

// 按键释放的扫描码 = 按下扫描码 + 0x80
if (scan_code & 0x80)
    return;  // 忽略释放事件
```

**字符送入 TTY 缓冲区：**

```c
void do_keyboard(void)
{
    char c;
    
    if (scan_code & 0x80) return;  // 忽略按键释放
    
    // Shift 状态处理
    if (scan_code == 0x2A || scan_code == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scan_code == 0xAA || scan_code == 0xB6) {
        shift_pressed = 0;
        return;
    }
    
    // 选择映射表
    c = shift_pressed ? shift_map[scan_code] : scancode_table[scan_code];
    
    if (c) {
        // 写入 TTY 读缓冲区
        put_queue(c);
    }
}
```

### 4.4 硬盘中断（IRQ14）

硬盘中断处理程序负责在 PIO 读操作完成后通知等待的进程。

```as
_hd_interrupt:
    pusha
    ...
    call do_hd
    movb $0x20, $0xA0    // 先向从 PIC 发送 EOI
    movb $0x20, $0x20    // 再向主 PIC 发送 EOI
    ...
    popa
    iret
```

**为什么需要两级 EOI？**
IRQ14 连接到从 PIC 的 IRQ6。当从 PIC 中断被响应时，主 PIC 的 IRQ2 也被触发。因此处理完后需要向两个 PIC 都发送 EOI。

`do_hd` 函数设置标志位并唤醒等待者：

```c
void do_hd(void)
{
    hd_status = inb(HD_STATUS);
    hd_done = 1;
    wake_up(&wait_for_hd);
}
```

### 4.5 系统调用入口（int 0x80）

系统调用是用户态程序请求内核服务的唯一方式。入口代码在 head.s 中定义：

```as
_system_call:
    cmpl $9, %eax              // 调用号范围检查
    ja bad_sys_call
    pushl %ebx                  // 保存所有调用者寄存器
    pushl %ecx
    pushl %edx
    pushl %esi
    pushl %edi
    pushl %ebp
    
    call *sys_call_table(,%eax,4)  // 调用处理函数
    
    popl %ebp                   // 恢复寄存器
    popl %edi
    popl %esi
    popl %edx
    popl %ecx
    popl %ebx
    iret                        // 返回用户态
```

**参数传递约定：**

```
EAX = 系统调用号 (0-9)
EBX = 第 1 个参数
ECX = 第 2 个参数
EDX = 第 3 个参数
```

对于 `sys_call_table(,%eax,4)`，这是一个 SIB（Scale-Index-Base）寻址方式：
`call *(table + eax * 4)`，其中 table 是函数指针数组。

**系统调用表：**

```c
typedef int (*fn_ptr)(void);

fn_ptr sys_call_table[] = {
    sys_setup,   // 0
    sys_exit,    // 1
    sys_fork,    // 2
    sys_read,    // 3
    sys_write,   // 4
    sys_open,    // 5
    sys_close,   // 6
    sys_getpid,  // 7
    sys_pause,   // 8
    sys_time,    // 9
};
```

### 4.6 页错误处理

页错误（#PF，向量 14）发生时，CPU 自动将产生错误的**线性地址**放入 CR2，并在栈上压入错误码。

**错误码含义：**

```
Bit 0 (P)   : 0=页不存在, 1=保护违规
Bit 1 (W/R) : 0=读, 1=写
Bit 2 (U/S) : 0=超级用户, 1=用户模式
Bit 3 (R)   : 1=保留位被置位
Bit 4 (I)   : 1=取指令
```

**页错误处理入口（page.s）：**

```as
_page_fault:
    xchgl %eax, (%esp)         // 保存 EAX，取出错误码
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10, %edx            // 内核数据段
    mov %dx, %ds
    mov %dx, %es
    mov %dx, %fs
    movl %cr2, %edx             // 错误地址
    pushl %edx                   // 参数 2: 地址
    pushl %eax                   // 参数 1: 错误码
    call do_no_page              // C 处理函数
    addl $8, %esp
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret
```

注意 `xchgl %eax, (%esp)` 的用法——它将 EAX 与栈顶的错误码交换，这样既保存了 EAX，又取出了错误码。

在当前实现中，`do_no_page` 只是调用 `panic("page fault")`——因为内核尚未实现按需页加载（demand paging）。这在教学中是一个明确的简化点，也是后续可以扩展的方向。

### 4.7 异常处理

其他 x86 异常（除零、GPF 等）统一由 `ignore_int` 处理：

```as
ignore_int:
    cld
    pushl %eax
    pushl %ecx
    pushl %edx
    push %ds
    push %es
    push %fs
    movl $0x10, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    pushl $msg_ignore_int
    call printk               // 打印 "Unknown interrupt" 消息
    popl %eax
    pop %fs
    pop %es
    pop %ds
    popl %edx
    popl %ecx
    popl %eax
    iret
```

**为什么需要先切换到内核数据段？**
异常可能在用户态（DS=0x20+3）触发，而 printk 需要访问内核数据（DS=0x10）。先保存原 DS，再切换到内核 DS 保证 printk 能正常工作。

---

## 第五章：内核初始化（main.c）

`kernel/main.c` 中的 `main()` 函数是 C 语言入口。

### 5.1 主函数的结构

```c
void main(void)
{
    unsigned long memory_start, memory_end;

    memory_end = (1 << 20);             // 假设至少 1MB
    memory_end += 0; /* 从 setup.s 读取实际值 */;

    memory_start = (unsigned long)&_end + 0x1000;  // 内核末尾 + 1页

    mem_init(memory_start, memory_end);   // 初始化内存管理
    buffer_init(memory_end);              // 初始化缓冲区缓存
    hd_init();                            // 初始化硬盘
    tty_init();                           // 初始化 TTY
    sched_init();                         // 初始化调度器

    sti();                                // 开中断
    move_to_user_mode();                  // 切换到用户态
    if (!fork()) {                        // 创建子进程
        init();                           // 子进程运行 shell
    }
    for (;;) pause();                     // 父进程等待
}
```

### 5.2 内存检测

`memory_end` 通过 setup.s 中 BIOS int 0x15 的调用结果计算。setup.s 将结果写入 0x90000 偏移处，main.c 中可读取：

```c
memory_end = (1 << 20);
memory_end += 0;  // 实际应该读取 setup.s 写入的内存大小
```

**`_end` 符号**是由链接脚本定义的符号，标记内核映像的结束位置。这是 C 代码和链接器之间的接口。

### 5.3 各子系统初始化

**mem_init(start, end)：**
- 计算物理页数 `max_map_nr = (end - 0x100000) / 4096`
- 将 `mem_map` 位图数组放置在物理内存顶部
- 标记页目录、页表、内核代码为已使用（USED = 100）
- 标记 mem_map 自身占用的页为已使用

**buffer_init(end)：**
- 缓冲区缓存占用内核映像末尾到 `end - 2MB` 之间的内存
- 初始化双向链表连接所有缓冲区头

**hd_init()：**
- 重置 IDE 控制器
- 通过 IDENTIFY 命令检测硬盘

**tty_init()：**
- 初始化 3 个 TTY 设备（控制台、串口1、串口2）

**sched_init()：**
- 初始化任务数组
- 设置 init_task（PID=0 的空闲任务）
- 在 GDT 中为 init_task 设置 TSS/LDT 描述符
- 加载任务寄存器（LTR）

### 5.4 进入用户态

`move_to_user_mode()` 是切换到 Ring 3 的魔法代码：

```c
#define move_to_user_mode() \
__asm__ ( \
    "movl %%esp, %%eax\n\t" \
    "pushl $0x2B\n\t"       /* SS = 用户数据段选择子 (0x28 | 3) */ \
    "pushl %%eax\n\t"       /* ESP = 当前内核栈 */ \
    "pushfl\n\t"             /* EFLAGS */ \
    "pushl $0x23\n\t"       /* CS = 用户代码段选择子 (0x20 | 3) */ \
    "pushl $1f\n\t"         /* EIP = 下一条指令地址 */ \
    "iret\n\t"               /* IRET 弹出 CS:EIP, EFLAGS, SS:ESP */ \
    "1:\tmovl $0x2B, %%eax\n\t" \
    "movw %%ax, %%ds\n\t"   /* 设置数据段为 Ring 3 */ \
    "movw %%ax, %%es\n\t" \
    "movw %%ax, %%fs\n\t" \
    "movw %%ax, %%gs" \
    ::: "ax")
```

**IRET 指令的特权级切换魔法：**

IRET 从栈上弹出：
1. EIP → 恢复指令指针
2. CS → 恢复代码段选择子
3. EFLAGS → 恢复标志寄存器
4. ESP → 恢复栈指针（仅在 CS.RPL ≠ 当前 CPL 时弹出）
5. SS → 恢复栈段（仅在 CS.RPL ≠ 当前 CPL 时弹出）

通过将 CS 设为 0x23（RPL=3），IRET "降落"在 Ring 3，同时将 SS 设为 0x2B（RPL=3）。DS/ES/FS/GS 不会自动切换，需要手动设置。

**注意**：在 `move_to_user_mode` 宏中，我们保持使用相同的栈地址（ESP 不变）。这意味着内核和用户使用同一个物理栈——这在实际操作系统中不安全，但在教学内核中可以简化。

---

## 第六章：进程管理

### 6.1 task_struct 数据结构

```c
struct task_struct {
    long state;               // 进程状态
    long counter;             // 时间片计数器
    long priority;            // 优先级（静态）
    long signal;              // 信号位图
    struct tss_struct tss;    // 硬件任务状态段
    struct file *filp[NR_OPEN];// 打开文件指针数组
    int uid;                  // 用户 ID
    int pid;                  // 进程 ID
    int pgrp;                 // 进程组 ID
    int session;              // 会话 ID
    int leader;               // 会话领导标志
    long cutime, cstime;      // 子进程用户/系统时间
    long start_time;          // 进程启动时间
    unsigned long start_code, end_code;  // 代码段范围
    unsigned long end_data;   // 数据段范围
    unsigned long brk;        // 堆顶
    unsigned long start_stack;// 栈底
    struct desc_struct ldt[3];// 局部描述符表
};
```

**进程状态定义：**

```c
#define TASK_RUNNING        0   // 可运行（或正在运行）
#define TASK_INTERRUPTIBLE  1   // 可中断睡眠
#define TASK_UNINTERRUPTIBLE 2  // 不可中断睡眠
#define TASK_ZOMBIE         3   // 僵尸进程
#define TASK_STOPPED        4   // 已停止
```

**全局进程表：**

```c
#define NR_TASKS 64
struct task_struct *task[NR_TASKS] = {NULL,};
struct task_struct *current = NULL;
```

`task[]` 是内核的全局进程表，`current` 指向当前正在执行的进程。

### 6.2 进程初始化

`sched_init()` 函数初始化第一个进程（PID=0 的 init_task）：

```c
void sched_init(void)
{
    // 初始化 LDT 描述符
    init_task.ldt[0].a = 0x0000FFFF;  // 代码段
    init_task.ldt[0].b = 0x00CFFA00;
    init_task.ldt[1].a = 0x0000FFFF;  // 数据段
    init_task.ldt[1].b = 0x00CFF200;

    // 在 GDT 中设置 TSS/LDT 描述符
    p = (struct desc_struct *)(&_gdt) + 8;
    set_tss_desc(p, &init_task.tss);
    p = (struct desc_struct *)(&_gdt) + 9;
    set_ldt_desc(p, &init_task.ldt);

    // 加载任务寄存器
    init_task.tss.ldt = 72;   // LDT 选择子 = 9 × 8 = 72
    ltr(64);                  // TR = TSS 选择子 = 8 × 8 = 64
}
```

**GDT 索引用法：**
- GDT[8] = TSS0，选择子 = 8 × 8 = 0x40
- GDT[9] = LDT0，选择子 = 9 × 8 = 0x48
- GDT[10] = TSS1，选择子 = 10 × 8 = 0x50
- GDT[11] = LDT1，选择子 = 11 × 8 = 0x58

每对 TSS/LDT 递增 2。

### 6.3 fork 系统调用实现

`sys_fork()` 是进程管理中最复杂的函数，它创建一个当前进程的完整副本。

**步骤 1：分配新页面**

```c
p = (struct task_struct *)get_free_page();
if (!p) return -1;
```

为新进程分配一个 4KB 的物理页面，将其作为新的 task_struct。

**步骤 2：在进程表中登记**

```c
for (i = 0; i < NR_TASKS; i++) {
    if (task[i]) continue;
    task[i] = p;
    nr = i;
    pid = i + 1;
    break;
}
if (pid == 0) {
    free_page((unsigned long)p);
    return -1;
}
```

遍历 `task[]` 寻找空槽位。进程 ID = 槽位索引 + 1（PID 从 1 开始，0 留给 init_task）。

**步骤 3：复制进程结构体**

```c
*p = *current;

for (i = 0; i < NR_OPEN; i++) {
    if (p->filp[i])
        p->filp[i]->f_count++;
}

p->pid = pid;
p->counter = p->priority;
p->state = TASK_RUNNING;
```

`*p = *current` 是结构体赋值，复制整个 task_struct。然后需要递增每个打开文件的引用计数。

**步骤 4：复制内核栈**

```c
parent_top = current->tss.esp0;     // 父进程内核栈顶
parent_sp  = syscall_esp + 12;      // 父进程栈当前指针
size = parent_top - parent_sp;

child_top = (long)p + PAGE_SIZE;
child_sp  = child_top - size;

memcpy((void *)child_sp, (void *)parent_sp, size);
```

子进程的 task_struct 所在页面同时也是其内核栈。内核栈从页面顶部向下增长：

```
页面布局 (4KB):
┌─────────────────────┐ ← p + 0x1000 (child_top 也是 p->tss.esp0)
│   内核栈 (向下增长)   │
│         ↓           │
├─────────────────────┤ ← child_sp (复制后的栈顶)
│   已复制栈数据        │
├─────────────────────┤
│   系统调用帧          │
├─────────────────────┤
│   task_struct       │
└─────────────────────┘ ← p
```

**步骤 5：设置子进程的 TSS**

```c
p->tss.back_link = 0;
p->tss.esp0 = (long)p + PAGE_SIZE;
p->tss.ss0 = KERNEL_DS;
p->tss.cr3 = read_cr3();
p->tss.eip = (long)ret_from_sys_call;
p->tss.eflags = 0x202;
p->tss.eax = 0;  // 子进程 fork() 返回 0
p->tss.esp = (long)child_frame;
p->tss.cs = KERNEL_CS;
// ... 其他段寄存器
```

**关键设计点：**
- `tss.eax = 0`：fork() 在子进程中返回 0（父进程返回 PID）
- `tss.eip = ret_from_sys_call`：子进程首次被调度时从系统调用返回处开始执行
- `tss.cr3 = read_cr3()`：子进程与父进程共享页表（因为没有 COW）

**步骤 6：设置 GDT 描述符**

```c
int tss_entry = 8 + nr * 2;
int ldt_entry = tss_entry + 1;

p_desc = (struct desc_struct *)(&_gdt) + tss_entry;
set_tss_desc(p_desc, &p->tss);

p_desc = (struct desc_struct *)(&_gdt) + ldt_entry;
set_ldt_desc(p_desc, &p->ldt);

p->tss.ldt = ldt_entry * 8;
```

### 6.4 exit 系统调用实现

```c
int sys_exit(int ret)
{
    for (i = 0; i < NR_TASKS; i++) {
        if (task[i] == current) {
            task[i] = NULL;
            break;
        }
    }

    current->state = TASK_UNINTERRUPTIBLE;
    free_page((unsigned long)current);
    schedule();          // 切换到其他进程
    cli();
    for (;;) __asm__ volatile("hlt");
}
```

exit 的工作流程：
1. 从进程表中移除自己
2. 释放 task_struct 页面
3. 调用 schedule() 切换到其他进程
4. 后续代码不应被执行（free_page 后内存已不属于该进程）

**重要**：这里没有实现 wait/waitpid，子进程退出后没有父进程回收其退出状态。这是一个教学上的简化。

### 6.5 进程状态管理

**进程状态转换图：**

```
        fork()
  NEW ────────→ RUNNING ←──────┐
                  │             │
                  │ schedule()  │ 时间片到期
                  ▼             │
              (等待调度)────────┘
                  
  RUNNING ──→ INTERRUPTIBLE ──→ RUNNING
    │          (sleep_on被唤醒时)
    │
    └──→ exit ──→ ZOMBIE
```

状态设置的关键点：
- `TASK_RUNNING` 的进程被调度器选中后才能运行
- `TASK_INTERRUPTIBLE` 可被信号唤醒
- `TASK_UNINTERRUPTIBLE` 只能被显式 wake_up 唤醒

---

## 第七章：调度器

### 7.1 调度器设计

Linux 0.01 使用 O(N) 轮转调度算法：

```c
void schedule(void)
{
    int next, c;
    struct task_struct **p;

    while (1) {
        c = -1;
        next = -1;

        // 第一遍：找到 counter 最大的就绪进程
        for (p = &task[NR_TASKS - 1]; p >= &task[0]; p--) {
            if (*p == NULL) continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {
                c = (*p)->counter;
                next = (int)(p - task);
            }
        }

        if (c > 0) break;       // 找到了可运行进程

        // 第二遍：所有进程 time slice 用完了，重新分配
        for (p = &task[NR_TASKS - 1]; p >= &task[0]; p--) {
            if (*p == NULL) continue;
            (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
        }
    }

    // 执行上下文切换
    if (next != current_idx) {
        current = task[next];
        switch_to(next);
    }
}
```

**算法分析：**

1. **第一轮遍历**：在 TASK_RUNNING 进程中找 counter 最大的
2. **第二轮遍历**：如果所有进程 counter 都为 0，重新计算 counter：
   - `counter = (counter >> 1) + priority`
   - 新创建的进程（counter=0）获得 `priority` 作为初始时间片
   - 长时间运行的进程获得额外的时间片奖励（counter>>1 非零）
3. **O(N) 复杂度**：遍历整个 task 数组，N=64

**counter 的巧妙设计：**
- 时间片奖励机制：长期等待的进程（如 I/O 密集）积累 counter，下次被调度时获得更多 CPU 时间
- 饥饿避免：`counter >> 1` 确保等待时间越长，累积的 counter 越多
- 初始时间片 = priority（15 ticks = 150ms）

### 7.2 时钟中断处理

```c
void do_timer(void)
{
    jiffies++;

    if (current->counter > 0) {
        current->counter--;
    }

    if (current->counter > 0) return;

    schedule();
}
```

流程图：

```
时钟中断 (100Hz)
    │
    ▼
do_timer()
    │
    ├─ jiffies++
    ├─ current->counter--
    │
    ├─ counter > 0? ─── yes ──→ 返回（继续当前进程）
    │
    └─ no
        │
        ▼
    schedule()
        │
        ├─ 选择新进程
        └─ switch_to()
```

### 7.3 上下文切换

`switch_to(n)` 宏使用 x86 硬件任务切换：

```c
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx, _current\n\t" \
    "je 1f\n\t" \
    "movw %%dx, %1\n\t" \
    "xchgl %%ecx, _current\n\t" \
    "ljmp %0\n\t" \
    "cmpl %%ecx, _last_task_used_math\n\t" \
    "jne 1f\n\t" \
    "clts\n" \
    "1:" \
    ::"m"(*&__tmp.a),"m"(*&__tmp.b), \
    "d"(_TSS(n)),"c"((long)task[n])); \
}
```

**ljmp 如何触发任务切换：**

`ljmp %0` 的操作数是 `__tmp` 结构体，其格式为：
- `__tmp.a`：TSS 描述符的选择子（如 GDT 索引 × 8）
- `__tmp.b`：忽略（远跳转到 TSS 时不需要偏移）

`ljmp TSS选择子` 触发硬件任务切换：
1. CPU 从当前 TSS 保存所有寄存器状态
2. CPU 从新 TSS 加载所有寄存器状态（EIP, ESP, CR3, LDT, ...）
3. 设置 TR 寄存器指向新 TSS
4. 设置 EFLAGS.NT = 1（嵌套任务标志）

**优化技巧：**
- 如果 `current == task[n]`，跳过切换（无需切换到自己）
- `clts` 在 FPU 切换时清除 TS 标志

### 7.4 sleep_on / wake_up 机制

`sleep_on` 和 `wake_up` 实现简单的进程等待队列：

```c
void sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p) return;
    if (current == &init_task) panic("init sleeping");

    tmp = *p;
    *p = current;
    current->state = TASK_UNINTERRUPTIBLE;
    schedule();
    if (tmp && (tmp->state == TASK_INTERRUPTIBLE ||
                tmp->state == TASK_UNINTERRUPTIBLE))
        tmp->state = TASK_RUNNING;
}

void wake_up(struct task_struct **p)
{
    if (p && *p) {
        (**p).state = TASK_RUNNING;
        *p = NULL;
    }
}
```

**隐式等待链表：**

`sleep_on` 维护一个通过内核栈链接的隐式链表：

```
假设 A 先调用 sleep_on(&wait)，然后 B 也调用 sleep_on(&wait)

调用前:  wait = NULL
A 调用:  tmp = NULL, wait = &A, A.state = SLEEPING → schedule()
B 调用:  tmp = &A, wait = &B, B.state = SLEEPING → schedule()

唤醒链:
    wait → B → (B 栈中保存的 tmp) → A
```

`wake_up(&wait)` 将 B 设为 RUNNING。当 B 恢复执行时，`sleep_on` 返回后检查 tmp（=A）是否在睡眠，如果是则唤醒 A。这样就形成了级联唤醒。

**使用场景：** 在缓冲区管理中用得最多：

```c
// 等待缓冲区解锁
while (bh->b_lock) {
    sleep_on(&bh->b_wait);
}

// 释放缓冲区时唤醒等待者
bh->b_lock = 0;
wake_up(&bh->b_wait);
```

---

## 第八章：内存管理

### 8.1 物理内存布局

```
地址范围              大小       用途
─────────────────────────────────────────────────
0x000000 - 0x000FFF  4KB       IVT + BDA (实模式)
0x001000 - 0x0FFFFF  ~1MB      实模式可用
0x100000 - 0x100FFF  4KB       页目录 (pg_dir)
0x101000 - 0x101FFF  4KB       页表 0 (映射 0-4MB)
0x102000 - 0x102FFF  4KB       页表 1 (映射 4-8MB)
0x103000 - 0x103FFF  4KB       页表 2 (映射 8-12MB)
0x104000 - 0x104FFF  4KB       页表 3 (映射 12-16MB)
0x105000 - 0x107FFF  12KB      (未使用)
0x108000 - _end      内核映像  内核代码+数据+BSS
_end - (end-2MB)     ~800KB+   缓冲区缓存
(end-2MB) - end      2MB       保留给用户进程
```

**关键常量：**

```c
#define LOW_MEM  0x100000       // 内核空间起始（1MB）
#define PAGE_SIZE 4096           // 页大小
#define USED      100            // 永久保留的页标记
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12)  // 物理地址 → mem_map 索引
```

### 8.2 页框分配器

页框分配器使用简单的位图（bitmap）管理物理内存：

```c
unsigned long *mem_map = NULL;   // 位图数组
int max_map_nr = 0;              // 可管理的最大页数
```

**初始化：**

```c
void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    max_map_nr = (end_mem - LOW_MEM) / PAGE_SIZE;
    map_size = max_map_nr * sizeof(unsigned long);
    mem_map = (unsigned long *)(end_mem - map_size);

    // 全部标记为 0 (空闲)
    for (i = 0; i < max_map_nr; i++)
        mem_map[i] = 0;

    // 保留关键页面
    mem_map[0] = USED;   // 页目录
    mem_map[1] = USED;   // 页表 0
    // ... 保留 mem_map 自身和内核代码
}
```

`mem_map` 放置在物理内存最顶端，向下增长到 `end_mem`：

```
┌─────────────────────┐ ← end_mem
│     mem_map 数组     │ ← end_mem - map_size
├─────────────────────┤
│         ...         │
│     可用物理页       │
│         ...         │
├─────────────────────┤ ← LOW_MEM (1MB)
│    低内存 (保留)     │
└─────────────────────┘ ← 0
```

**分配一页：**

```c
unsigned long get_free_page(void)
{
    for (i = 0; i < max_map_nr; i++) {
        if (mem_map[i] != 0) continue;
        mem_map[i] = 1;
        addr = LOW_MEM + i * PAGE_SIZE;
        memset((char *)addr, 0, PAGE_SIZE);  // 清零分配到的页
        return addr;
    }
    return 0;  // 内存耗尽
}
```

- 遍历 `mem_map` 找第一个值为 0 的条目
- 标记为 1（已分配一次）
- 计算物理地址 = 1MB + 索引 × 4KB
- 用 `memset` 清零页面（安全措施）
- 返回物理地址

**释放一页：**

```c
void free_page(unsigned long addr)
{
    if (addr < LOW_MEM) return;
    if (addr >= memory_end) return;
    if (addr & (PAGE_SIZE - 1)) return;  // 检查地址对齐

    i = MAP_NR(addr);
    if (i >= max_map_nr) return;
    if (mem_map[i] <= 0) return;   // 防止重复释放
    if (mem_map[i] >= USED) return; // 永久页面不可释放

    mem_map[i]--;
}
```

重要保护措施：
1. 地址边界检查
2. 4KB 对齐检查
3. 防止重复释放 (<0)
4. 防止释放永久页面 (>=USED)

### 8.3 页表管理

**释放页表：**

```c
int free_page_tables(unsigned long from, unsigned long size)
{
    if (from & 0x3FFFFF)
        panic("free_page_tables: from must be 4MB aligned");

    for (nr = 0; nr < size; nr++) {
        if (*pg_dir & 1) {
            pg_table = (unsigned long *)(0xFFFFF000 & *pg_dir);
            // 释放页表中的所有物理页
            for (j = 0; j < 1024; j++) {
                if (pg_table[j] & 1)
                    free_page(pg_table[j] & 0xFFFFF000);
            }
            // 释放页表自身
            free_page((unsigned long)pg_table & 0xFFFFF000);
            *pg_dir = 0;
        }
    }

    write_cr3(read_cr3());  // 刷新 TLB
}
```

**`0xFFFFF000 & addr` 技巧：** 将地址对齐到 4KB 边界（去除低 12 位）。

**TLB 刷新：** `write_cr3(read_cr3())` 重新加载 CR3，使 TLB 全部失效。这是刷新整个 TLB 的标准方法。

### 8.4 段内存访问

内核使用段寄存器 FS 来访问用户空间数据：

```c
#define get_fs_byte(addr) ({ \
    register char __res; \
    __asm__("movb %%fs:%1, %0" : "=r"(__res) : "m"(*(addr))); \
    __res; })

#define put_fs_byte(val, addr) \
    __asm__("movb %0, %%fs:%1" : : "r"(val), "m"(*(addr)))
```

`FS` 段寄存器被设置为用户数据段选择子（0x2B = 0x28 | 3），这样 `%%fs:addr` 就访问了用户空间的地址。

**为什么需要这样做？**
内核代码运行在 Ring 0，内核数据段基址为 0。但用户代码使用 LDT 中的段，基址可能不为 0。通过 FS 段，内核可以安全地将用户空间指针解释为用户地址空间中的地址。

---

## 第九章：文件系统

### 9.1 MINIX v1 文件系统布局

MINIX v1 文件系统的磁盘布局：

```
块号      内容              说明
───────────────────────────────────
0         引导块              可引导标志 + 引导代码
1         超级块              文件系统元数据
2         Inode 位图          已用/空闲 inode
2+i       Zone 位图           已用/空闲数据块
2+i+z     Inode 表            Inode 数组
2+i+z+m   数据块              文件数据区
```

**超级块结构：**

```c
struct super_block {
    unsigned short s_ninodes;       // Inode 总数
    unsigned short s_nzones;        // 数据块总数
    unsigned short s_imap_blocks;   // Inode 位图占用块数
    unsigned short s_zmap_blocks;   // Zone 位图占用块数
    unsigned short s_firstdatazone; // 第一个数据块号
    unsigned short s_log_zone_size; // log2(块大小/基本块大小)
    unsigned long s_max_size;       // 最大文件大小
    unsigned short s_magic;         // 魔数 (0x137F)
};
```

### 9.2 超级块管理

```c
struct super_block super_block[NR_SUPER];  // 全局超级块数组（最多 8 个）

int sys_setup(void)
{
    // 读取设备根文件系统的超级块
    bh = bread(dev, 1);  // 超级块在块 1
    sb = (struct minix_superblock *)bh->b_data;

    if (sb->s_magic != SUPER_MAGIC) {
        printk("MINIX: bad magic");
        brelse(bh);
        return -1;
    }

    // 复制到全局超级块
    super_block[0].s_dev = dev;
    super_block[0].s_ninodes = sb->s_ninodes;
    super_block[0].s_nzones = sb->s_nzones;
    // ... 复制其他字段
    brelse(bh);
    return 0;
}
```

### 9.3 缓冲区缓存

缓冲区缓存是文件系统性能的核心。它将磁盘块缓存在内存中，减少磁盘 I/O。

**缓冲区头结构：**

```c
struct buffer_head {
    char *b_data;                   // 指向数据区
    unsigned long b_blocknr;        // 块号
    unsigned short b_dev;           // 设备号
    unsigned char b_uptodate;       // 数据是否有效
    unsigned char b_dirt;           // 是否脏（需写回）
    unsigned char b_count;          // 引用计数
    unsigned char b_lock;           // 锁定标志
    struct task_struct *b_wait;     // 等待队列
    struct buffer_head *b_prev;     // 哈希链表前驱
    struct buffer_head *b_next;     // 哈希链表后继
    struct buffer_head *b_prev_free;// 空闲链表前驱
    struct buffer_head *b_next_free;// 空闲链表后继
};
```

**双链表组织：**

缓冲区通过两个链表组织：
1. **哈希链表**（b_prev/b_next）：按设备号和块号快速查找
2. **空闲链表**（b_prev_free/b_next_free）：LRU 排序，分配/释放时使用

```
哈希桶 (散列到不同桶):
  bucket[0] → BH_A ↔ BH_B ↔ ...
  bucket[1] → BH_C ↔ ...
  ...

空闲链表 (LRU 顺序):
  free_list → BH_oldest ↔ ... ↔ BH_newest
```

**getblk — 获取缓冲区：**

```c
struct buffer_head *getblk(int dev, int block)
{
    struct buffer_head *bh;

repeat:
    // 在哈希链中查找
    bh = find_buffer(dev, block);
    if (bh) {
        if (bh->b_lock) {
            sleep_on(&bh->b_wait);
            goto repeat;
        }
        bh->b_count++;
        return bh;
    }

    // 从空闲链表找可用缓冲区
    bh = get_free_buffer();
    if (!bh) {
        sleep_on(&buffer_wait);
        goto repeat;
    }

    // 设置新缓冲区的设备/块号
    bh->b_dev = dev;
    bh->b_blocknr = block;
    bh->b_count = 1;
    return bh;
}
```

**getblk 流程图：**

```
getblk(dev, block)
    │
    ▼
在哈希表中查找 dev+block
    │
    ├─ 找到？
    │   ├─ b_locked? → sleep_on → 重试
    │   └─ b_count++, return
    │
    └─ 未找到
        │
        ▼
    从 LRU 空闲链表找空闲缓冲区
        │
        ├─ 找到？
        │   ├─ b_dirt? → ll_rw_block(WRITE) 写回
        │   └─ 设置 dev/block, b_count=1, return
        │
        └─ 未找到 → sleep_on(buffer_wait) → 重试
```

**bread — 读取块：**

```c
struct buffer_head *bread(int dev, int block)
{
    struct buffer_head *bh = getblk(dev, block);
    if (bh->b_uptodate) return bh;
    ll_rw_block(READ, bh);
    wait_on_buffer(bh);
    return bh;
}
```

`bread` 简化了"获取并确保数据有效"的操作。如果缓冲区数据已是最新的（b_uptodate），直接返回；否则发起读操作并等待完成。

### 9.4 Inode 缓存

Inode 缓存管理内存中的文件元数据。

```c
struct m_inode inode_table[NR_INODE];  // 64 个 inode 缓存槽
```

**iget — 获取 Inode：**

```c
struct m_inode *iget(int dev, int nr)
{
    // 先在缓存中查找
    for (i = 0; i < NR_INODE; i++) {
        inode = &inode_table[i];
        if (inode->i_dev == dev && inode->i_num == nr) {
            inode->i_count++;
            return inode;
        }
    }

    // 找空闲槽位
    for (i = 0; i < NR_INODE; i++) {
        inode = &inode_table[i];
        if (!inode->i_count) {
            inode->i_count = 1;
            inode->i_dev = dev;
            inode->i_num = nr;
            read_inode(inode);
            return inode;
        }
    }
    return NULL;
}
```

**read_inode — 从磁盘读取 Inode：**

```c
static void read_inode(struct m_inode *inode)
{
    // 计算 inode 所在磁盘块
    block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
            (inode->i_num - 1) / (BLOCK_SIZE / sizeof(struct d_inode));

    bh = bread(inode->i_dev, block);
    // 计算 inode 在块内的偏移
    offset = (inode->i_num - 1) % (BLOCK_SIZE / sizeof(struct d_inode));
    di = (struct d_inode *)bh->b_data + offset;

    // 复制到内存 inode
    inode->i_mode = di->i_mode;
    inode->i_uid = di->i_uid;
    inode->i_size = di->i_size;
    // ... 复制其他字段
    for (i = 0; i < 9; i++)
        inode->i_zone[i] = di->i_zone[i];
    brelse(bh);
}
```

Inode 区域块号计算：
- 跳过引导块(1) + 超级块(1) + inode 位图(i) + zone 位图(z)
- 然后根据 inode 号在 inode 表中定位

**iput — 释放 Inode：**

```c
void iput(struct m_inode *inode)
{
    if (!inode) return;
    inode->i_count--;
    if (inode->i_count) return;     // 还有其他引用
    if (inode->i_nlinks) return;    // 文件还有目录引用

    free_inode(inode);               // 彻底释放
}
```

### 9.5 文件读写

MINIX v1 的 inode 包含 9 个 zone 指针：

```
i_zone[0..6] : 直接块（每个指向一个 1KB 数据块）
i_zone[7]    : 一级间接块（指向一个包含 256 个块指针的块）
i_zone[8]    : 二级间接块（指向一个包含 256 个一级间接块指针的块）
```

**最大文件大小计算：**

```
直接块:   7 × 1KB       = 7KB
一级间接: 256 × 1KB     = 256KB
二级间接: 256 × 256 × 1KB = 64MB
─────────────────────────────────
总计:      ≈ 64.26MB
```

**file_read — 读取文件：**

```c
int file_read(struct m_inode *inode, struct file *filp,
              char *buf, int count)
{
    int left, chars, nr;
    struct buffer_head *bh;
    unsigned short block;
    int offset = filp->f_pos;

    left = count;
    while (left > 0) {
        // 计算当前读位置的块号和块内偏移
        nr = offset / BLOCK_SIZE;
        int boff = offset % BLOCK_SIZE;

        // 获取目标块号
        block = bmap(inode, nr, 0);

        if (!block) break;

        bh = bread(inode->i_dev, block);

        // 计算本次读取字节数
        chars = BLOCK_SIZE - boff;
        if (chars > left) chars = left;
        if (chars > inode->i_size - offset)
            chars = inode->i_size - offset;

        // 从缓冲区复制到用户空间
        for (i = 0; i < chars; i++)
            put_fs_byte(bh->b_data[boff + i], buf++);

        brelse(bh);
        left -= chars;
        offset += chars;
    }

    filp->f_pos = offset;
    return count - left;
}
```

**bmap — 块号映射：**

```c
static unsigned short bmap(struct m_inode *inode, int block, int create)
{
    // 直接块
    if (block < 7) {
        if (create && !inode->i_zone[block])
            inode->i_zone[block] = new_block(inode->i_dev);
        return inode->i_zone[block];
    }

    block -= 7;
    // 一级间接块
    if (block < 256) {
        if (create && !inode->i_zone[7])
            inode->i_zone[7] = new_block(inode->i_dev);
        bh = bread(inode->i_dev, inode->i_zone[7]);
        blk = ((unsigned short *)bh->b_data)[block];
        brelse(bh);
        return blk;
    }

    block -= 256;
    // 二级间接块
    if (create && !inode->i_zone[8])
        inode->i_zone[8] = new_block(inode->i_dev);
    bh = bread(inode->i_dev, inode->i_zone[8]);
    indblk = ((unsigned short *)bh->b_data)[block / 256];
    brelse(bh);
    bh = bread(inode->i_dev, indblk);
    blk = ((unsigned short *)bh->b_data)[block % 256];
    brelse(bh);
    return blk;
}
```

`bmap` 函数递归地解析间接块链，最终返回目标块的块号。

### 9.6 路径解析（namei）

`namei` 将路径名（如 "/usr/bin/sh"）解析为对应的 inode。

**MINIX 目录项结构：**

```c
struct minix_dir_entry {
    unsigned short inode;   // 文件 inode 号
    char name[14];          // 文件名（最大 14 字符）
};                          // 总共 16 字节
```

每个目录就是一个包含目录项数组的文件。

**namei 函数：**

```c
struct m_inode *namei(const char *pathname)
{
    // 起始 inode = 根目录 (inode 1)
    inode = iget(dev, 1);
    
    // 逐级解析路径
    while (*p) {
        // 提取下一个路径分量
        namelen = 0;
        while (*p && *p != '/' && namelen < 15)
            name[namelen++] = *p++;
        name[namelen] = '\0';

        // 在当前目录中查找
        if (find_entry(inode, name, namelen, &ino) < 0)
            return NULL;

        iput(inode);
        inode = iget(dev, ino);
    }

    return inode;
}
```

**find_entry — 在目录中查找：**

```c
static int find_entry(struct m_inode *dir, const char *name,
                       int namelen, unsigned short *res_inode)
{
    entries = dir->i_size / sizeof(struct minix_dir_entry);

    for (i = 0; i < entries; i++) {
        next_entry(dir, i, &bh, &de);
        if (name_eq(de->name, name, namelen)) {
            ino = de->inode;
            brelse(bh);
            *res_inode = ino;
            return 0;
        }
        brelse(bh);
    }
    return -1;
}
```

逐个遍历目录项，比较文件名。MINIX v1 目录项固定 16 字节，不支持变长文件名。

**name_eq — 文件名比较：**

```c
static int name_eq(const char *de_name, const char *name, int namelen)
{
    for (j = 0; j < namelen && j < 14; j++) {
        if (de_name[j] != name[j]) return 0;
    }
    if (j == namelen) {
        if (j == 14 || de_name[j] == '\0') return 1;
        return 0;
    }
    return 1;
}
```

MINIX 文件名是固定 14 字节的字符数组（不足用 '\0' 填充），所以比较逻辑需要特殊处理。

### 9.7 Bitmap 分配器

Bitmap 分配器管理 inode 和数据块的分配/释放。

**分配新块：**

```c
int new_block(int dev)
{
    sb = get_super(dev);

    for (i = 0; i < sb->s_zmap_blocks; i++) {
        bh = bread(dev, 2 + sb->s_imap_blocks + i);

        for (j = 0; j < BLOCK_SIZE * 8; j++) {
            // 找到第一个空闲位
            if (!(((char *)bh->b_data)[j / 8] & (1 << (j % 8)))) {
                ((char *)bh->b_data)[j / 8] |= (1 << (j % 8));
                bh->b_dirt = 1;
                brelse(bh);
                return sb->s_firstdatazone + i * (BLOCK_SIZE * 8) + j;
            }
        }
        brelse(bh);
    }
    return 0;  // 磁盘已满
}
```

**位图位置计算：**
- Zone 位图从块 2+imap_blocks 开始
- 共 zmap_blocks 个块
- 每个块管理 BLOCK_SIZE × 8 = 8192 个数据块
- 返回的块号 = firstdatazone + 位图中的偏移

**释放块：**

```c
void free_block(int dev, int block)
{
    // 检查块号是否合法
    if (block < sb->s_firstdatazone || block >= sb->s_nzones)
        return;

    // 计算在 zone 位图中的位置
    bit = block - sb->s_firstdatazone + 1;
    bh = bread(dev, 2 + sb->s_imap_blocks + (bit / (BLOCK_SIZE * 8)));

    // 清除对应位
    bit &= (BLOCK_SIZE * 8) - 1;
    if (((char *)bh->b_data)[bit / 8] & (1 << (bit % 8))) {
        ((char *)bh->b_data)[bit / 8] &= ~(1 << (bit % 8));
        bh->b_dirt = 1;
    }
    brelse(bh);
}
```

**为什么 `bit = block - firstdatazone + 1`？**
位图的第 0 位保留（表示 inode 0，不存在），所以实际数据块从位图的第 1 位开始。

---

## 第十章：设备驱动

### 10.1 控制台驱动（VGA 文本模式）

VGA 文本模式是最简单的显示设备，显存映射在物理地址 0xB8000。

**显示内存布局：**

```
0xB8000 ┌──────────────────┐
        │ 字符(0)  属性(0)  │
0xB8002 │ 字符(1)  属性(1)  │
        │       ...        │
0xB8F9E │ 字符(1999)属性    │
0xB8FA0 └──────────────────┘  (80×25×2 = 4000 字节)
```

每个字符占 2 字节：低字节是 ASCII，高字节是属性。

**属性字节格式：**

```
Bit 7    Bit 6-4      Bit 3    Bit 2-0
闪烁     背景色(3位)   高亮     前景色(3位)
```

常用颜色组合：
- 0x07 = 黑底白字（默认）
- 0x0F = 黑底亮白字
- 0x4F = 红底亮白字（内核 panic）
- 0x1F = 蓝底亮白字
- 0x02 = 黑底绿字

**con_write — 字符输出：**

```c
void con_write(struct tty_struct *tty)
{
    while (tty->write_cnt > 0) {
        c = tty->write_buf[tty->write_tail];
        tty->write_tail++;
        tty->write_cnt--;

        switch (c) {
        case '\n':
            cursor_y++;
            cursor_x = 0;
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7;  // 对齐到 8 列
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                video_mem[cursor_y * 80 + cursor_x] = 0x0720;  // 空格擦除
            }
            break;
        default:
            if (c >= 32) {
                video_mem[cursor_y * 80 + cursor_x] = 0x0700 | c;
                cursor_x++;
            }
        }

        // 处理换行和滚动
        if (cursor_x >= 80) { cursor_x = 0; cursor_y++; }
        if (cursor_y >= 25) scroll();
    }
    set_cursor(cursor_x, cursor_y);
}
```

**scroll — 屏幕滚动：**

```c
static void scroll(void)
{
    // 将第 2-25 行上移到第 1-24 行
    for (i = 0; i < 80 * 24; i++)
        video_mem[i] = video_mem[i + 80];

    // 清空最后一行
    for (i = 80 * 24; i < 80 * 25; i++)
        video_mem[i] = 0x0720;  // 黑底白字空格

    cursor_y = 24;
    cursor_x = 0;
}
```

**set_cursor — 硬件光标控制：**

```c
static void set_cursor(int x, int y)
{
    unsigned short pos = y * 80 + x;
    outb(14, 0x3D4);        // CRT 光标高字节寄存器
    outb(pos >> 8, 0x3D5);
    outb(15, 0x3D4);        // CRT 光标低字节寄存器
    outb(pos & 0xFF, 0x3D5);
}
```

VGA CRT 控制器通过寄存器索引（0x3D4）和数据（0x3D5）端口访问。寄存器 14/15 控制光标位置。

### 10.2 键盘驱动

键盘通过 PS/2 控制器连接到系统，数据在 I/O 端口 0x60。

**中断驱动模型：**

```
按键按下 → IRQ1 → keyboard_interrupt → do_keyboard → put_queue(c)
                                                        │
                                                        ▼
                                                  TTY 读缓冲区
                                                        │
                                                        ▼
                                            用户调用 read(0, ...) 读取
```

**put_queue — 向 TTY 缓冲区写字符：**

```c
void do_keyboard(void)
{
    if (scan_code & 0x80) return;  // 忽略释放事件

    if (scan_code == 0x2A || scan_code == 0x36) {
        shift_pressed = 1;          // Shift 按下
        return;
    }
    if (scan_code == 0xAA || scan_code == 0xB6) {
        shift_pressed = 0;          // Shift 释放
        return;
    }

    c = shift_pressed ? shift_map[scan_code] : scancode_table[scan_code];
    if (c) put_queue(c);
}
```

**扫描码映射原理：**
PS/2 Set 1 扫描码中，字母键的扫描码对应 ASCII 字母在键盘上的位置，而非 ASCII 值。例如 'A' 的扫描码是 0x1E，'Z' 是 0x2C。映射表提供了扫描码 → ASCII 的转换。

### 10.3 硬盘驱动（IDE PIO）

IDE 硬盘通过 I/O 端口 0x1F0-0x1F7 访问，使用 PIO（Programmed I/O）模式。

**IDE 端口定义：**

```c
#define HD_DATA      0x1F0   // 数据寄存器（16位）
#define HD_ERROR     0x1F1   // 错误寄存器（读）
#define HD_PRECOMP   0x1F1   // 预补偿（写）
#define HD_NSECTOR   0x1F2   // 扇区计数
#define HD_SECTOR    0x1F3   // LBA[7:0] 或扇区号
#define HD_LCYL      0x1F4   // LBA[15:8] 或柱面低字节
#define HD_HCYL      0x1F5   // LBA[23:16] 或柱面高字节
#define HD_CURRENT   0x1F6   // LBA[27:24] + 驱动器/磁头
#define HD_STATUS    0x1F7   // 状态寄存器（读）
#define HD_COMMAND   0x1F7   // 命令寄存器（写）
```

**读取扇区：**

```c
void hd_read_sectors(unsigned long start, unsigned short count, void *buf)
{
    // 设置 LBA 地址
    outb((start >> 24) | 0xE0, HD_CURRENT);  // LBA 模式, 主驱动器
    outb(count, HD_NSECTOR);
    outb(start & 0xFF, HD_SECTOR);
    outb((start >> 8) & 0xFF, HD_LCYL);
    outb((start >> 16) & 0xFF, HD_HCYL);

    // 发送 READ SECTORS 命令
    outb(CMD_READ, HD_COMMAND);

    // 轮询等待数据就绪
    while (count--) {
        while (!(inb(HD_STATUS) & DRQ_STAT));  // 等 DRQ
        // 读取 512 字节（256 个字）
        insw(HD_DATA, buf, 256);
        buf += 512;
    }
}
```

**关键状态位：**

```c
#define DRQ_STAT  0x08  // Data Request — 数据就绪
#define DRDY_STAT 0x40  // Drive Ready   — 驱动器就绪
#define BSY_STAT  0x80  // 驱动器忙
#define ERR_STAT  0x01  // 错误
```

### 10.4 TTY 子系统

TTY（Teletype）子系统在硬件驱动（键盘/控制台）和系统调用之间提供一个统一的接口层。

**TTY 结构：**

```c
struct tty_struct {
    char write_buf[TTY_BUF_SIZE];   // 写缓冲区
    int write_head, write_tail;     // 写缓冲区的头尾指针
    int write_cnt;                  // 写缓冲区中的字符数

    char read_buf[TTY_BUF_SIZE];    // 读缓冲区
    int read_head, read_tail;       // 读缓冲区的头尾指针
    int read_cnt;                   // 读缓冲区中的字符数

    struct task_struct *read_waiter; // 等待读取的进程

    void (*write)(struct tty_struct *); // 底层写函数指针
};
```

**环形缓冲区设计：**

```
写操作：
write_head → 下一个将被写入的位置
write_tail → 下一个将被读取的位置
write_cnt  → 缓冲区中的字符数

读操作：
read_head → 下一个将被写入的位置  
read_tail → 下一个将被读取的位置
read_cnt  → 缓冲区中的字符数
```

**空条件**：cnt == 0
**满条件**：cnt == TTY_BUF_SIZE

**tty_write — 统一写接口：**

```c
void tty_write(struct tty_struct *tty, const char *buf, int nr)
{
    for (i = 0; i < nr; i++) {
        // 缓冲区满时等待
        while (tty->write_cnt >= TTY_BUF_SIZE) {
            // 直接调用底层写函数
            tty->write(tty);
        }

        tty->write_buf[tty->write_head] = buf[i];
        tty->write_head = (tty->write_head + 1) % TTY_BUF_SIZE;
        tty->write_cnt++;
    }

    // 触发底层输出
    tty->write(tty);
}
```

`write` 函数指针指向 `con_write`（控制台）或其他设备的具体写函数。

---

## 第十一章：系统调用

### 11.1 系统调用机制

**调用流程（以 write(fd, buf, count) 为例）：**

```
用户态程序:
    write(1, "hello", 5)
        │
        ▼ (lib/close.c 中的包装函数)
    __asm__("int $0x80" : : "a"(__NR_write), "b"(fd), "c"(buf), "d"(count))
        │
        ▼ (CPU 陷阱到 Ring 0)
    _system_call (head.s)
        │
        ├─ 保存寄存器
        ├─ 检查系统调用号
        │
        ▼
    call *sys_call_table[4]
        │
        ▼
    sys_write(fd, "hello", 5)  (kernel/sys.c)
        │
        ├─ fd=1 (stdout) → tty_write(...)
        │
        ▼
    返回值在 EAX 中
        │
        ▼
    IRET → 用户态
```

**系统调用号：**

```c
#define __NR_setup    0
#define __NR_exit     1
#define __NR_fork     2
#define __NR_read     3
#define __NR_write    4
#define __NR_open     5
#define __NR_close    6
#define __NR_getpid   7
#define __NR_pause    8
#define __NR_time     9
```

### 11.2 各系统调用详解

**sys_open — 打开文件：**

```c
int sys_open(const char *filename, int flag)
{
    // 在进程 filp 表中找空闲槽
    for (fd = 0; fd < NR_OPEN; fd++)
        if (!current->filp[fd]) break;

    // 在全局 file_table 中找空闲槽
    for (i = 0; i < NR_FILE; i++)
        if (!file_table[i].f_count) break;

    // 解析路径名到 inode
    inode = namei(filename);
    if (!inode) return -1;

    // 初始化文件结构
    f = &file_table[i];
    f->f_mode = flag;
    f->f_count = 1;
    f->f_inode = inode;
    f->f_pos = 0;

    current->filp[fd] = f;
    return fd;
}
```

**为什么有两级索引？**
- `current->filp[fd]`：进程私有（最多 64 个打开文件）
- `file_table[i]`：内核全局（最多 64 个打开文件实例）
- 多个进程可以打开同一文件，共享同一个 file_table 条目（f_count > 1）

**sys_read — 读取文件：**

```c
long sys_read(unsigned int fd, char *buf, unsigned long count)
{
    if (fd == 0) {
        // fd 0 (stdin) — 从键盘 TTY 读取
        for (i = 0; i < count; i++) {
            // 等待数据
            while (tty_table[0].read_cnt == 0) {
                if (i) break;
                current->state = TASK_INTERRUPTIBLE;
                tty_table[0].read_waiter = current;
                schedule();
                tty_table[0].read_waiter = NULL;
            }
            // 读取一个字符
            c = tty_table[0].read_buf[tty_table[0].read_tail];
            tty_table[0].read_tail = (tty_table[0].read_tail + 1) % TTY_BUF_SIZE;
            tty_table[0].read_cnt--;
            put_fs_byte(c, buf + i);
            if (c == '\n') { i++; break; }
        }
        return i;
    }

    // 普通文件读取
    f = current->filp[fd];
    result = file_read(f->f_inode, f, buf, count);
    return result;
}
```

**sys_read 的阻塞设计：**
当 fd=0 (stdin) 且 TTY 缓冲区为空时：
1. 设进程为 TASK_INTERRUPTIBLE（可被中断唤醒）
2. 记录该进程为 TTY 的等待者
3. 调用 schedule() 让出 CPU
4. 键盘中断到来时，put_queue 发现等待者并唤醒它
5. schedule() 返回后继续读取

**sys_write — 写入文件：**

```c
long sys_write(unsigned int fd, const char *buf, unsigned long count)
{
    if (fd == 1 || fd == 2) {
        // stdout/stderr — 写入控制台 TTY
        for (i = 0; i < count; i++) {
            c = get_fs_byte(buf + i);
            if (c == '\n') tty_write(&tty_table[0], "\r", 1);  // 回车符
            tty_write(&tty_table[0], &c, 1);
        }
        return count;
    }

    // 普通文件写入（如果文件系统支持写操作）
    f = current->filp[fd];
    result = file_write(f->f_inode, f, buf, count);
    return result;
}
```

**为什么需要 \r**
在类 Unix 终端中，`\n`（换行）只下移光标，`\r`（回车）将光标移到行首。控制台驱动只处理 `\n`（下一行），所以写 `\n` 时需要额外输出 `\r`。

**sys_time — 获取时间：**

```c
int sys_time(unsigned long *tloc)
{
    return jiffies / HZ;  // 返回系统启动以来的秒数
}
```

`HZ = 100`，所以返回值是启动秒数（不是真实时间）。

---

## 第十二章：用户态 Shell

### 12.1 Shell 初始化

Shell 作为 init 进程运行，是系统的第一个用户态进程：

```c
// main.c 中的初始化代码
if (!fork()) {
    init();  // 子进程（PID=1）运行 shell
}

// init/shell.c
void init(void)
{
    setup((void *)0);
    
    // 打开 stdin, stdout, stderr
    if (!fork()) {
        // 子进程
        open("/dev/tty0", O_RDWR);
        // 此时 fd=0 (因为是最小的可用 fd)
        dup(0);  // fd=1 (stdout)
        dup(0);  // fd=2 (stderr)
        
        // 打印欢迎信息
        write(1, "\n", 1);
        write(1, "Welcome to Linux 0.01\n", 22);
        
        // 进入命令循环
        shell_loop();
        exit(0);
    }
    // 父进程等待子进程退出
    while (1) pause();
}
```

### 12.2 命令实现

Shell 支持的命令：

```c
// 命令分派表
struct {
    char *name;
    void (*func)(void);
} cmd_table[] = {
    {"echo", cmd_echo},
    {"help", cmd_help},
    {"ps",   cmd_ps},
    {"clear",cmd_clear},
    {"exit", cmd_exit},
    {NULL, NULL}
};
```

**命令解析流程：**

```
1. 打印提示符 "$ "
2. 读取一行输入
3. 去掉尾部换行符
4. 在 cmd_table 中匹配命令
5. 如果找到 → 调用对应函数
6. 如果没找到 → 打印 "unknown command"
```

**echo 命令：**

```c
void cmd_echo(void)
{
    char *p = cmd_line + 5;  // 跳过 "echo "
    while (*p == ' ') p++;   // 跳过空格
    write(1, p, strlen(p));
    write(1, "\n", 1);
}
```

**ps 命令：**

通过读取 /proc 目录（如果存在）来显示进程信息。这是一个可以扩展的方向。

---

## 第十三章：构建系统与运行

### 13.1 Makefile 构建流程

**编译器选择策略：**

```makefile
# 自动检测平台和可用编译器
ifeq ($(shell uname), Linux)
    AS = as --32
    CC = gcc -m32
else
    # macOS: 尝试 i386-elf-gcc，否则用 Docker
    ifeq ($(shell which i386-elf-gcc 2>/dev/null),)
        USE_DOCKER = 1
    else
        AS = i386-elf-as
        CC = i386-elf-gcc
    endif
endif
```

**编译标志说明：**

```makefile
CFLAGS = -Wall -O -fstrength-reduce -fomit-frame-pointer \
         -finline-functions -nostdinc -Iinclude \
         -fno-builtin -fno-stack-protector -m32
```

| 标志 | 说明 |
|------|------|
| `-Wall` | 启用所有警告 |
| `-O` | 基本优化 |
| `-fstrength-reduce` | 强度削减优化（乘除法→移位/加法） |
| `-fomit-frame-pointer` | 省略帧指针（用 EBP 作通用寄存器） |
| `-finline-functions` | 内联小函数 |
| `-nostdinc` | 不搜索标准头文件目录 |
| `-Iinclude` | 头文件搜索路径 |
| `-fno-builtin` | 禁用内建的 memcpy/strlen 等（我们有自己的实现） |
| `-fno-stack-protector` | 禁用栈保护（与内核不兼容） |
| `-m32` | 生成 32 位代码 |

**链接器标志：**

```makefile
LDFLAGS = -T kernel.ld -m elf_i386 -nostdlib
```

| 标志 | 说明 |
|------|------|
| `-T kernel.ld` | 使用自定义链接脚本 |
| `-m elf_i386` | 目标格式为 ELF32 i386 |
| `-nostdlib` | 不链接标准 C 库 |

**构建流程详解：**

```
make all
  ├─ tools/build (编译镜像构建工具)
  │   └─ gcc tools/build.c -o tools/build
  │
  ├─ boot/boot (引导扇区)
  │   └─ as --32 boot/boot.s -o boot/boot.o
  │   └─ objcopy -O binary boot/boot.o boot/boot
  │
  ├─ boot/setup (实模式桥梁)
  │   └─ as --32 boot/setup.s -o boot/setup.o
  │   └─ objcopy -O binary boot/setup.o boot/setup
  │
  ├─ kernel/system (内核映像)
  │   ├─ head.o + 18个C/汇编目标文件
  │   ├─ ld -T kernel.ld → kernel/system (ELF)
  │   └─ objcopy -O binary → kernel/system.bin
  │
  └─ Image (最终可启动镜像)
      └─ tools/build boot/boot boot/setup kernel/system.bin → Image
```

### 13.2 链接脚本（kernel.ld）

```ld
OUTPUT_FORMAT("elf32-i386")
ENTRY(startup_32)            /* 入口点在 head.s 中 */

SECTIONS
{
    . = 0x10800;              /* 内核加载地址 */

    .text : {
        *(.text)              /* 所有代码段 */
    }

    .data : {
        *(.data)              /* 所有已初始化数据 */
    }

    .bss : {
        *(.bss)               /* 所有未初始化数据 */
        *(COMMON)             /* 公共块 */
    }

    _end = .;                 /* _end 符号标记内核结束 */
}
```

**加载地址 0x10800 的含义：**
- 页目录在 0x100000-0x100FFF
- 4 个页表在 0x101000-0x104FFF
- 4 个未使用的页在 0x105000-0x107FFF
- 内核代码从 0x10800（偏移 0x800 在 0x100000 段内）开始

因为链接脚本基于物理内存布局，内核中的所有地址都是预先确定的，无需重定位。

**objcopy 转换：**
```bash
objcopy -O binary -R .note -R .comment kernel/system kernel/system.bin
```

将 ELF 格式的内核映像转换为纯二进制（无头信息），因为引导加载程序不理解 ELF 格式。

### 13.3 QEMU 运行与调试

**运行：**

```bash
make run
# 等价于：
qemu-system-i386 -fda Image -m 4M -boot a
```

| 参数 | 说明 |
|------|------|
| `-fda Image` | 软盘 A 的镜像文件 |
| `-m 4M` | 分配 4MB 内存 |
| `-boot a` | 从软盘 A 启动 |

**ISO 模式运行：**

```bash
make iso
make run-cd
# 等价于：
qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
```

**调试模式：**

```bash
make debug
# 等价于：
qemu-system-i386 -fda Image -m 4M -boot a -s -S
```

然后在另一个终端：
```bash
gdb kernel/system
(gdb) target remote localhost:1234
(gdb) b main
(gdb) c
```

QEMU 的 `-s` 标志在 1234 端口启动 GDB 服务器，`-S` 在启动时暂停 CPU。

**Docker 构建：**

```bash
docker build -t linux-0.01-builder .
docker run --rm -v $(pwd):/kernel -w /kernel linux-0.01-builder make clean all
```

Docker 构建确保了跨平台的一致编译环境。

---

## 附录 A：全部源文件索引

| 文件 | 行数 | 功能 |
|------|------|------|
| `boot/boot.s` | 75 | 第一阶段引导扇区 |
| `boot/setup.s` | 68 | 实模式→保护模式桥接 |
| `boot/head.s` | 280 | 32位入口，分页/IDT/GDT 设置 |
| `kernel/main.c` | 48 | 内核主函数 |
| `kernel/sched.c` | 132 | 调度器 |
| `kernel/process.c` | 136 | 进程管理（fork/exit） |
| `kernel/sys.c` | 133 | 系统调用实现 |
| `kernel/vsprintf.c` | 101 | printk 实现 |
| `kernel/panic.c` | 24 | 内核 panic |
| `kernel/asm.s` | 7 | 汇编辅助函数 |
| `mm/memory.c` | 123 | 内存管理 |
| `mm/page.s` | 29 | 页错误处理入口 |
| `fs/minix.c` | 62 | 超级块管理 |
| `fs/buffer.c` | 198 | 缓冲区缓存 |
| `fs/bitmap.c` | 113 | Bitmap 分配器 |
| `fs/inode.c` | 86 | Inode 缓存 |
| `fs/file_dev.c` | 148 | 文件读写 |
| `fs/namei.c` | 126 | 路径解析 |
| `drivers/console.c` | 90 | VGA 控制台 |
| `drivers/keyboard.c` | 78 | PS/2 键盘 |
| `drivers/hd.c` | 73 | IDE 硬盘 |
| `drivers/tty_io.c` | 33 | TTY 子系统 |
| `init/shell.c` | 158 | 用户态 Shell |
| `lib/string.c` | 105 | 字符串函数 |
| `lib/ctype.c` | 41 | 字符分类函数 |
| `lib/malloc.c` | 29 | 堆分配器 |
| `lib/close.c` | 6 | close() 包装函数 |
| `tools/build.c` | 110 | 镜像构建工具 |

## 附录 B：系统调用速查表

| 编号 | 名称 | EAX | 参数 | 返回值 |
|------|------|-----|------|--------|
| 0 | setup | EBX=ptr | — | 0 on success |
| 1 | exit | EBX=code | — | — |
| 2 | fork | — | — | PID in parent, 0 in child |
| 3 | read | EBX=fd | ECX=buf, EDX=count | 读取字节数 |
| 4 | write | EBX=fd | ECX=buf, EDX=count | 写入字节数 |
| 5 | open | EBX=filename | ECX=flags | fd |
| 6 | close | EBX=fd | — | 0 on success |
| 7 | getpid | — | — | PID |
| 8 | pause | — | — | 0 |
| 9 | time | EBX=tloc | — | 启动秒数 |

## 附录 C：I/O 端口速查表

| 端口 | 方向 | 设备 | 说明 |
|------|------|------|------|
| 0x20 | R/W | 主 PIC | 命令寄存器 |
| 0x21 | R/W | 主 PIC | 数据/掩码寄存器 |
| 0xA0 | R/W | 从 PIC | 命令寄存器 |
| 0xA1 | R/W | 从 PIC | 数据/掩码寄存器 |
| 0x40 | R/W | PIT | 通道 0 数据 |
| 0x43 | W | PIT | 命令寄存器 |
| 0x60 | R/W | 键盘 | 数据端口 |
| 0x64 | R | 键盘 | 状态端口 |
| 0x92 | R/W | 系统 | A20 门控制 |
| 0x1F0 | R/W | IDE | 数据寄存器 |
| 0x1F1 | R | IDE | 错误寄存器 |
| 0x1F2 | R/W | IDE | 扇区计数 |
| 0x1F3 | R/W | IDE | LBA[7:0] |
| 0x1F4 | R/W | IDE | LBA[15:8] |
| 0x1F5 | R/W | IDE | LBA[23:16] |
| 0x1F6 | R/W | IDE | 驱动器/磁头 |
| 0x1F7 | R/W | IDE | 状态/命令 |
| 0x3D4 | R/W | VGA | CRT 索引寄存器 |
| 0x3D5 | R/W | VGA | CRT 数据寄存器 |

## 附录 D：进一步学习方向

完成本教程后，你可以继续探索以下方向：

1. **写时复制（COW, Copy-On-Write）**：fork 时不复制物理页，只在写入时分配
2. **按需页加载（Demand Paging）**：利用页错误实现虚拟内存
3. **写回文件系统**：使 MINIX 文件系统支持写操作
4. **ELF 可执行文件加载**：替换当前的静态链接方式
5. **真实硬件引导**：在 U 盘上运行内核
6. **网络栈**：实现简单的 TCP/IP 协议栈
7. **多用户**：增加用户和权限管理
