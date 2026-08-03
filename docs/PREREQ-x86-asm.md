# 前置知识一：x86 汇编语言（AT&T 语法）

本教程面向操作系统内核开发，重点讲解在本项目中用到的 x86 32 位保护模式汇编知识。

---

## 目录

1. [AT&T 语法基础](#1-att-语法基础)
2. [寄存器](#2-寄存器)
3. [数据传送指令](#3-数据传送指令)
4. [算术和逻辑指令](#4-算术和逻辑指令)
5. [控制转移指令](#5-控制转移指令)
6. [栈操作指令](#6-栈操作指令)
7. [字符串指令](#7-字符串指令)
8. [I/O 指令](#8-io-指令)
9. [标志位与控制指令](#9-标志位与控制指令)
10. [GCC 内联汇编](#10-gcc-内联汇编)
11. [寻址模式](#11-寻址模式)
12. [汇编器伪指令](#12-汇编器伪指令)
13. [实战：本项目中的典型模式](#13-实战本项目中的典型模式)

---

## 1. AT&T 语法基础

### 1.1 AT&T vs Intel 语法对比

| 特性 | Intel 语法 | AT&T 语法（本项目使用） |
|------|-----------|----------------------|
| 操作数顺序 | `目的, 源` | `源, 目的` |
| 寄存器前缀 | 无 (`eax`, `ebx`) | `%` (`%eax`, `%ebx`) |
| 立即数前缀 | 无 (`10`, `0x10`) | `$` (`$10`, `$0x10`) |
| 操作数大小 | `mov byte [addr], al` | `movb %al, addr` |
| 内存寻址 | `[eax + ebx*4 + 8]` | `8(%eax, %ebx, 4)` |
| 间接跳转 | `jmp [addr]` | `jmp *addr` |
| 远跳转 | `jmp far 0x08:0x1000` | `ljmp $0x08, $0x1000` |

### 1.2 操作数大小后缀

AT&T 语法使用指令后缀表示操作数大小：

| 后缀 | 大小 | 示例 |
|------|------|------|
| `b` | 1 字节 (byte) | `movb %al, %bl` |
| `w` | 2 字节 (word) | `movw %ax, %bx` |
| `l` | 4 字节 (long) | `movl %eax, %ebx` |

在 16 位模式下，`l` 后缀仍表示 32 位操作，但需要 `.code16` 伪指令配合操作数大小前缀 `0x66`。

### 1.3 注释

```as
# 单行注释（AT&T 风格）
/* 多行注释
   支持跨行 */
// 也支持 C++ 风格
```

---

## 2. 寄存器

### 2.1 通用寄存器与子寄存器

```
32位:   EAX          EBX          ECX          EDX
        │            │            │            │
16位:   └── AX       └── BX       └── CX       └── DX
        │   │        │   │        │   │        │   │
8位:  AH    AL     BH    BL     CH    CL     DH    DL
高8位 低8位

32位:   ESI          EDI          EBP          ESP
16位:   └── SI       └── DI       └── BP       └── SP
```

**寄存器用途约定：**

| 寄存器 | 约定用途 |
|--------|---------|
| EAX | 累加器，函数返回值 |
| EBX | 基址寄存器，系统调用第1参数 |
| ECX | 计数器，系统调用第2参数 |
| EDX | 数据寄存器，系统调用第3参数，I/O 端口地址 |
| ESI | 源变址（字符串操作的源） |
| EDI | 目标变址（字符串操作的目标） |
| EBP | 基址指针（栈帧基址） |
| ESP | 栈指针（始终指向栈顶） |

### 2.2 段寄存器

```
CS: 代码段选择子
DS: 数据段选择子
SS: 栈段选择子
ES: 附加段
FS: 附加段（Linux 用于线程本地存储和用户空间访问）
GS: 附加段
```

每个 16 位，存储保护模式下的段选择子。

**本项目中选择子定义：**

| 选择子 | 值 | 含义 |
|--------|-----|------|
| KERNEL_CS | 0x08 | GDT[1]，内核代码段，DPL=0（见 `include/linux/head.h`） |
| KERNEL_DS | 0x10 | GDT[2]，内核数据段，DPL=0 |
| USER_CS | 0x1B | GDT[3]\|RPL3，用户代码段（本仓库 GDT 布局） |
| USER_DS | 0x23 | GDT[4]\|RPL3，用户数据段；`system_call` 中 FS 用此选择子 |

### 2.3 控制寄存器

```
CR0: 系统控制标志
  Bit 0  (PE)  = 保护模式使能
  Bit 16 (WP)  = 写保护
  Bit 31 (PG)  = 分页使能

CR2: 页错误线性地址（只读，CPU 自动写入）

CR3: 页目录基址寄存器（PDBR）
  Bit 31-12 = 页目录物理地址[31:12]
  Bit 11-0  = 保留（应为 0）

CR4: 架构扩展标志
```

**本项目中的关键操作：**

```as
movl %eax, %cr0       # 写入 CR0（切换保护模式、启用分页）
movl %cr0, %eax       # 读取 CR0
movl %eax, %cr3       # 设置页目录基址
movl %cr3, %eax       # 读取 CR3（用于刷新 TLB）
movl %cr2, %edx       # 读取页错误地址
```

### 2.4 其他关键寄存器

```
EIP:     指令指针（不可直接访问，通过 JMP/CALL/RET 修改）
EFLAGS:  标志寄存器
  Bit 0  (CF) = 进位标志
  Bit 2  (PF) = 奇偶标志
  Bit 6  (ZF) = 零标志
  Bit 7  (SF) = 符号标志
  Bit 9  (IF) = 中断使能
  Bit 10 (DF) = 方向标志（0=正向, 1=反向）
  Bit 11 (OF) = 溢出标志

GDTR:    全局描述符表寄存器（48位: 32位基址 + 16位界限）
IDTR:    中断描述符表寄存器（48位: 32位基址 + 16位界限）
TR:      任务寄存器（指向当前 TSS 的段选择子）
LDTR:    局部描述符表寄存器（指向当前 LDT 的段选择子）
```

---

## 3. 数据传送指令

### 3.1 MOV — 基本数据传送

```as
movl $0x10, %eax        # 立即数 → 寄存器
movl %eax, %ebx         # 寄存器 → 寄存器
movl %eax, (%ebx)       # 寄存器 → 内存（EBX 指向的地址）
movl (%eax), %ebx       # 内存 → 寄存器
movb $0x41, (%edi)      # 立即数 → 内存（字节）
movw %ax, %ds           # 寄存器 → 段寄存器
movw %ds, %ax           # 段寄存器 → 寄存器
```

### 3.2 MOVZ / MOVS — 零扩展和符号扩展

```as
movzbl %al, %ebx        # 将 AL（8位）零扩展为 EBX（32位）
movsbl %al, %ebx        # 将 AL（8位）符号扩展为 EBX（32位）
movzwl %ax, %ebx        # 将 AX（16位）零扩展为 EBX（32位）
movswl %ax, %ebx        # 将 AX（16位）符号扩展为 EBX（32位）
```

在 C 语言中处理 `unsigned char` 到 `unsigned int` 转换时，编译器会自动生成零扩展指令。

### 3.3 XCHG — 交换

```as
xchgl %eax, %ebx        # 交换 EAX 和 EBX
xchgl %eax, (%esp)      # 交换 EAX 和栈顶（常用于中断处理）
```

本项目页错误处理中的典型用法：

```as
_page_fault:
    xchgl %eax, (%esp)   # 保存 EAX 到栈，同时取出错误码到 EAX
```

### 3.4 LEA — 加载有效地址

```as
leal 8(%eax, %ebx, 4), %ecx   # ECX = EAX + EBX*4 + 8（不访问内存）
leal (%eax), %ebx             # EBX = EAX（相当于 mov，但语义更清晰）
```

`LEA` 只计算地址，不访问内存。常用于：
- 算术运算（利用地址计算完成加法/乘法）
- 获取局部变量地址

---

## 4. 算术和逻辑指令

### 4.1 基本算术

```as
addl $8, %eax           # EAX = EAX + 8
subl $4, %esp           # ESP = ESP - 4（分配栈空间）
incl %ecx               # ECX = ECX + 1
decl %ecx               # ECX = ECX - 1
negl %eax               # EAX = -EAX（取负）
mull %ebx               # EDX:EAX = EAX * EBX（无符号乘法）
imull %ebx              # EDX:EAX = EAX * EBX（有符号乘法）
divl %ebx               # EAX = EDX:EAX / EBX, EDX = 余数（无符号）
idivl %ebx              # 同上（有符号）
```

### 4.2 位操作

```as
andl $0xFFFFF000, %eax  # EAX = EAX & 0xFFFFF000（常用于页对齐）
orl  $0x007, %eax       # EAX = EAX | 7（设置页属性位）
xorl %eax, %eax          # EAX = 0（清空寄存器，比 movl $0 更短）
notl %eax               # EAX = ~EAX
shll $12, %eax          # EAX = EAX << 12（左移）
shrl $12, %eax          # EAX = EAX >> 12（逻辑右移）
sarl $1, %eax           # EAX = EAX >> 1（算术右移，保留符号）
rorl $16, %eax          # 循环右移 16 位（用于设置描述符）
roll $16, %eax          # 循环左移
```

### 4.3 测试和比较

```as
testl %eax, %eax        # 测试 EAX（做 AND 但不保存结果，只设置 ZF/SF）
cmpl $0, %eax           # 比较 EAX 和 0（做减法但不保存结果）
cmpl %ebx, %eax         # 比较 EAX - EBX（设置标志位）
cmpb $0xAA, (bx)        # 比较内存字节
```

**条件跳转指令（紧跟 TEST/CMP）：**

```as
je   label    # 相等 / ZF=1    (Jump if Equal)
jne  label    # 不等 / ZF=0    (Jump if Not Equal)
jz   label    # 零 / ZF=1     (Jump if Zero)
jnz  label    # 非零 / ZF=0   (Jump if Not Zero)
jl   label    # 小于（有符号）  (Jump if Less)
jle  label    # 小于等于       (Jump if Less or Equal)
jg   label    # 大于（有符号）  (Jump if Greater)
jge  label    # 大于等于       (Jump if Greater or Equal)
jb   label    # 低于（无符号）  (Jump if Below)
jbe  label    # 低于等于       (Jump if Below or Equal)
ja   label    # 高于（无符号）  (Jump if Above)
jae  label    # 高于等于       (Jump if Above or Equal)
jc   label    # 进位 / CF=1   (Jump if Carry)
jnc  label    # 无进位 / CF=0  (Jump if Not Carry)
js   label    # 负号 / SF=1   (Jump if Sign)
jns  label    # 正号 / SF=0   (Jump if Not Sign)
```

---

## 5. 控制转移指令

### 5.1 JMP — 无条件跳转

```as
jmp label               # 直接短跳转（8位偏移）
jmp *%eax               # 间接跳转（跳转到 EAX 指向的地址）
jmp *sys_call_table(,%eax,4)  # 跳转表（系统调用）
ljmp $0x08, $0x10000    # 远跳转（同时设置 CS 和 EIP）
```

### 5.2 CALL / RET — 函数调用

```as
call func               # 压入返回地址，跳转到 func
call *%eax              # 间接调用（函数指针）
ret                     # 弹出返回地址，跳转回去
ret $8                  # 返回后弹出 8 字节参数（stdcall 约定）
lret                    # 远返回（弹出 CS 和 EIP）
```

**CALL 的等效操作：**

```as
# call func 等价于：
pushl $next_instruction
jmp func
next_instruction:
```

### 5.3 条件跳转和循环

```as
# 典型的 do-while 循环（计数器递减）
    movl $10, %ecx
1:  # 循环体
    decl %ecx
    jnz 1b                 # ECX != 0 时跳回

# 带条件判断的循环
    cmpl $0, %eax
    jle exit_loop          # EAX <= 0 时退出
    # 循环体
    jmp loop_start
exit_loop:
```

**`1b` 和 `1f` 的含义：**
- `1b` = 向后搜索标签 `1`（backward）
- `1f` = 向前搜索标签 `1`（forward）
- 数字标签是局部标签，可重复使用

### 5.4 INT / IRET — 中断

```as
int $0x80               # 软件中断（系统调用）
iret                    # 中断返回（恢复 EIP, CS, EFLAGS, [ESP, SS]）
```

**IRET 的详细行为（特权级切换时）：**

```
1. 弹出 EIP → 加载指令指针
2. 弹出 CS  → 加载代码段选择子
3. 弹出 EFLAGS → 加载标志寄存器
4. 如果 CS 的 RPL > 当前 CPL（从高特权级返回低特权级）：
   a. 弹出 ESP → 加载栈指针
   b. 弹出 SS  → 加载栈段选择子
```

---

## 6. 栈操作指令

### 6.1 PUSH / POP

```as
pushl %eax              # ESP -= 4, [ESP] = EAX
pushl $42               # ESP -= 4, [ESP] = 42
pushw %ax               # ESP -= 2, [ESP] = AX
popl %eax               # EAX = [ESP], ESP += 4
pushfl                  # ESP -= 4, [ESP] = EFLAGS
popfl                   # EFLAGS = [ESP], ESP += 4
pusha                   # 保存所有 16 位通用寄存器（AX, CX, DX, BX, SP, BP, SI, DI）
popa                    # 恢复所有 16 位通用寄存器
pushal                  # 保存所有 32 位通用寄存器（EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI）
popal                   # 恢复所有 32 位通用寄存器
```

**栈增长方向：** x86 栈向下增长（高地址→低地址）

```
初始状态 ESP = 0x9FFF0:
  0x9FFF0:  ← ESP（初始）
  0x9FFEC:
  0x9FFE8:
  ...

pushl $42 后：
  0x9FFF0:  ← 旧 ESP
  0x9FFEC: [42] ← ESP（新）
  0x9FFE8:
```

### 6.2 ENTER / LEAVE — 栈帧

```as
# 标准函数序言
pushl %ebp              # 保存调用者的帧指针
movl %esp, %ebp          # 设置当前函数的帧指针
subl $16, %esp           # 分配 16 字节局部变量空间

# 标准函数尾声
movl %ebp, %esp          # 恢复栈指针
popl %ebp                # 恢复调用者的帧指针
ret                      # 返回
```

---

## 7. 字符串指令

### 7.1 核心字符串指令

```as
# 前缀：REP（重复 CX/ECX 次）
#       REPE/REPZ（重复直到 ZF=0）
#       REPNE/REPNZ（重复直到 ZF=1）
# 方向：CLD（正向，DF=0，ESI/EDI 递增）
#       STD（反向，DF=1，ESI/EDI 递减）

movsb    # 复制 1 字节 [DS:ESI] → [ES:EDI], ESI++, EDI++
movsw    # 复制 2 字节
movsl    # 复制 4 字节
stosb    # 将 AL 写入 [ES:EDI], EDI++
stosw    # 将 AX 写入 [ES:EDI], EDI+=2
stosl    # 将 EAX 写入 [ES:EDI], EDI+=4
lodsb    # 将 [DS:ESI] 读到 AL, ESI++
lodsw    # 将 [DS:ESI] 读到 AX, ESI+=2
lodsl    # 将 [DS:ESI] 读到 EAX, ESI+=4
scasb    # 比较 AL 和 [ES:EDI], EDI++
cmpsb    # 比较 [DS:ESI] 和 [ES:EDI], ESI++, EDI++
```

### 7.2 本项目中的典型用法

**内存复制（boot.s 中重定位引导扇区）：**

```as
movw $BOOTSEG, %ax       # DS = 源段
movw %ax, %ds
movw $INITSEG, %ax       # ES = 目标段
movw %ax, %es
movw $256, %cx           # 复制 256 个字 = 512 字节
subw %si, %si            # SI = 0（源偏移）
subw %di, %di            # DI = 0（目标偏移）
cld                      # 正向
rep
movsw                    # 重复执行 MOVSW 256 次
```

**内存清零（head.s 中清空页表）：**

```as
movl $1024*5, %ecx       # 清空 5 页 = 5120 个双字
xorl %eax, %eax          # EAX = 0
xorl %edi, %edi           # EDI = 0（目标偏移）
cld                      # 正向
rep; stosl               # 重复执行 STOSL 5120 次
```

**反向填充（head.s 中填充页表条目）：**

```as
movl $pg3 + 4092, %edi   # EDI 指向最后一页的最后一个双字
movl $0xFFF007, %eax      # 最后一个 PTE 的值
std                      # 设置方向标志（反向）
1:  stosl                # 写入 PTE, EDI -= 4
    subl $0x1000, %eax   # 递减物理地址
    jge 1b               # EAX >= 0 时继续
```

---

## 8. I/O 指令

### 8.1 端口 I/O

```as
# IN — 从端口读取
inb $0x60, %al           # 从端口 0x60 读取 1 字节到 AL
inb %dx, %al             # 从 DX 指定的端口读取
inw $0x1F0, %ax          # 从端口 0x1F0 读取 2 字节到 AX
inl $0x1F0, %eax         # 从端口 0x1F0 读取 4 字节到 EAX

# OUT — 写入端口
outb %al, $0x20          # 将 AL 写入端口 0x20
outb %al, %dx            # 将 AL 写入 DX 指定的端口
outw %ax, $0x1F0         # 将 AX 写入端口 0x1F0

# INS — 从端口读取字符串
insb                     # 从 DX 端口读 1 字节到 [ES:EDI]
insw                     # 从 DX 端口读 2 字节
insl                     # 从 DX 端口读 4 字节

# OUTS — 写入字符串到端口
outsb                    # 将 [DS:ESI] 写入 DX 端口
outsw                    # 写入 2 字节
outsl                    # 写入 4 字节
```

**I/O 端口地址限制：**
- 端口 0x00-0xFF：可以用立即数或 DX 指定
- 端口 0x100-0xFFFF：只能用 DX 指定

**本项目中的典型用法：**

```as
# 发送 EOI 给主 PIC
movb $0x20, %al
outb %al, $0x20

# 读取键盘扫描码
inb $0x60, %al

# 读取 IDE 数据（256 个字 = 一个扇区）
movw $0x1F0, %dx
movw $256, %cx
rep; insw
```

---

## 9. 标志位与控制指令

### 9.1 STI / CLI — 中断控制

```as
sti    # 设置 IF=1（开中断）
cli    # 清除 IF=0（关中断）
```

**使用场景：**
- 进入保护模式前：`cli`（此时没有 IDT）
- 设置完 IDT 后：`sti`（允许硬件中断）
- 关键区保护：`cli ... sti`

### 9.2 CLD / STD — 方向控制

```as
cld    # 设置 DF=0（正向，ESI/EDI 自动递增）
std    # 设置 DF=1（反向，ESI/EDI 自动递减）
```

### 9.3 其他控制指令

```as
nop         # 空操作（0x90）
hlt         # 暂停 CPU 直到下一个中断
lock        # 总线锁前缀（用于原子操作）
cpuid       # 获取 CPU 信息
rdtsc       # 读取时间戳计数器
```

---

## 10. GCC 内联汇编

本项目大量使用 GCC 内联汇编来访问特殊寄存器和 I/O 端口。

### 10.1 基本语法

```c
__asm__ __volatile__ (
    "汇编指令1\n\t"
    "汇编指令2\n\t"
    : 输出操作数列表
    : 输入操作数列表
    : 被破坏的寄存器列表
);
```

- `__asm__`：内联汇编关键字（`asm` 的别名，避免宏冲突）
- `__volatile__`：禁止编译器优化（不重新排序、不删除）
- `\n\t`：每条指令独占一行，生成整洁的反汇编代码

### 10.2 操作数约束

| 约束 | 含义 | 示例 |
|------|------|------|
| `"r"` | 任意通用寄存器 | `"r"(value)` |
| `"a"` | EAX 寄存器 | `"a"(port)` |
| `"b"` | EBX 寄存器 | `"b"(fd)` |
| `"c"` | ECX 寄存器 | `"c"(buf)` |
| `"d"` | EDX 寄存器 | `"d"(count)` |
| `"S"` | ESI 寄存器 | `"S"(src)` |
| `"D"` | EDI 寄存器 | `"D"(dst)` |
| `"m"` | 内存操作数 | `"m"(variable)` |
| `"i"` | 立即数 | `"i"(10)` |
| `"g"` | 任意通用操作数 | `"g"(value)` |
| `"0"` | 与第 0 个操作数相同 | `"0"(x)` |
| `"+r"` | 读写操作数 | `"+r"(counter)` |
| `"=&r"` | 早期破坏（不共享输入寄存器） | `"=&r"(result)` |

### 10.3 典型示例

**读取 EFLAGS：**

```c
unsigned long eflags;
__asm__ volatile("pushfl; popl %0" : "=r"(eflags));
// eflags 现在包含 EFLAGS 的值
```

`%0` 指第一个操作数（从 0 开始编号），对应输出列表中的 `eflags`。

**读取 CR3：**

```c
#define read_cr3() ({ \
    unsigned long __cr3; \
    __asm__("movl %%cr3, %0" : "=r"(__cr3)); \
    __cr3; \
})
```

`%%cr3` 中双百分号是因为 GCC 在内联汇编字符串中处理 `%` 为操作数占位符。要表示寄存器名中的 `%`，需要写 `%%`。

**写入 I/O 端口：**

```c
#define outb(value, port) \
__asm__("outb %%al, %%dx" : : "a"((unsigned char)(value)), "d"((unsigned short)(port)))
```

约束 `"a"` 将 value 放入 AL，`"d"` 将 port 放入 DX。因为只有输出，输出列表为空。

**无副作用的读 I/O 端口：**

```c
#define inb(port) ({ \
    unsigned char _v; \
    __asm__ volatile("inb %%dx, %%al" : "=a"(_v) : "d"((unsigned short)(port))); \
    _v; \
})
```

`volatile` 确保每次调用 `inb` 都生成真正的 IN 指令，不会被优化掉。

**带内存操作数的 TSS 描述符设置：**

```c
#define set_tss_desc(n, addr) \
__asm__("movw $104, %1\n\t"      /* 限长 0-1 */ \
        "movw %%ax, %2\n\t"      /* 基址 0-1 */ \
        "rorl $16, %%eax\n\t"    /* 旋转 EAX */ \
        "movb %%al, %3\n\t"      /* 基址 2 */ \
        "movb $0x89, %4\n\t"     /* P=1,DPL=0,Type=TSS */ \
        "movb $0x00, %5\n\t"     /* 限长 2-3 + G */ \
        "movb %%ah, %6\n\t"      /* 基址 3 */ \
        "rorl $16, %%eax"         /* 恢复 EAX */ \
        ::"a"(addr), "m"(*(n)), "m"(*(n+2)), "m"(*(n+4)), \
          "m"(*(n+5)), "m"(*(n+6)), "m"(*(n+7)))
```

约束 `"m"(*(n+偏移))` 将 GDT 描述符的对应字节作为直接内存操作数，编译器会计算正确的地址并生成如 `movw %ax, 64(%ebx)` 这样的指令。

### 10.4 匹凡表达式（Statement Expression）

```c
#define MAP_NR(addr) ({ \
    unsigned long __a = (unsigned long)(addr); \
    (__a >= LOW_MEM) ? ((__a - LOW_MEM) >> 12) : -1; \
})
```

`({ ... })` 是 GCC 扩展，允许在表达式中包含多条语句，最后一条语句的值作为整个表达式的值。这使得宏可以包含临时变量而仍然是表达式。

---

## 11. 寻址模式

### 11.1 实模式寻址

```as
# 直接寻址
movw %ax, (0x1000)       # DS:[0x1000] = AX

# 寄存器间接寻址
movw %ax, (%bx)          # DS:[BX] = AX

# 基址+变址
movw %ax, (%bx, %si)     # DS:[BX+SI] = AX
```

**16 位寻址的寄存器限制：** 必须是 `BX, BP, SI, DI` 的组合。

### 11.2 保护模式寻址（32 位）

```as
# 寄存器间接
movl %eax, (%ebx)                     # DS:[EBX] = EAX

# 基址 + 偏移
movl %eax, 8(%ebx)                    # DS:[EBX+8] = EAX

# 基址 + 变址
movl %eax, (%ebx, %esi)               # DS:[EBX+ESI] = EAX

# 基址 + 变址 + 偏移
movl %eax, 16(%ebx, %esi)             # DS:[EBX+ESI+16] = EAX

# 基址 + 变址*比例
movl %eax, (%ebx, %esi, 2)            # DS:[EBX+ESI*2] = EAX

# 基址 + 变址*比例 + 偏移
movl %eax, 8(%ebx, %esi, 4)           # DS:[EBX+ESI*4+8] = EAX

# 纯偏移（直接寻址）
movl %eax, variable                   # DS:[variable 的地址] = EAX
```

**比例因子：** 1, 2, 4, 8（对应 1, 2, 4, 8 字节数据类型）。

**32 位寻址的寄存器自由度：** 任意通用寄存器都可以用作基址或变址（除了 ESP 不能用作变址）。

### 11.3 项目中的关键寻址示例

**系统调用跳转表：**

```as
call *sys_call_table(,%eax,4)
# 等价于: call *(sys_call_table + EAX * 4)
# sys_call_table 是函数指针数组，每个指针 4 字节
# EAX 是系统调用号（0-9）
```

---

## 12. 汇编器伪指令

### 12.1 内存分配

```as
.section .text             # 切换到代码段
.section .data             # 切换到已初始化数据段
.section .bss              # 切换到未初始化数据段

.align 4                   # 对齐到 4 字节边界
.align 8                   # 对齐到 8 字节边界
.align 4096                # 对齐到页边界

.org 510                   # 将位置计数器设为 510（Boot 签名用）
```

### 12.2 数据定义

```as
.byte  0xAA               # 定义 1 个字节
.word  0xAA55             # 定义 1 个字（2 字节）
.long  0x12345678         # 定义 1 个双字（4 字节）
.quad  0x123456789ABCDEF0 # 定义 1 个四字（8 字节）

.ascii "hello"            # 定义字符串（无结尾 '\0'）
.asciz "hello"            # 定义字符串（有结尾 '\0'）

.space 512, 0             # 分配 512 字节，填充 0

.fill 10, 4, 0xDEADBEEF   # 填充 10 个双字
```

### 12.3 符号定义

```as
.equ  MAX_TASKS, 64       # 等同于 #define MAX_TASKS 64

.set  SYMBOL, 0x1000       # 定义可重定义的符号

.globl startup_32         # 声明全局符号（可被其他文件引用）
.globl _idt               # 导出 IDT 表给 C 代码
.globl _gdt               # 导出 GDT 表

.extern printk            # 声明外部符号
```

### 12.4 宏

```as
.macro push_regs
    pushl %eax
    pushl %ecx
    pushl %edx
.endm

# 调用宏
push_regs
```

---

## 13. 实战：本项目中的典型模式

### 13.1 中断处理程序模板

```as
_timer_interrupt:
    pusha                    # 保存所有 32 位通用寄存器
    pushl %ds                # 保存段寄存器
    pushl %es
    pushl %fs

    movl $0x10, %eax        # 切换到内核数据段
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs

    call do_timer            # 调用 C 处理函数

    movb $0x20, %al         # 向 PIC 发送 EOI
    outb %al, $0x20

    popl %fs                 # 恢复段寄存器
    popl %es
    popl %ds
    popa                     # 恢复通用寄存器
    iret                     # 中断返回
```

**为什么要保存和恢复这些寄存器？**
- `pusha/popa` 保存/恢复所有通用寄存器（EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI）
- 段寄存器必须单独保存/恢复：
  - `pusha` 不保存段寄存器
  - 中断可能在 DS≠内核段时触发（本仓库 Shell 实际多在 Ring0）
  - C 处理函数需要访问内核数据（DS=0x10）
  - 返回时必须恢复原始段寄存器

### 13.2 地址对齐技巧

```as
# 将地址对齐到页边界（4KB）
andl $0xFFFFF000, %eax    # 清除低 12 位

# 从地址中提取页框号
shrl $12, %eax             # 地址 >> 12

# 从页框号构造地址
shll $12, %eax             # 页框号 << 12

# 检查地址是否页对齐
testl $0xFFF, %eax          # 如果 ZF=1，则已对齐
jnz not_aligned
```

### 13.3 描述符设置的模式

```as
# 设置 IDT 描述符的通用模式
leal handler_addr, %edx    # 获取处理程序地址
movl $0x00080000, %eax     # 高 16 位放段选择子 (0x0008)
movw %dx, %ax              # 低 16 位放处理程序偏移
movw $0x8E00, %dx          # P=1, DPL=0, 32位中断门

# 描述符写入 IDT
movl %eax, (%edi)          # 低 4 字节：[偏移15:0][选择子]
movl %edx, 4(%edi)         # 高 4 字节：[偏移31:16][标志]
```

### 13.4 16 位实模式代码的特殊处理

在 16 位代码中需要使用操作数大小前缀（0x66）和地址大小前缀（0x67）才能访问 32 位寄存器和地址：

```as
.code16
    .byte 0x66              # 操作数大小前缀（使用 32 位寄存器）
    lgdt gdt_48             # 等同于 32 位的 lgdt（加载 6 字节描述符）

    .byte 0x66, 0x67       # 两个前缀
    movl %cr0, %eax         # 在 16 位模式下读取 32 位 CR0
```

或者使用 GNU 汇编器的 `.code16gcc` 模式，它会自动插入大小前缀：

```as
.code16gcc
    movl %cr0, %eax         # 自动插入 0x66 前缀
    lgdt gdt_48             # 自动插入 0x66 前缀
```

### 13.5 位置无关的自重定位

boot.s 需要将自己从 0x7C00 移动到 0x90000。关键技巧是使用段寄存器操作：

```as
_start:
    movw $0x07C0, %ax       # 源段 = 0x07C0（物理地址 0x7C00）
    movw %ax, %ds
    movw $0x9000, %ax       # 目标段 = 0x9000（物理地址 0x90000）
    movw %ax, %es
    movw $256, %cx          # 512 字节 = 256 字
    xorw %si, %si            # 源偏移 = 0
    xorw %di, %di            # 目标偏移 = 0
    cld
    rep; movsw              # 复制

    # 跳转到新位置继续执行
    ljmp $0x9000, $go       # CS = 0x9000, IP = go 的偏移
go:
    movw %cs, %ax           # 重设数据段
    movw %ax, %ds
```

**为什么不能直接使用物理地址计算？**
在实模式下，所有内存寻址都使用 `段*16+偏移`。要访问超过 64KB 的数据，必须修改段寄存器。

---

## 附录：指令速查表

### 常用指令编码对照

| 指令 | 操作 | 受影响标志 |
|------|------|-----------|
| MOV | 传送 | 无 |
| PUSH | 压栈 | 无 |
| POP | 出栈 | 无 |
| XCHG | 交换 | 无 |
| ADD | 加法 | OF, SF, ZF, AF, PF, CF |
| SUB | 减法 | OF, SF, ZF, AF, PF, CF |
| INC | 加 1 | OF, SF, ZF, AF, PF（不改变 CF） |
| DEC | 减 1 | OF, SF, ZF, AF, PF（不改变 CF） |
| MUL | 无符号乘法 | OF, CF（其他未定义） |
| AND | 位与 | OF=0, CF=0, SF, ZF, PF |
| OR | 位或 | OF=0, CF=0, SF, ZF, PF |
| XOR | 位异或 | OF=0, CF=0, SF, ZF, PF |
| NOT | 位非 | 无 |
| SHL/SAL | 左移 | CF（最后移出的位）, OF, SF, ZF, PF |
| SHR | 逻辑右移 | CF, OF, SF, ZF, PF |
| SAR | 算术右移 | CF, OF, SF, ZF, PF |
| ROL | 循环左移 | CF, OF |
| ROR | 循环右移 | CF, OF |
| TEST | 测试（AND 不保存） | SF, ZF, PF |
| CMP | 比较（SUB 不保存） | OF, SF, ZF, AF, PF, CF |
| STI | 开中断 | IF=1 |
| CLI | 关中断 | IF=0 |
| CLD | 清方向标志 | DF=0 |
| STD | 置方向标志 | DF=1 |
| HLT | 停机（等待中断） | 无 |
| NOP | 空操作 | 无 |
| IRET | 中断返回 | 全部（从栈恢复） |
