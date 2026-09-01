# Linux 0.01 内核完整实现教程

> **总索引：** [INDEX.md](INDEX.md) · **已知限制：** [LIMITATIONS.md](LIMITATIONS.md)  
> **权威顺序：** 源码 > LIMITATIONS/本教程 > HLD/SRS

### 阅读前

1. 四份前置：`PREREQ-x86-asm.md` → `PREREQ-c-language.md` → `PREREQ-computer-arch.md` → `PREREQ-os-theory.md`
2. 先读 [LIMITATIONS.md](LIMITATIONS.md)（Shell 在内核态、仅映射 0–4MB 等）
3. 本文 §1–§6 为引导与 main 详解；§7 起见 `docs/tutorial/`

---

## 目录

### 第一部分：引导流程（本文）
1. [概述：从通电到 main()](#第一部分概述从通电到-main)
2. [boot.s — 引导扇区逐行解析](#2-boots--引导扇区逐行解析)
3. [tools/build.c — 镜像构建工具逐行解析](#3-toolsbuildc--镜像构建工具逐行解析)
4. [setup.s — 实模式到保护模式逐行解析](#4-setups--实模式到保护模式逐行解析)
5. [head.s — 32 位内核入口逐行解析](#5-heads--32-位内核入口逐行解析)
6. [main.c — 内核主函数逐行解析](#6-mainc--内核主函数逐行解析)

### 第二部分起（分文件）
7. [调度器](tutorial/07-sched.md) · 8. [进程](tutorial/08-process.md) · 9. [系统调用](tutorial/09-syscalls.md)  
10. [内存](tutorial/10-mm.md) · 11. [文件系统](tutorial/11-fs.md) · 12. [驱动](tutorial/12-drivers.md)  
13. [Shell/库](tutorial/13-shell-lib.md) · 14. [头文件与构建](tutorial/14-headers-build.md) · 15. [端到端场景](tutorial/15-scenarios.md)

---

## 第一部分：概述：从通电到 main()

### 整体启动流程时序图

```
时间线：
  0ms  CPU 上电
  │    ├─ CS:IP = 0xF000:0xFFF0
  │    └─ 执行 BIOS ROM 第一条指令
  │
  ~1s  BIOS POST 完成
  │    ├─ 扫描启动设备
  │    ├─ 读取 MBR (扇区 0) 到 0x7C00
  │    └─ 验证 0xAA55 签名 → 跳转到 0x0000:0x7C00
  │
  │    【boot.s 执行】 (~0.1ms)
  │    ├─ 启用 A20 地址线 (INT 0x15 AX=0x2401)
  │    ├─ 设置 VGA 文本模式 (INT 0x10)
  │    ├─ 读取 setup 扇区到 0x10000 (INT 0x13)
  │    ├─ 读取 kernel 扇区到 0x10000+0x800=0x10800 (INT 0x13)
  │    └─ 跳转到 0x1000:0x0000 (setup 入口)
  │
  │    【setup.s 执行】 (~0.05ms)
  │    ├─ 检查扩展内存 (INT 0x15 AH=0x88)
  │    ├─ 保存内存大小到 0x10002
  │    ├─ 启用 A20 门 (outb 0x92)
  │    ├─ 重映射 8259A PIC (IRQ0→向量0x20, IRQ8→向量0x28)
  │    ├─ 初始化 ICW 和中断掩码
  │    ├─ 加载临时 GDT + 空 IDT
  │    ├─ 设置 CR0.PE=1 进入保护模式
  │    └─ 远跳转到 0x08:0x10800 (head.s 入口)
  │
  │    【head.s 执行】 (~0.1ms)
  │    ├─ 重设段寄存器为内核选择子
  │    ├─ 设置内核栈
  │    ├─ 设置页目录和页表 (恒等映射 0-4MB)
  │    ├─ 启用分页 (CR0.PG=1)
  │    ├─ 设置 IDT (256 个中断门)
  │    ├─ 设置完整 GDT (137 个条目)
  │    ├─ 加载 GDT + IDT
  │    └─ 调用 C 语言 main()
  │
  │    【main.c 执行】 (~0.5ms 不含磁盘)
  │    ├─ 读取内存大小
  │    ├─ mem_init() — 初始化页框分配器
  │    ├─ buffer_init() — 初始化缓冲区缓存
  │    ├─ tty_init() — 初始化 TTY
  │    ├─ sys_setup() — 挂载根文件系统
  │    ├─ sched_init() — 初始化调度器
  │    ├─ sti() — 开中断
  │    └─ shell_main() — 内核态 Shell
  │
  ~1.5s 用户看到 "$ " 提示符
```

### 镜像文件布局 (Image)

```
扇区地址      物理地址(加载后)    内容
─────────────────────────────────────────
扇区 0        0x7C00 (复制到此处)  boot.bin (512B 引导扇区)
扇区 1-4      0x10000-0x10FFF    setup.bin (4×512=2048B)
扇区 5+       0x10800+           system.bin (内核 ELF→raw)
─────────────────────────────────────────
```

---

## 2. boot.s — 引导扇区逐行解析

**文件：** `boot/boot.s` (75 行)
**作用：** 作为 BIOS 引导的第一段代码，加载 setup 和 kernel 到内存。

### 第 1-8 行：头定义

```as
.code16            # ① 告诉汇编器生成 16 位实模式代码
.text               # ② 放入代码段
.globl _start       # ③ 导出入口符号

.equ SETUP_SECTORS, 4        # ④ setup 模块固定占 4 个扇区
.equ SETUPSEG, 0x1000        # ⑤ setup 加载的段地址 (0x10000)
.equ SYSSEG,   0x1000        # ⑥ kernel 加载的段地址 (0x10000)
.equ SYSOFF,   0x0800        # ⑦ kernel 在段内的偏移 (0x800)
                              #   → 物理地址 = 0x10000 + 0x800 = 0x10800
```

**① .code16** — 这是 GNU 汇编器指令，告诉汇编器生成 16 位操作码。在 x86 上，16 位和 32 位模式使用不同的指令编码，.code16 确保生成正确的实模式指令。如果没有这个指令，汇编器默认生成 32 位代码，在实模式下会出错。

**⑦ SYSOFF=0x0800** — 为什么是 0x800？
- 物理地址 0x100000-0x107FFF 被保留给页目录和页表（共 8 页 = 32KB = 0x8000）
- 但页目录和页表在 0x100000 处，不在 0x10000
- 0x800 偏移是因为内核代码在段内偏移，最终地址 = 0x10000 + 0x800 = 0x10800
- 这与链接脚本 kernel.ld 中的 `. = 0x10800` 一致

### 第 10-19 行：硬件初始化

```as
_start:
    mov %dl, (drive)          # 第11行：保存 BIOS 传入的启动驱动器号
                               # DL = 0x00 (软盘A) 或 0x80 (硬盘C)
```

**BIOS 传递 DL 寄存器的含义：**
当 BIOS 跳转到 0x7C00 时，DL 寄存器保存了启动设备的驱动器号。boot.s 需要知道从哪个设备读取后续扇区。

```as
    mov $0x2401, %ax          # 第12行：启用 A20 地址线
    int $0x15                 # 第13行：BIOS 中断
                               # AH=0x24 (功能: A20 门控制)
                               # AL=0x01 (启用 A20)
```

**为什么要启用 A20？** 这是 80286+ 兼容 8086 的历史包袱。8086 的地址线只有 20 根，当地址超过 1MB 时回绕到 0。A20 门控制第 21 根地址线，启用后才能访问 1MB 以上的内存。

```as
    mov $0x13, %ah            # 第14行：设置显示模式
    mov $0x01, %al            # 第15行：VGA 40×25 文本模式
    int $0x10                 # 第16行：调用 BIOS 视频服务
```

实际上 boot.s 不需要设置显示模式，但原来的 Linux 0.01 boot.s 包含了这一步来重置显卡状态。

```as
    mov $0x03, %ah            # 第17行：获取光标位置
    xor %bh, %bh               # 第18行：页号 = 0
    int $0x10                 # 第19行：返回 DH=行, DL=列
```

### 第 22-36 行：加载 setup 模块

```as
load_setup:
    mov (drive), %dl           # 第22行：设置驱动器号 (之前保存的)
    mov $SETUPSEG, %ax         # 第23行：目标段 = 0x1000
    mov %ax, %es               # 第24行：ES = 0x1000 (ES:BX = 0x10000)
    xor %bx, %bx               # 第25行：偏移 = 0
    mov $0x0002, %cx           # 第26行：CH=柱面0, CL=扇区2
    xor %dh, %dh               # 第27行：磁头 = 0
    mov $0x0200 | SETUP_SECTORS, %ax  # 第28行：AH=02(读), AL=4(扇区数)
    int $0x13                  # 第29行：调用 BIOS 磁盘服务
    jnc load_setup_ok          # 第30行：CF=0 表示成功，跳转
```

**磁盘 CHS 参数详解：**
```
CX = 0x0002:
  CH(高8位) = 0x00 → 柱面号 = 0
  CL(低8位) = 0x02 → 扇区号 = 2 (扇区1是引导扇区)
```

**为什么 setup 从扇区 2 开始？**
扇区 0 是引导扇区，扇区 1-4 是 setup 模块。但这里从扇区 2 开始读 4 个扇区，实际上覆盖了扇区 2-5... 等一下，让我看看 build.c 的实际布局。实际上 `tools/build.c` 将 boot 写到扇区 0，setup 写到扇区 1-4（共 4 个扇区）。所以 setup 应该从扇区 1 开始读 4 个扇区。但代码中是 CX=0x0002（扇区 2）。

**实际上：** 这里 CX=0x0002 表示扇区 2，读 4 个扇区会覆盖扇区 2-5。这意味着 setup 实际被加载了一个扇区的偏移？不，实际上 boot.s 在加载之前已经把自己读到了 0x10000 的位置（前面的代码），所以 setup 应该从正确的扇区开始。实际上新的 boot.s 实现是从扇区 1 开始加载 setup 的（之前的版本需要重定位）。

另一个可能性是：前 4 个扇区的 setup 是从扇区 1 开始的，boot.s 中确实 CX=0x0002。这可能意味着起始扇区号是从 1 编号的，CL=2 表示第二个扇区（即编号为 1 的扇区，从 0 计数）。CHS 寻址中 CL[5:0] 位表示扇区号，从 1 开始编号。

等等，CHS 中扇区编号从 1 开始，所以扇区 0 是 CHS(0,0,1)，扇区 1 是 CHS(0,0,2)。CX=0x0002 表示 CL=2，即扇区 2，而且读 4 个扇区（扇区 2,3,4,5）。这似乎多读了一个扇区。但实际上 image 布局是扇区 0=boot，扇区 1-4=setup。CX=0x0002 读 4 个扇区从扇区 2 开始，覆盖 2,3,4,5。等等，不对。

让我重新看 build.c 的输出顺序。在 build.c 中：
1. 写 boot 到扇区 0
2. 写 setup 从扇区 1 开始（4 个扇区）
3. 写 kernel 从扇区 5 开始

所以：
- 扇区 0 = boot
- 扇区 1-4 = setup
- 扇区 5+ = kernel

CX=0x0002 意味着从扇区 2 开始读... 那就错过了扇区 1。但等等，我可能看错了。CHS 中扇区编号从 1 开始，没有扇区 0。所以：
- 第一个扇区 = CHS(0,0,1)
- 第二个扇区 = CHS(0,0,2)

CX=0x0002 表示从第二个扇区开始（即镜像中的扇区 1，如果从 0 开始编号）。读 4 个扇区就是扇区 1-4。这就对了。

```as
    xor %ah, %ah              # 第31行：AH=0 (复位磁盘)
    int $0x13                 # 第32行：复位磁盘控制器
    mov $0x0200 | SETUP_SECTORS, %ax  # 第33行：再次尝试
    int $0x13                  # 第34行：
    jnc load_setup_ok          # 第35行：
    jmp _start                 # 第36行：彻底失败，重启
```

这里实现了一个简单的重试机制：首次读取失败 → 复位磁盘 → 重试一次 → 仍失败就跳回开头重启。

### 第 40-61 行：加载 kernel 模块

```as
load_kernel:
    mov $SYSSEG, %ax           # 第40行：目标段 = 0x1000
    mov %ax, %es
    mov $SYSOFF, %bx           # 第42行：偏移 = 0x800
                               # 物理地址 = 0x10000 + 0x800 = 0x10800
    mov (drive), %dl           # 第43行：驱动器号
    xor %dh, %dh               # 第44行：磁头 = 0
```

**kernel 加载地址计算：**
- 段地址 SYSSEG = 0x1000
- 偏移 SYSOFF = 0x0800
- 物理地址 = 0x1000 × 16 + 0x0800 = 0x10000 + 0x800 = 0x10800

这与链接脚本 `kernel.ld` 中的 `ENTRY(startup_32)` 和 `. = 0x10800` 匹配。

```as
    mov (kernel_sectors), %al  # 第45行：AL = kernel 的扇区数
    or %al, %al                # 第46行：检查是否为 0
    jz load_done               # 第47行：如果为 0，跳过加载
    mov $0x0001+1+SETUP_SECTORS, %cx  # 第48行：CHS 起始位置
    mov $0x02, %ah             # 第49行：读扇区
    int $0x13                  # 第50行：
    jnc load_done              # 第51行：
```

**kernel 扇区的 CHS 计算：**
- kernel 从扇区 5 开始（从 0 编号）→ CHS(0,0,6)
- CX = (柱面<<8) | 扇区号
- `0x0001 + 1 + SETUP_SECTORS` = 0x0001 + 1 + 4 = 0x0006
- CH = 0x00（柱面 0），CL = 0x06（扇区 6）
- 在 CHS 编号中扇区从 1 开始，所以扇区 6 = 偏移 5（0 基准）= 第 5 个扇区

**kernel_sectors 从哪里来？**
这个值由 `tools/build.c` 在构建镜像时写入 boot 扇区的偏移 0x1F3（在 .org 0x1F0 的区域内）。boot.s 通过 `(kernel_sectors)` 引用读取它。

```as
    /* 重试逻辑（同上） */
    xor %ah, %ah
    int $0x13
    mov (kernel_sectors), %al
    mov $0x0001+1+SETUP_SECTORS, %cx
    mov $0x02, %ah
    int $0x13
    jnc load_done
    jmp load_done              # 第61行：即使失败也继续
```

### 第 63-68 行：跳转到 setup

```as
load_done:
    mov $SETUPSEG, %ax         # 第64行：DS = 0x1000
    mov %ax, %ds               # 第65行：
    mov $0x0003, %ax           # 第66行：FS = 0x0003
    mov %ax, %fs               # 第67行：设置 FS 段
    ljmp $SETUPSEG, $0x0000    # 第68行：远跳转到 setup 入口
```

**为什么要设置 FS 为 0x0003？** 这可能是为了后续在 setup.s 或 head.s 中通过 FS 段访问某些 BIOS 数据。`FS=0x0003` 对应的物理地址是 `0x0003 × 16 = 0x00030`。

`ljmp $SETUPSEG, $0x0000` 等价于 `jmp far 0x1000:0x0000`——CS=0x1000, IP=0x0000，物理地址=0x10000，即 setup 的入口。

### 第 70-75 行：数据与签名

```as
drive:          .byte 0           # 第70行：保存启动驱动器号

.org 0x1F0                       # 第71行：将位置计数器跳到 0x1F0
setup_sectors:  .word SETUP_SECTORS  # 第72行：偏移 0x1F0 — setup 扇区数
kernel_sectors: .word 0             # 第73行：偏移 0x1F2 — kernel 扇区数
                                     # (由 build.c 填入)

.org 0x1FE                       # 第74行：跳到扇区末尾-2
.word 0xAA55                     # 第75行：BIOS 引导签名
```

**引导签名：** 0xAA55 在小端序存储为 [0x55, 0xAA]，这是 BIOS 识别有效引导扇区的标准标记。BIOS 检查偏移 510 (0x1FE) 和 511 (0x1FF) 是否分别为 0x55 和 0xAA，只有匹配时才执行引导代码。

**kernel_sectors 的 Magic Number：** 注意这个偏移 (0x1F3, 0x1F2) 在 `tools/build.c` 中被写入：
```c
buf[0x1F0] = setup_sectors & 0xFF;
buf[0x1F1] = (setup_sectors >> 8) & 0xFF;
buf[0x1F2] = kernel_sectors & 0xFF;
buf[0x1F3] = (kernel_sectors >> 8) & 0xFF;
```

---

## 3. tools/build.c — 镜像构建工具逐行解析

**文件：** `tools/build.c` (110 行)
**作用：** 将三个独立文件（boot, setup, kernel）合并为一个可启动的软盘镜像。

### 宏定义

```c
#define SETUP_SECTORS 4          // setup 始终占 4 个扇区
#define BOOT_SIZE 512            // 引导扇区大小
#define SECTOR_SIZE 512          // 扇区大小
```

**SETUP_SECTORS=4** — setup 模块固定占 4 个扇区（2048 字节）。如果实际 setup 大小超过 2KB，工具会发出警告。小于 2KB 时用零填充。

### main() 函数解析

**第 18-25 行：变量声明与参数检查**

```c
int main(int argc, char *argv[])
{
    FILE *boot_f, *setup_f, *system_f, *out;
    unsigned char buf[65536];    // 64KB 缓冲区
    int len;
    long setup_size, system_size;
    unsigned int setup_sectors, kernel_sectors;

    if (argc != 4)
        die("Usage: build boot setup system");
```

**buf[65536]** — 64KB 缓冲区用于：
1. 存储引导扇区（前 512 字节）
2. 后续用于读写 setup 和 kernel 数据

**第 28-36 行：读取并验证引导扇区**

```c
    boot_f = fopen(argv[1], "rb");
    if (!boot_f) die("Cannot open boot file");
    len = fread(buf, 1, BOOT_SIZE, boot_f);
    fclose(boot_f);

    if (len != BOOT_SIZE)
        die("Boot must be exactly 512 bytes");    // 必须正好 512B
    if (buf[510] != 0x55 || buf[511] != 0xAA)    // 检查签名
        die("Boot must have 0xAA55 signature");
```

**第 39-52 行：测量 setup 大小**

```c
    setup_f = fopen(argv[2], "rb");
    if (!setup_f) die("Cannot open setup file");
    fseek(setup_f, 0, SEEK_END);
    setup_size = ftell(setup_f);           // 获取文件大小
    fseek(setup_f, 0, SEEK_SET);

    setup_sectors = (setup_size + SECTOR_SIZE - 1) / SECTOR_SIZE;  // 向上取整
    if (setup_sectors > SETUP_SECTORS) {
        fprintf(stderr, "Warning: setup is %ld bytes, needs %d sectors; "
                "truncating to %d\n", setup_size, setup_sectors, SETUP_SECTORS);
        setup_sectors = SETUP_SECTORS;     // 截断
    }
    setup_sectors = SETUP_SECTORS;         // 始终使用 4 扇区
```

**第 55-63 行：测量 kernel 大小**

```c
    system_f = fopen(argv[3], "rb");
    if (!system_f) die("Cannot open system file");
    fseek(system_f, 0, SEEK_END);
    system_size = ftell(system_f);
    fseek(system_f, 0, SEEK_SET);

    kernel_sectors = (system_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    fprintf(stderr, "Kernel: %ld bytes -> %d sectors\n",
            system_size, kernel_sectors);
```

**第 66-73 行：向引导扇区写入扇区计数**

```c
    buf[0x1F0] = setup_sectors & 0xFF;         // setup 扇区数低字节
    buf[0x1F1] = (setup_sectors >> 8) & 0xFF;  // setup 扇区数高字节
    buf[0x1F2] = kernel_sectors & 0xFF;        // kernel 扇区数低字节
    buf[0x1F3] = (kernel_sectors >> 8) & 0xFF; // kernel 扇区数高字节
```

这些值对应 boot.s 中 `.org 0x1F0` 区域的定义。boot.s 读取这些值来确定要加载多少扇区。

**第 76-91 行：写入 boot 扇区**

```c
    out = fopen("Image", "wb");
    if (fwrite(buf, 1, BOOT_SIZE, out) != BOOT_SIZE)
        die("Write error on boot");
```

**第 80-84 行：写入 setup 模块**

```c
    memset(buf, 0, sizeof(buf));              // 清空缓冲区
    len = fread(buf, 1, sizeof(buf), setup_f);
    fclose(setup_f);
    if (fwrite(buf, 1, setup_sectors * SECTOR_SIZE, out)
        != (unsigned)(setup_sectors * SECTOR_SIZE))
        die("Write error on setup");
```

setup 固定写入 2048 字节（4×512），不足部分自动补零。

**第 87-100 行：写入 kernel 模块并填充**

```c
    memset(buf, 0, sizeof(buf));
    while ((len = fread(buf, 1, sizeof(buf), system_f)) > 0)
        if (fwrite(buf, 1, len, out) != (size_t)len)
            die("Write error on system");
    fclose(system_f);

    /* Pad system to sector boundary */
    {
        unsigned int remain = (kernel_sectors * SECTOR_SIZE) - system_size;
        if (remain > 0) {
            memset(buf, 0, sizeof(buf));
            fwrite(buf, 1, remain, out);
        }
    }
```

内核写入后在末尾补零，使其大小对齐到扇区边界。

**第 104-109 行：输出文件大小**

```c
    struct stat st;
    if (stat("Image", &st) == 0)
        fprintf(stderr, "Image: %ld bytes (%ld KB)\n",
                (long)st.st_size, (long)(st.st_size / 1024));

    return 0;
```

### build.c 数据流图

```
boot.bin (512B)  ─┐
                  ├─→ buf[0..511] → fwrite(buf, 512)  → Image[扇区0]
                  │
setup.bin (≤2KB)─┐
                  ├─→ buf[0..2047] → fwrite(buf, 2048) → Image[扇区1-4]
                  │
kernel.bin ───────┐
                  └─→ 分块读写 → fwrite → Image[扇区5+]
                     末尾填充零到扇区边界
```

---

## 4. setup.s — 实模式到保护模式逐行解析

**文件：** `boot/setup.s` (68 行)
**作用：** 从 16 位实模式切换到 32 位保护模式的桥梁代码。

### 第 1-13 行：头定义与段初始化

```as
.code16            # 16 位代码
.text
.globl _start

.equ SETUPSEG, 0x1000
.equ SYSSEG,   0x1000
.equ SYSOFF,   0x0800

_start:
    mov $SETUPSEG, %ax         # DS = SS = 0x1000
    mov %ax, %ds
    mov %ax, %ss
    mov $0x4000, %sp           # SP = 0x4000 → 栈顶 = 0x14000
```

**栈指针选择：** SS:SP = 0x1000:0x4000 → 物理栈顶 = 0x10000 + 0x4000 = 0x14000。这个位置在已加载的 setup 区段内，但 setup 只有 2KB，SP 设置得足够高以避免覆盖代码。

### 第 15-17 行：检测扩展内存

```as
    mov $0x88, %ah             # AH=0x88: 获取扩展内存
    int $0x15                  # BIOS 系统服务
    mov %ax, (2)               # 保存到 DS:2 = 0x10002
```

**INT 0x15 AH=0x88 返回：**
- AX = 1MB 以上内存的 KB 数
- 例如：4MB 内存 → AX = 3072 (3MB × 1024KB/MB = 3072KB)
- 结果存储到物理地址 0x10002，供 main.c 读取

### 第 19-23 行：安装保护模式

```as
    cli                        # 关中断！保护模式需要 IDT
```

**为什么要 CLI？** 进入保护模式后，IDT 还未加载（实际上是空 IDT），如果此时发生中断，CPU 不知道如何处理，系统会崩溃。所以必须在进入保护模式前禁用中断。

```as
    inb $0x92, %al             # 读系统控制端口 A
    orb $0x02, %al             # 设置 Bit 1 (A20 门)
    outb %al, $0x92
```

**0x92 端口 (System Control Port A)：**
```
Bit 0: 快速复位 (写1触发)
Bit 1: A20 门 (1=启用)
Bit 2-7: 保留
```

### 第 25-44 行：8259A PIC 初始化

这是 setup.s 中代码量最大的部分，也是最关键的。理解这段代码需要知道 PIC 的初始化命令字（ICW）序列。

**ICW1 (第 25-27 行)：**

```as
    mov $0x11, %al             # ICW1: 边沿触发, 级联模式, 需ICW4
    outb %al, $0x20            # 写入主 PIC
    outb %al, $0xA0            # 写入从 PIC
```

0x11 = 0001_0001:
- Bit 4 = 1: ICW1 标志
- Bit 3 = 0: 边沿触发 (Edge Triggered)
- Bit 1 = 0: 级联模式 (Cascade)
- Bit 0 = 1: 需要 ICW4

**ICW2 (第 28-31 行) — 设置中断向量基址：**

```as
    mov $0x20, %al             # 主 PIC: IRQ0→INT 0x20
    outb %al, $0x21
    mov $0x28, %al             # 从 PIC: IRQ8→INT 0x28
    outb %al, $0xA1
```

映射结果：
```
主 PIC IRQ0 → INT 0x20 (时钟)
主 PIC IRQ1 → INT 0x21 (键盘)
主 PIC IRQ2 → INT 0x22 (级联从PIC)
...
从 PIC IRQ8 → INT 0x28 (RTC)
从 PIC IRQ14 → INT 0x2E (IDE)
```

**ICW3 (第 32-35 行) — 级联设置：**

```as
    mov $0x04, %al             # 主 PIC: IRQ2 连接从 PIC
    outb %al, $0x21
    mov $0x02, %al             # 从 PIC: 连接到主 PIC 的 IRQ2
    outb %al, $0xA1
```

主 PIC 的 Bit 2 设为 1，表示 IRQ2 用于级联。
从 PIC 的值 0x02 表示它作为主 PIC 的 IRQ2 的从设备。

**ICW4 (第 36-38 行) — 操作模式：**

```as
    mov $0x01, %al             # ICW4: 8086/8088 模式
    outb %al, $0x21
    outb %al, $0xA1
```

0x01: 8086 模式（非 8085 模式），自动 EOI 关闭，缓冲模式关闭。

**OCW1 (第 39-44 行) — 中断掩码：**

```as
    mov $0x00, %al             # 主 PIC: 启用所有中断 (mask=0)
    outb %al, $0x21
    mov $0xFF, %al             # 从 PIC: 禁用所有中断 (mask=0xFF)
    outb %al, $0xA1
```

主 PIC 全部启用（mask=0），从 PIC 全部禁用（mask=0xFF）。这是因为从 PIC 上连接了 IRQ14（硬盘），在 head.s 中启用硬盘中断之前，先禁用从 PIC 的所有中断。

### 第 46-55 行：进入保护模式

```as
    lgdt (gdt_descr - _start)  # 加载临时 GDT
    lidt (idt_descr - _start)  # 加载空 IDT（限长=0）
```

**地址计算：** `(gdt_descr - _start)` — 这是相对于 setup 模块起始的偏移。因为 setup 不在 0x0000 地址，符号的值需要减去 _start 得到相对偏移。LGDT/LIDT 接受的是相对于 DS 的地址。

```as
    mov %cr0, %eax             # 读 CR0
    or  $1, %al                # 设置 PE 位 (Bit 0)
    mov %eax, %cr0             # 写回 → 进入保护模式！
```

设置 CR0.PE=1 后，CPU 立即进入保护模式。下一条指令将在保护模式下执行。

```as
    .byte 0x66, 0xea           # 手动编码远跳转 (JMP FAR)
    .long SYSSEG*16 + SYSOFF   # 目标偏移 = 0x10000 + 0x800 = 0x10800
    .word 0x08                 # 目标段选择子 = GDT[1] (内核代码段)
```

**为什么手动编码而不是用 LJMP？**
`.code16` 模式下的 `ljmp` 生成 16 位操作数格式，但我们需要的地址是 32 位的。`0x66` 是操作数大小前缀（16→32），`0xEA` 是远跳转操作码。手动编码：
```
.byte 0x66       → 使用 32 位偏移
.byte 0xEA       → JMP FAR
.long 0x10800    → 32 位偏移 (EIP)
.word 0x08       → 16 位段选择子 (CS)
```

这等价于保护模式下的 `jmp $0x08, $0x10800`。

**此时发生了什么？**
1. CS 被设为 0x08（内核代码段选择子，基址=0，限长=4GB）
2. EIP 被设为 0x10800（head.s 的 startup_32 入口）
3. CPU 刷新预取队列，开始以 32 位保护模式执行 head.s

### 第 57-68 行：临时 GDT 和空 IDT

```as
gdt:
    .quad 0x0000000000000000    # GDT[0]: 空描述符（硬件要求）
    .quad 0x00CF9A000000FFFF    # GDT[1]: 内核代码段
    .quad 0x00CF92000000FFFF    # GDT[2]: 内核数据段
```

**GDT[1] 代码段描述符解析：**

```
0x00CF_9A00_0000_FFFF

分段解析:
  高32位: 0x00CF9A00
    基址[31:24] = 0x00
    G/D/B/AVL = 0xC = 1100
      G=1 (4KB 粒度, 限长单位=4096)
      D=1 (32位代码)
      0 保留
      AVL=0
    限长[19:16] = 0xF
    P/DPL/S/Type = 0x9A = 1001_1010
      P=1 (存在)
      DPL=00 (Ring 0)
      S=1 (代码/数据段)
      Type=1010 (代码段, 可执行, 可读, 非一致)
  基址[23:16] = 0x00
  
  低32位: 0x0000FFFF
    基址[15:0] = 0x0000
    限长[15:0] = 0xFFFF

最终:
  基址 = 0x00000000 (从地址 0 开始)
  限长 = 0xFFFFF × 4KB = 4GB (G=1)
```

**GDT[2] 数据段描述符解析：**

```
0x00CF_9200_0000_FFFF

Type = 0010 (数据段, 可读写, 向上扩展)
其余与代码段相同
```

```as
gdt_descr:
    .word (3*8)-1             # 限长 = 23 (3个条目×8字节-1)
    .long (gdt - _start + SETUPSEG*16)  # 基址 = GDT 的物理地址

idt_descr:
    .word 0                   # 限长 = 0 (禁用中断)
    .long 0                   # 基址 = 0
```

**IDT 为什么是空的？** 进入保护模式时还没有设置 IDT。LIDT 加载一个限长为 0 的空 IDT，只要不发生中断就不需要它。如果发生中断，CPU 会因为 IDT 限长不足而触发 #GP。

---

## 5. head.s — 32 位内核入口逐行解析

**文件：** `boot/head.s` (280 行)
**作用：** 保护模式下执行的第一段代码，设置分页、IDT、GDT，然后跳转到 C 语言 main()。

### 第 1-13 行：全局符号和常量定义

```as
.text
.globl startup_32, main                        # 导出入口和 main
.globl divide_error, timer_interrupt, system_call  # 导出中断处理程序
.globl keyboard_interrupt, hd_interrupt
.globl _gdt, _idt, gdt_descr, idt_descr        # 导出 GDT/IDT 给 C 代码

.equ PGDIR, 0x100000        # 页目录物理地址 (1MB)
.equ PGTBL0, 0x101000       # 页表 0 物理地址
.equ KERNEL_CS, 0x08        # 内核代码段选择子
.equ KERNEL_DS, 0x10        # 内核数据段选择子
.equ USER_CS, 0x1B          # GDT[3]|RPL3 — 与 include/linux/head.h 一致
.equ USER_DS, 0x23          # GDT[4]|RPL3 — system_call 中 FS 使用此值
```

**本仓库 GDT 布局（head.s `_gdt`）：**

| 索引 | 选择子 | 含义 |
|------|--------|------|
| 0 | 0x00 | 空 |
| 1 | 0x08 | 内核代码 |
| 2 | 0x10 | 内核数据 |
| 3 | 0x1B (RPL3) | 用户代码 |
| 4 | 0x23 (RPL3) | 用户数据 |
| 8+ | TSS/LDT | 每任务一对 |

### 第 14-23 行：startup_32 — 内核入口

```as
startup_32:
    mov $KERNEL_DS, %ax        # AX = 0x10 (内核数据段)
    mov %ax, %ds               # 设置所有数据段
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss               # 设置栈段
```

进入 head.s 时，CS 已经被 setup.s 的远跳转设为 0x08（内核代码段），但其他段寄存器仍是实模式的随机值。必须全部重新设置为内核数据段选择子。

```as
    lea _end, %esp             # ESP = _end 符号的地址
    add $0x1000, %esp          # ESP += 4096 (留一页作栈空间)
```

**内核栈设置：**
- _end 是链接脚本定义的符号，标记内核映像的结束
- 内核栈在 _end 之后 4KB 处，向下增长
- 例如：如果 _end = 0x12000，则栈顶 = 0x13000

### 第 25-26 行：初始化分页和 IDT

```as
    call setup_paging
    call setup_idt
```

### 第 28-40 行：加载 GDT 并重新初始化

```as
    lgdt gdt_descr              # 加载完整的 GDT
    ljmp $KERNEL_CS, $flush_cs  # 刷新 CS（远跳转）
```

**为什么要重新加载 GDT？**
setup.s 加载了一个只有 3 个条目的临时 GDT。head.s 中定义了完整的 137 个条目的 GDT（5 个基本段 + 132 个为任务 TSS/LDT 预留）。需要用 LGDT 加载新的描述符。

```as
flush_cs:
    mov $KERNEL_DS, %ax        # 重新设置所有段寄存器
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    lea _end, %esp             # 重新设置栈（可能的冗余但无害）
    add $0x1000, %esp

    xor %eax, %eax
    mov %eax, %cr2             # 清空 CR2 (页错误地址)
```

### 第 45-48 行：跳转到 C 语言 main()

```as
    call main                   # 调用 C 语言内核主函数

_idle:
    jmp _idle                   # main 返回后自旋（不应发生）
```

**为什么使用 CALL 而不是 JMP？**
CALL 将返回地址压栈，main() 可以通过 RET 返回。但实际上 main() 不应该返回——如果返回了，CPU 进入 _idle 死循环。

### 第 50-73 行：setup_paging — 分页设置

这是 head.s 中最重要的函数，建立内核的页表。

```as
setup_paging:
    mov $PGDIR, %eax            # EAX = 0x100000 (页目录地址)
    mov %eax, %cr3              # 设置 CR3 (页目录基址)
```

**第 54-57 行：清空页目录和页表区域**

```as
    mov $PGDIR, %edi            # EDI = 0x100000
    xor %eax, %eax              # EAX = 0
    mov $0x1000, %ecx           # ECX = 4096 (清空 4096 个双字 = 16KB)
    rep stosl                   # 重复 STOSL 4096 次
```

清空的区域：0x100000-0x103FFF。这里包括页目录（0x100000-0x100FFF）和 4 个页表（0x101000-0x104FFF 但只清空到 0x103FFF，4KB×4096=16KB）。

**第 59-61 行：设置页目录条目**

```as
    mov $PGDIR, %edi            # EDI = 0x100000 (页目录)
    lea (PGTBL0 + 0x07), %eax   # EAX = 0x101007
    stosl                       # PDE[0] = 0x101007
```

**PDE[0] = 0x101007 的含义：**
- 0x101000 (Bit 31-12) = 页表 0 的物理地址
- 0x007 (Bit 11-0) = 0111:
  - Bit 0 (P) = 1: 存在
  - Bit 1 (R/W) = 1: 可读写
  - Bit 2 (U/S) = 1: 用户可访问——**PDE 保留 U/S 位，由每个 PTE 单独决定该页是否对 Ring3 开放**（内存隔离的控制点在 PTE）

**注意**：这里只设置了一个 PDE！只映射了 PDE[0]，覆盖 0-4MB 的地址空间。这就是为什么如果系统有超过 4MB 的 RAM，main.c 中的 `if (memory_end > 0x400000) memory_end = 0x400000` 会截断内存。

**第 63-68 行：填充页表 0 的条目**

```as
    mov $PGTBL0, %edi           # EDI = 0x101000 (页表 0)
    lea 0x03, %eax              # EAX = 0x000003 (第一个 PTE: P+RW, 无 U/S)
    mov $0x400, %ecx            # ECX = 1024 (填充 1024 个 PTE)
1:  stosl                       # PTE = EAX, EDI += 4
    add $0x1000, %eax           # EAX += 4096 (下一个物理页)
    loop 1b                     # 循环 1024 次
```

**填充意义（内存隔离）：**
- PTE[0] = 0x000003 → 线性地址 0x000000 映射到物理页 0x000000（**0x03 = P+RW，无 U/S → 内核专属**）
- PTE[1] = 0x001003 → 线性地址 0x001000 映射到物理页 0x001000
- PTE[1023] = 0x3FF003 → 线性地址 0x3FF000 映射到物理页 0x3FF000
- **默认全 0x03：0-4MB 全部只有 Ring0 能访问**。用户程序/堆/栈页在启动（`grant_user_pages` 授权堆+栈 0x310000-0x400000）和 `execve`（授权程序区 0x200000 起）时把对应 PTE 置为 0x07（P+RW+U/S）。Ring3 访问未授权页 → page fault → `do_no_page` panic。

**恒等映射**：所有线性地址等于物理地址。

**第 70-73 行：启用分页**

```as
    mov %cr0, %eax              # 读 CR0
    or  $0x80000001, %eax       # 设置 PG (Bit 31) 和 PE (Bit 0)
    mov %eax, %cr0              # 写回 → 分页开启！
    ret
```

`0x80000001` = Bit 31(PG) | Bit 0(PE)。同时设置保护模式使能和分页使能。

**`RET` 指令的隐身作用**：RET 修改 EIP，强制 CPU 重新获取指令预取队列，刷新了 TLB 和指令缓存。

### 第 75-114 行：setup_idt — IDT 设置

**第 76-89 行：填充所有 256 个入口为默认处理程序**

```as
setup_idt:
    lea _idt, %edi              # EDI = IDT 表基址
    mov $256, %ecx              # 256 个入口
    lea ignore_int, %edx        # EDX = 默认处理程序地址
    mov %edx, %eax              # EAX = 处理程序地址
    shr $16, %eax               # EAX = 地址高 16 位
    shl $16, %eax               # EAX = 高16位上移
    or  $0x8E00, %eax           # 加上门描述符标志
```

**门描述符高 4 字节的构造过程：**
```
原始 EDX = 0x0001XXXX (处理程序地址, 32位)

步骤1: mov %edx, %eax              → EAX = 0x0001XXXX
步骤2: shr $16, %eax               → EAX = 0x00000001
步骤3: shl $16, %eax               → EAX = 0x00010000
步骤4: or $0x8E00, %eax            → EAX = 0x00018E00
                                        = 偏移[31:16]=1, P=1, DPL=0, 中断门
```

```as
    mov %edx, %ebx              # EBX = 处理程序地址
    and $0xFFFF, %ebx           # EBX = 偏移[15:0]
    or  $0x00080000, %ebx       # EBX = 偏移[15:0] | 段选择子(0x0008)
```

**门描述符低 4 字节的构造过程：**
```
EBX = 偏移[15:0] | 0x00080000
    = XXXX | 0x00080000
    = 0x0008XXXX
```

```as
1:  mov %ebx, (%edi)            # 写低 4 字节 → [偏移15:0][段选择子]
    mov %eax, 4(%edi)           # 写高 4 字节 → [偏移31:16][P|DPL|Type]
    add $8, %edi                # 下一个入口
    loop 1b
```

**中断描述符最终格式（8 字节）：**
```
低4字节 (EBX):  偏移[15:0]   段选择子(0x0008)
高4字节 (EAX):  偏移[31:16]  P(1) DPL(00) 0 D(1) 1 1 Type(110=中断门)
```

**第 106-111 行：为特定中断设置专用处理程序**

```as
    set_idt_entry 0, divide_error, 0x8E00       # 除零错误
    set_idt_entry 0x20, timer_interrupt, 0x8E00  # 时钟 IRQ0
    set_idt_entry 0x21, keyboard_interrupt, 0x8E00 # 键盘 IRQ1
    set_idt_entry 0x2E, hd_interrupt, 0x8E00    # 硬盘 IRQ14
    set_idt_entry 0x0E, page_fault, 0x8E00      # 页错误
    set_idt_entry 0x80, system_call, 0xEF00     # 系统调用 (DPL=3!)
```

**set_idt_entry 宏解析（第 91-104 行）：**

```as
    .macro set_idt_entry vec, handler, type
    lea _idt, %edi              # IDT 基址
    add $\vec*8, %edi           # 偏移到对应入口 (vec × 8)
    lea \handler, %edx          # 处理程序地址
    mov %edx, %eax
    shr $16, %eax
    shl $16, %eax
    or  $\type, %eax            # 使用指定的 type (0x8E00 或 0xEF00)
    mov %edx, %ebx
    and $0xFFFF, %ebx
    or  $0x00080000, %ebx
    mov %ebx, (%edi)
    mov %eax, 4(%edi)
    .endm
```

**0xEF00 vs 0x8E00：**
- 0x8E00: DPL=00 (Ring 0 才能触发)，用于硬件中断和异常
- 0xEF00: DPL=11 (Ring 3 也可以触发)，用于用户态系统调用

```as
    lidt idt_descr              # 加载 IDT 描述符到 IDTR
    ret
```

### 第 116-117 行：默认中断处理程序

```as
ignore_int:
    iret                        # 什么都不做，直接返回
```

这个极简版本只是直接返回。原始 Linux 0.01 的 `ignore_int` 会打印中断信息，但本项目简化了。

### 第 119-141 行：timer_interrupt — 时钟中断处理

```as
timer_interrupt:
    push %ds                    # 保存调用者的段寄存器
    push %es
    push %fs
    push %gs
    pushal                      # 保存所有通用寄存器
    mov $KERNEL_DS, %ax         # 切换到内核数据段
    mov %ax, %ds
    mov %ax, %es                # ES 也设为内核数据段
```

**为什么 FS 和 GS 不显式保存？** `push %fs` / `push %gs` 在第 127-128 行已经保存了。

**PUSHAL 保存的寄存器：** EAX, ECX, EDX, EBX, ESP(原始), EBP, ESI, EDI（共 8 个）

```as
    mov $0x20, %al              # EOI 命令
    outb %al, $0x20             # 发送给主 PIC
    call do_timer                # 调用 C 语言处理函数
    popal                        # 恢复通用寄存器
    pop %gs                     # 恢复段寄存器 (逆序)
    pop %fs
    pop %es
    pop %ds
    iret                        # 中断返回
```

**为什么 EOI 在 CALL 之前？**
发送 EOI 告知 PIC 当前中断已处理完毕。这样在 `do_timer` 执行期间，PIC 可以接收新的 IRQ0 中断（虽然不会立即响应，因为 IF=0）。

### 第 143-196 行：system_call — 系统调用入口

这是整个内核中最复杂的汇编例程之一。

```as
system_call:
    movl %esp, syscall_esp      # 保存系统调用发生时的 ESP
```

**syscall_esp 的用途**：在 sys_fork 中，需要知道系统调用返回时的栈帧位置，syscall_esp 提供了这个参考点。

```as
    push %ds                    # 保存段寄存器
    push %es
    push %fs
    push %gs

    push %eax                   # 保存系统调用号
```

**此时的栈布局：**

```
高地址
┌─────────────┐ ← 旧 ESP (Ring 3)
│   SS (用户)  │ ← 仅当特权级切换 (Ring 3 → Ring 0)
│   ESP (用户) │
│   EFLAGS    │
│   CS (用户)  │
│   EIP (用户) │ ← CPU 自动压入
├─────────────┤
│   DS        │ ← push %ds
│   ES        │
│   FS        │
│   GS        │
│   EAX (nr)  │ ← push %eax (系统调用号)
│   EBP       │ ← push %ebp (假参数 arg5)
│   EDI       │ ← push %edi (arg4)
│   ESI       │ ← push %esi (arg3)
│   EDX       │ ← push %edx (arg2)
│   ECX       │ ← push %ecx (arg1)
│   EBX       │ ← push %ebx (arg0)
└─────────────┘ ← ESP (当前)
低地址
```

```as
    push %ebp                   # 假参数 arg5 (与前面的push顺序一致)
    push %edi                   # arg4
    push %esi                   # arg3
    push %edx                   # arg2 = 系统调用参数3
    push %ecx                   # arg1 = 系统调用参数2
    push %ebx                   # arg0 = 系统调用参数1
```

**系统调用参数在寄存器中的约定：**
- EBX → 参数 1（如 fd, filename）
- ECX → 参数 2（如 buf, flags）
- EDX → 参数 3（如 count）

通过压栈构造了函数调用所需的参数列表。

```as
    mov $KERNEL_DS, %ax         # DS, ES = 内核数据段
    mov %ax, %ds
    mov %ax, %es
    mov $USER_DS, %ax           # FS = 用户数据段（用于访问用户空间）
    mov %ax, %fs
    mov $KERNEL_DS, %ax         # GS = 内核数据段
    mov %ax, %gs
```

**FS = USER_DS 的关键设计：**
`USER_DS = 0x23`，基址为 0。通过 FS 段访问用户空间指针时，`get_fs_byte` 和 `put_fs_byte` 宏使用 `%%fs:` 前缀：

```c
#define get_fs_byte(addr) ({ \
    register char __res; \
    __asm__("movb %%fs:%1, %0" : "=r"(__res) : "m"(*(addr))); \
    __res; })
```

这允许内核在 Ring 0 下安全地读取 Ring 3 的内存。

```as
    mov 24(%esp), %eax          # 从栈上恢复系统调用号
```

为什么是偏移 24？
```
EBX   → ESP+0
ECX   → ESP+4
EDX   → ESP+8
ESI   → ESP+12
EDI   → ESP+16
EBP   → ESP+20
EAX   → ESP+24 ← 系统调用号在这里
GS    → ESP+28
FS    → ESP+32
ES    → ESP+36
DS    → ESP+40
```

```as
    cmpl $10, %eax              # 系统调用号 < 10 ?
    jb 1f                       # 合法 → 跳转
    movl $-1, %eax              # 非法 → 返回 -1
    jmp 2f
1:  call *sys_call_table(,%eax,4)  # 通过跳转表调用
```

**`call *sys_call_table(,%eax,4)` 详解：**
- `sys_call_table` 是函数指针数组的基址
- `,%eax,4` → 索引寄存器=EAX，比例因子=4
- 计算地址 = `sys_call_table + EAX * 4`
- EAX=0 → sys_setup, EAX=1 → sys_exit, EAX=4 → sys_write, ...

```as
.globl ret_from_sys_call
ret_from_sys_call:
2:
    mov %eax, 24(%esp)          # 将返回值写入栈中保存的 EAX 位置
```

**返回值传递**：函数返回值在 EAX 中。栈上偏移 24 处的 EAX 将被后续的 POP 恢复。写入栈上的 EAX 位置确保 IRET 恢复寄存器时能得到正确的返回值。

```as
    pop %ebx                    # 恢复参数寄存器 (逆序)
    pop %ecx
    pop %edx
    pop %esi
    pop %edi
    pop %ebp
    pop %eax                    # 恢复 EAX (此时是返回值)
    pop %gs                     # 恢复段寄存器
    pop %fs
    pop %es
    pop %ds
    iret                        # 返回用户态
```

### 第 198-239 行：其他中断处理程序

**键盘中断 (keyboard_interrupt)：**

```as
keyboard_interrupt:
    push %ds / %es / %fs / %gs  # 保存段寄存器
    pushal                      # 保存通用寄存器
    mov $KERNEL_DS, %ax         # 切换到内核数据段
    mov %ax, %ds
    mov %ax, %es
    xor %al, %al
    inb $0x60, %al              # 从端口 0x60 读取扫描码
    push %eax                   # 将扫描码作为参数压栈
    call kbd_interrupt_handler  # 调用 C 处理函数
    pop %eax                    # 清理参数
    mov $0x20, %al              # 发送 EOI
    outb %al, $0x20
    popal                        # 恢复寄存器
    pop %gs / %fs / %es / %ds
    iret
```

**硬盘中断 (hd_interrupt)：**

```as
hd_interrupt:
    push %ds / %es / %fs / %gs
    pushal
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov $0x20, %al
    outb %al, $0x20             # EOI 发往主 PIC
    outb %al, $0xA0             # EOI 发往从 PIC (关键!)
    call hd_interrupt_handler   # 调用 C 处理函数
    popal
    pop %gs / %fs / %es / %ds
    iret
```

**为什么要向两个 PIC 发送 EOI？** IRQ14（硬盘）连接到从 PIC。当从 PIC 的中断被响应时，主 PIC 的 IRQ2 也被触发。处理完成后必须向主 PIC 和从 PIC 都发送 EOI，否则相关的中断线不会被释放。

### 第 241-280 行：数据段

```as
.data
.globl _gdt, _idt, gdt_descr, idt_descr
.globl syscall_esp

syscall_esp:
    .long 0                     # 系统调用栈指针

divide_msg:
    .string "Divide error"      # 除零错误消息

_gdt:
    .quad 0x0000000000000000    # [0] 空描述符
    .quad 0x00CF9A000000FFFF    # [1] 内核代码段 (DPL=0)
    .quad 0x00CF92000000FFFF    # [2] 内核数据段 (DPL=0)
    .quad 0x00CFFA000000FFFF    # [3] 用户代码段 (DPL=3)
    .quad 0x00CFF2000000FFFF    # [4] 用户数据段 (DPL=3)
    .fill 132, 8, 0             # [5-136] 为 TSS/LDT 预留 132 个条目
                                 # (支持 66 个进程, 每进程一对 TSS+LDT)

gdt_descr:
    .word (137*8)-1             # 限长 = 137×8-1 = 1095
    .long _gdt                  # GDT 基址

_idt:
    .fill 256, 8, 0             # 256 个中断门, 每个 8 字节

idt_descr:
    .word (256*8)-1             # 限长 = 2047
    .long _idt                  # IDT 基址

sys_call_table:
    .long sys_setup             # 0
    .long sys_exit              # 1
    .long sys_fork              # 2
    .long sys_read              # 3
    .long sys_write             # 4
    .long sys_open              # 5
    .long sys_close             # 6
    .long sys_waitpid           # 7
    .long sys_creat             # 8
    .long sys_link              # 9
    .long sys_unlink            # 10
    .long sys_execve            # 11
    .long sys_chdir             # 12
    .long sys_time              # 13
    .long sys_mknod             # 14
    .long sys_chmod             # 15
    .long sys_chown             # 16
    .long sys_break             # 17 (stub)
    .long sys_stat              # 18
    .long sys_lseek             # 19
    .long sys_getpid            # 20
    .long sys_mount             # 21 (stub)
    .long sys_umount            # 22 (stub)
    .long sys_setuid            # 23
    .long sys_getuid            # 24
    .long sys_stime             # 25
    .long sys_ptrace            # 26 (stub)
    .long sys_alarm             # 27
    .long sys_fstat             # 28
    .long sys_pause             # 29
    .long sys_utime             # 30
    .long sys_stty              # 31 (stub)
    .long sys_gtty              # 32 (stub)
    .long sys_access            # 33
    .long sys_nice              # 34
    .long sys_ftime             # 35 (stub)
    .long sys_sync              # 36
    .long sys_kill              # 37
    .long sys_rename            # 38
    .long sys_mkdir             # 39
    .long sys_rmdir             # 40
    .long sys_dup               # 41
    .long sys_pipe              # 42
    .long sys_times             # 43
    .long sys_prof              # 44 (stub)
    .long sys_brk               # 45
    .long sys_setgid            # 46
    .long sys_getgid            # 47
    .long sys_signal            # 48
    .long sys_geteuid           # 49
    .long sys_getegid           # 50
    .long sys_acct              # 51 (stub)
    .long sys_phys              # 52 (stub)
    .long sys_lock              # 53 (stub)
    .long sys_ioctl             # 54 (stub)
    .long sys_fcntl             # 55
    .long sys_mpx               # 56 (stub)
    .long sys_setpgid           # 57
    .long sys_ulimit            # 58 (stub)
    .long sys_uname             # 59
    .long sys_umask             # 60
    .long sys_chroot            # 61
    .long sys_ustat             # 62 (stub)
    .long sys_dup2              # 63
    .long sys_getppid           # 64
    .long sys_getpgrp           # 65
    .long sys_setsid            # 66
```

> 编号与 1991 年 Linux 0.01 的 `sys_call_table` **完全一致**（stub 项同样返回 -1）。
> 完整对应关系与 `include/unistd.h` 一致；入口汇编见 §5 `system_call`（`cmpl $67, %eax; jb` 校验范围）。

**sys_call_table 在 C 中的声明：**

```c
typedef int (*fn_ptr)(void);
extern fn_ptr sys_call_table[];
```

汇编的 `.long symbol` 等价于 C 的 `&function`。跳转表通过函数地址乘以系统调用号来调用。

---

## 6. main.c — 内核主函数逐行解析

**文件：** `kernel/main.c` (48 行)
**作用：** C 语言内核入口，初始化所有子系统。

### 第 1-11 行：头文件和外部声明

```c
#include <linux/kernel.h>       // panic(), printk()
#include <linux/sched.h>        // task_struct, NR_TASKS, 调度器接口
#include <linux/mm.h>           // mem_init, get_free_page
#include <linux/fs.h>           // 文件系统接口
#include <linux/tty.h>          // TTY 接口
#include <asm/system.h>         // sti(), cli() — 本仓库无 move_to_user_mode

extern unsigned long _end;      // 链接脚本定义的符号, 标记内核映像结束
extern void shell_main(void);   // Shell 入口 (init/shell.c)，运行在内核态
extern int sys_setup(void);     // 文件系统挂载 (fs/minix.c)
```

### 第 12-48 行：main() 函数

**第 18-30 行：获取物理内存大小**

```c
    ext_kb = *((unsigned short *)0x10002);
```

从物理地址 0x10002 读取 setup.s 写入的扩展内存 KB 数。setup.s 中执行了 `int $0x15; mov %ax, (2)` —— 将值写入 DS:2，DS=0x1000，所以物理地址=0x10002。

```c
    if (ext_kb == 0)
        memory_end = 0x400000;  // 4MB 默认
    else
        memory_end = (1 << 20) + ((unsigned long)ext_kb << 10);
```

从 KB 数计算总内存：
- `ext_kb`：1MB 以上的 KB 数
- `1 << 20` = 1MB
- `ext_kb << 10` = 扩展内存字节数
- `memory_end` = 1MB + 扩展内存 = 总物理内存

```c
    if (memory_end > 0x400000)
        memory_end = 0x400000;   // 只映射了前 4MB
    memory_end &= 0xFFFFF000;    // 对齐到页边界
    if (memory_end < 0x300000)
        memory_end = 0x400000;   // 最少 4MB
```

**为什么限制 4MB？** head.s 中只设置了一个页目录条目（PDE[0]），映射了 0-4MB 的地址空间。超过 4MB 的物理内存无法访问。

**第 32-33 行：计算内核内存区域**

```c
    memory_start = (unsigned long) &_end;
    memory_start += 0x1000;     // 内核映像结束 + 1 页（留给栈）
```

**第 35-47 行：子系统初始化**

```c
    mem_init(memory_start, memory_end);     // ① 内存管理器
    buffer_init(memory_end - 0x100000);     // ② 缓冲区缓存

    tty_init();                             // ③ TTY (终端)

    if (sys_setup() < 0)                    // ④ 文件系统
        printk("Warning: no root filesystem found\n");

    sched_init();                           // ⑤ 调度器

    sti();                                  // ⑥ 开中断
    shell_main();                           // ⑦ 启动 Shell（内核态，不返回）
```

**重要：** 这里**没有** `move_to_user_mode()` / `fork()`。Shell 与内核共享 Ring 0。系统调用入口（`int 0x80`）已实现，但 Shell 多数路径直接调用 `printk` / `schedule` / `sys_exit`；用户程序则经 `execve`/`run_user_program` iret 切到 **Ring3** 运行（见 09-syscalls / 13-shell-lib）。

**各初始化函数的职责：**

| 函数 | 文件 | 功能 |
|------|------|------|
| mem_init | mm/memory.c | 设置位图管理物理页，标记内核占用页 |
| buffer_init | fs/buffer.c | 分配缓冲区缓存，连接双向链表 |
| tty_init | drivers/tty_io.c | 初始化 TTY 设备（控制台等） |
| sys_setup | fs/minix.c | 读取超级块，验证文件系统 |
| sched_init | kernel/sched.c | 初始化进程表、TSS/LDT、PIT 定时器、pwd=根 inode |
| sti | asm/system.h | 开启硬件中断 |
| shell_main | init/shell.c | 启动交互式 Shell（内核态） |

---

## 后续章节（分文件）

引导与 main 之后的源码解析已拆到 `docs/tutorial/`，请按顺序阅读：

| 章节 | 文件 | 源码 |
|------|------|------|
| §7 调度器 | [tutorial/07-sched.md](tutorial/07-sched.md) | `kernel/sched.c` |
| §8 进程 | [tutorial/08-process.md](tutorial/08-process.md) | `kernel/process.c` |
| §9 系统调用 | [tutorial/09-syscalls.md](tutorial/09-syscalls.md) | `sys.c` `vsprintf.c` `panic.c` `asm.s` |
| §10 内存 | [tutorial/10-mm.md](tutorial/10-mm.md) | `mm/*` |
| §11 文件系统 | [tutorial/11-fs.md](tutorial/11-fs.md) | `fs/*` |
| §12 驱动 | [tutorial/12-drivers.md](tutorial/12-drivers.md) | `drivers/*` |
| §13 Shell/库 | [tutorial/13-shell-lib.md](tutorial/13-shell-lib.md) | `init/` `lib/` |
| §14 头文件与构建 | [tutorial/14-headers-build.md](tutorial/14-headers-build.md) | `include/` `kernel.ld` `Makefile` |
| §15 端到端场景 | [tutorial/15-scenarios.md](tutorial/15-scenarios.md) | 跨文件数据流 |

总索引：[INDEX.md](INDEX.md) · 已知限制：[LIMITATIONS.md](LIMITATIONS.md)
