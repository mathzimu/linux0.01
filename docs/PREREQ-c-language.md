# 前置知识二：C 语言内核编程技术

本教程讲解在操作系统内核开发中用到的 C 语言关键技术和惯用法。

---

## 目录

1. [指针与强制类型转换](#1-指针与强制类型转换)
2. [结构体与位域](#2-结构体与位域)
3. [函数指针与回调](#3-函数指针与回调)
4. [预处理器宏技巧](#4-预处理器宏技巧)
5. [GCC 扩展特性](#5-gcc-扩展特性)
6. [内联汇编](#6-内联汇编)
7. [链接器符号的 C 访问](#7-链接器符号的-c-访问)
8. [内核编程约定](#8-内核编程约定)
9. [二进制数据操作](#9-二进制数据操作)
10. [内存模型与对齐](#10-内存模型与对齐)

---

## 1. 指针与强制类型转换

### 1.1 绝对地址访问（MMIO）

VGA 显存映射在物理地址 0xB8000，在平坦内存模型中可直接用指针访问：

```c
// 直接读写显存
static unsigned short *video_mem = (unsigned short *)0xB8000;

// 在指定位置写字符
video_mem[y * 80 + x] = 0x0700 | 'A';  // 黑底白字 'A'
```

**为什么不用 volatile？**
在这个单线程、无优化屏障的内核中，编译器不会对 MMIO 访问做激进优化，所以省略 volatile 是安全的。但在现代内核中，MMIO 必须使用 volatile。

### 1.2 强制类型转换模式

```c
// 将整数转为指针
struct task_struct *p = (struct task_struct *)get_free_page();

// 将结构体当作字节数组操作
memset((char *)addr, 0, PAGE_SIZE);

// 提取页框号（地址对齐到 4KB）
unsigned long page_addr = (unsigned long)pg_table & 0xFFFFF000;

// 将缓冲区数据重解释为另一个类型
struct d_inode *di = (struct d_inode *)bh->b_data + offset;

// 位图操作：将 char * 用于位索引
unsigned char *bitmap = (unsigned char *)bh->b_data;
bitmap[bit / 8] |= (1 << (bit % 8));
```

### 1.3 空指针检查模式

```c
// 防御性编程：检查分配是否成功
struct task_struct *p = (struct task_struct *)get_free_page();
if (!p) return -1;

// 检查指针参数
if (!inode) return;
if (!inode->i_dev) return;

// 检查文件描述符有效性
if (fd >= NR_OPEN || !current->filp[fd]) return -1;
```

### 1.4 指针算术与数组

```c
// 通过指针访问 GDT 描述符数组
struct desc_struct *p_desc;
p_desc = (struct desc_struct *)(&_gdt) + tss_entry;
// 等价于：&((struct desc_struct *)(&_gdt))[tss_entry]

// 遍历进程表
struct task_struct **p;
for (p = &task[NR_TASKS - 1]; p >= &task[0]; p--) {
    if (*p == NULL) continue;
    // 处理 *p（即 task[i]）
}

// 指针之差得索引
int next = (int)(p - task);  // 获取当前指针在 task 数组中的索引
```

---

## 2. 结构体与位域

### 2.1 硬件映射结构体

内核中大量使用结构体来描述硬件数据结构，要求结构体内存布局与硬件完全一致。

```c
// MINIX 磁盘目录项（16 字节，与磁盘格式完全对应）
struct minix_dir_entry {
    unsigned short inode;    // 偏移 0: inode 号（2 字节）
    char name[14];           // 偏移 2: 文件名（14 字节）
};                           // 总共 16 字节
```

**内存布局验证：**

```
偏移 大小 字段
0x00   2   inode     (unsigned short，小端序)
0x02  14   name[14]  (固定 14 字节)
─────────────────
总共 16 字节
```

### 2.2 进程控制块

```c
struct task_struct {
    long state;               // -1=不可运行, 0=可运行, >0=已停止
    long counter;             // 时间片计数器
    long priority;            // 优先级（静态）
    long signal;              // 信号位图
    struct tss_struct tss;    // 硬件 TSS（104 字节）
    struct file *filp[NR_OPEN];// 打开文件表（64 个指针）
    int uid;                  // 用户 ID
    int pid;                  // 进程 ID
    int pgrp;                 // 进程组
    int session;              // 会话
    int leader;               // 会话领导
    long cutime, cstime;      // 子进程时间
    long start_time;          // 启动时间
    unsigned long start_code, end_code, end_data, brk, start_stack;
    struct desc_struct ldt[3];// 局部描述符表
};
```

### 2.3 带有链表的结构体

```c
struct buffer_head {
    char *b_data;                        // 数据指针
    unsigned long b_blocknr;             // 块号
    unsigned short b_dev;                // 设备号
    unsigned char b_uptodate;            // 数据有效标志
    unsigned char b_dirt;                // 脏标志
    unsigned char b_count;               // 引用计数
    unsigned char b_lock;                // 锁定标志
    struct task_struct *b_wait;          // 等待队列
    struct buffer_head *b_prev;          // 哈希链前驱
    struct buffer_head *b_next;          // 哈希链后继
    struct buffer_head *b_prev_free;     // 空闲链前驱
    struct buffer_head *b_next_free;     // 空闲链后继
};
```

这个结构体同时存在于两个双向链表中（哈希链表和空闲链表），典型的嵌入式链表设计。

### 2.4 静态初始化

```c
// 使用指定初始化器
static struct task_struct init_task = {
    0,                                    // state
    15,                                   // counter
    15,                                   // priority
    0,                                    // signal
    {0},                                  // tss (全零)
    {NULL,},                              // filp (全 NULL)
    0,                                    // uid
    0,                                    // pid
    0, 0, 0,                              // pgrp, session, leader
    0,0,0,0,                              // time 字段
    0,0,0,0,                              // code/data 范围
    0,0,                                  // brk, stack
    {{0,0},{0,0},{0,0}}                   // ldt
};
```

按位置初始化，适用于简单结构。注意 `{NULL,}` 让所有未指定的元素都初始化为 NULL。

### 2.5 紧凑（packed）结构体

```c
struct minix_superblock {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
} __attribute__((packed));
```

`__attribute__((packed))` 告诉编译器不要插入对齐填充，确保结构体布局与磁盘格式完全一致。不加 packed 时，编译器可能在 `s_log_zone_size` 后面插入 2 字节填充使 `s_max_size` 按 4 字节对齐。

---

## 3. 函数指针与回调

### 3.1 系统调用表

```c
// 函数指针类型定义
typedef int (*fn_ptr)(void);

// 系统调用表：通过编号索引调用
fn_ptr sys_call_table[] = {
    sys_setup,   // 0: 文件系统挂载
    sys_exit,    // 1: 进程退出
    sys_fork,    // 2: 创建子进程
    sys_read,    // 3: 读取文件
    sys_write,   // 4: 写入文件
    sys_open,    // 5: 打开文件
    sys_close,   // 6: 关闭文件
    sys_getpid,  // 7: 获取进程 PID
    sys_pause,   // 8: 暂停进程
    sys_time,    // 9: 获取时间
};
```

汇编中通过 `call *sys_call_table(,%eax,4)` 调用，计算方式：`table_base + eax * 4`。

### 3.2 TTY 多态写函数

```c
struct tty_struct {
    // ...
    void (*write)(struct tty_struct *);  // 函数指针：多态
};

// 控制台的写函数
void con_write(struct tty_struct *tty) { /* ... */ }

// 初始化
tty_table[0].write = con_write;

// 使用
tty->write(tty);  // 实际调用 con_write()
```

通过函数指针实现多态，不同 TTY 设备（控制台、串口）可以有不同的写函数。

### 3.3 Shell 命令分发表

```c
static struct {
    char *name;
    void (*func)(void);
} cmd_table[] = {
    {"echo",  cmd_echo},
    {"help",  cmd_help},
    {"ps",    cmd_ps},
    {"clear", cmd_clear},
    {"exit",  cmd_exit},
    {NULL, NULL}
};

// 命令查找与分派
for (int i = 0; cmd_table[i].name; i++) {
    if (!strcmp(cmd_table[i].name, cmd_name)) {
        cmd_table[i].func();
        return;
    }
}
```

---

## 4. 预处理器宏技巧

### 4.1 内联函数宏

```c
#define outb(value, port) \
__asm__ ("outb %%al, %%dx" : : "a"((unsigned char)(value)), "d"((unsigned short)(port)))

#define sti() __asm__("sti")
#define cli() __asm__("cli")
```

用宏封装单条汇编指令，提供函数式接口。

### 4.2 多语句宏

使用 `do { ... } while(0)` 模式确保宏作为单个语句使用。本仓库典型例子是 `set_tss_desc` / `switch_to`（见 `include/asm/system.h`）：

```c
#define set_tss_desc(n, addr) \
do { \
    unsigned char *__cp = (unsigned char *)(n); \
    unsigned long __addr = (unsigned long)(addr); \
    __cp[0] = 0x67; /* limit 低字节: TSS 限长 104-1=0x67 */ \
    /* ... 填入基址与 Type=0x89 (可用 32 位 TSS) ... */ \
} while(0)
```

**注意：** 本仓库 **没有** `move_to_user_mode()`。`main()` 直接调用 `shell_main()`，Shell 在内核态运行。若文档或 HLD 提到该宏，那是设计目标而非当前实现。

### 4.3 连接器符号宏

```c
#define set_tss_desc(n, addr) \
__asm__("movw $104, %1\n\t" \
        "movw %%ax, %2\n\t" \
        "rorl $16, %%eax\n\t" \
        "movb %%al, %3\n\t" \
        "movb $0x89, %4\n\t" \
        "movb $0x00, %5\n\t" \
        "movb %%ah, %6\n\t" \
        "rorl $16, %%eax" \
        ::"a"(addr), "m"(*(n)), "m"(*(n+2)), "m"(*(n+4)), \
          "m"(*(n+5)), "m"(*(n+6)), "m"(*(n+7)))
```

使用 `"m"` 约束让编译器处理内存地址计算，避免手动指针运算。

### 4.4 带局部变量的宏（语句表达式）

```c
#define MAP_NR(addr) ({ \
    unsigned long __a = (unsigned long)(addr); \
    (__a >= LOW_MEM) ? ((__a - LOW_MEM) >> 12) : -1; \
})

#define inb(port) ({ \
    unsigned char _v; \
    __asm__ volatile("inb %%dx, %%al" : "=a"(_v) : "d"((unsigned short)(port))); \
    _v; \
})
```

`({ ... })` 语句表达式允许宏包含临时变量。

### 4.5 常量宏

```c
#define NR_TASKS  64      // 最大任务数
#define PAGE_SIZE 4096    // 页大小
#define LOW_MEM   0x100000 // 内核空间起始
#define USED      100     // 已用页标记
#define BLOCK_SIZE 1024   // 磁盘块大小
#define NR_BUFFERS 512    // 缓冲区数量
```

---

## 5. GCC 扩展特性

### 5.1 __attribute__ 扩展

```c
// packed：取消对齐填充
struct minix_superblock {
    // ...
} __attribute__((packed));

// aligned：强制对齐
char buf[4096] __attribute__((aligned(4096)));

// unused：抑制未使用警告
static int unused_var __attribute__((unused));

// noreturn：标记不返回的函数
void panic(const char *msg) __attribute__((noreturn));

// section：放入指定段
void init_func(void) __attribute__((section(".init.text")));

// weak：弱符号（可被同名的强符号覆盖）
void __weak arch_init(void) { }
```

### 5.2 __builtin 函数

```c
// va_list 系列（可变参数）
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

// 其他常用内建函数
__builtin_expect(x, 0)   // 分支预测提示
__builtin_prefetch(ptr)   // 预取提示
__builtin_return_address(0) // 获取返回地址
__builtin_frame_address(0)  // 获取栈帧地址
```

### 5.3 匿名结构体嵌套

```c
struct desc_struct {
    unsigned long a, b;
};

// GCC 允许在结构体内匿名声明另一个结构体
// 但本项目使用的是标准 C，所以需要显式命名
```

### 5.4 零长度数组成员（GCC 扩展）

```c
// C99 灵活数组成员
struct flex_array {
    int count;
    char data[];  // 灵活数组（数据跟在结构体后面）
};

// 分配时 extra 字节紧跟在结构体后面
struct flex_array *fa = malloc(sizeof(*fa) + extra);
```

---

## 6. 内联汇编

（详见预置知识一: x86 汇编的第十章，此处只补充 C 语言视角）

### 6.1 修改内存的 volatile 内联汇编

```c
// __volatile__ 阻止编译器优化掉这个 asm 块
__asm__ __volatile__ (
    "movb $0x20, %%al\n\t"
    "outb %%al, $0x20\n\t"
    : : : "al"
);
```

第三个冒号后列出被修改的寄存器（clobber list），告诉编译器这些寄存器的值已改变。

### 6.2 含内存屏障的内联汇编

```c
// "memory" clobber 告诉编译器内存可能被修改
// 阻止编译器跨过此 asm 重新排序内存访问
__asm__ __volatile__("" : : : "memory");
```

---

## 7. 链接器符号的 C 访问

### 7.1 链接脚本定义的符号

```ld
/* kernel.ld */
SECTIONS {
    . = 0x10800;
    /* ... */
    _end = .;     /* 内核映像结束 */
    _stack_start = . + 0x1000;  /* 内核栈起始 */
}
```

### 7.2 C 代码中的访问

```c
// 声明为外部符号（不是变量，是地址！）
extern unsigned long _end;
extern unsigned long _stack_start;

// 错误！_end 的值是地址本身，不是存储在某处的变量
// int value = _end;  // 这会读取地址处的内容

// 正确：取符号的地址
unsigned long kernel_end = (unsigned long)&_end;

// 将 _end 当作内存起始
memset((void *)&_end, 0, 0x1000);

// 初始化栈
lss _stack_start, %esp;
```

**关键理解：** `extern unsigned long _end;` 声明 `_end` 在某个地址，其值不占用内存。`&_end` 获取的是那个地址的值，`_end` 本身（不加 &）会尝试读取那个地址的内容。

---

## 8. 内核编程约定

### 8.1 无标准库

内核无法使用标准 C 库，需要自己实现：

```c
// lib/string.c — 自实现的字符串函数
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int count);
int strcmp(const char *cs, const char *ct);
int strlen(const char *s);
void *memcpy(void *dest, const void *src, int n);
void *memset(void *s, char c, int count);
void *memmove(void *dest, const void *src, int n);

// lib/ctype.c — 字符分类
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int toupper(int c);
int tolower(int c);

// lib/malloc.c — 简易分配器
void *malloc(unsigned int size);
```

### 8.2 错误处理模式

```c
// 返回 NULL 表示失败
struct buffer_head *bh = bread(dev, block);
if (!bh) return -1;

// 返回 -1 表示失败
if (fd >= NR_OPEN || !current->filp[fd])
    return -1;

// 返回 0 表示失败
int block = new_block(dev);
if (!block) return;

// panic：不可恢复错误
void panic(const char *msg) {
    cli();
    // 打印错误消息到屏幕
    while(1) __asm__("hlt");
}
```

### 8.3 缩进与代码风格

```c
// if/while/for 后跟空格
if (condition) {
    // 4 空格缩进
}

// switch 中 case 可以与 switch 对齐或缩进
switch (c) {
case '\n':
    cursor_y++;
    break;
default:
    if (c >= ' ') {
        video_mem[pos] = c;
    }
}

// 大括号不单独占行（K&R 风格）
while (condition) {
    // ...
}
```

### 8.4 全局变量命名

```c
// 全局变量不做特殊修饰（小内核的惯例）
int jiffies = 0;
struct task_struct *current = NULL;
struct task_struct *task[NR_TASKS];
unsigned long *mem_map = NULL;

// 静态全局变量用于模块内部
static int cursor_x = 0;
static int cursor_y = 0;
```

---

## 9. 二进制数据操作

### 9.1 位操作

```c
// 设置位
bitmap[byte_index] |= (1 << bit_index);

// 清除位
bitmap[byte_index] &= ~(1 << bit_index);

// 测试位
if (bitmap[byte_index] & (1 << bit_index)) { /* 已设置 */ }

// 取模（2的幂可以用位操作优化）
int byte_index = bit / 8;       // bit >> 3
int bit_offset = bit % 8;       // bit & 7
int aligned  = addr & (PAGE_SIZE - 1);  // addr % 4096

// 对齐到 4KB 边界
unsigned long page = addr & 0xFFFFF000;   // 清除低 12 位
unsigned long next_page = (addr + 0xFFF) & 0xFFFFF000;  // 向上对齐
```

### 9.2 移位操作的巧妙用法

```c
// 优先级重算
p->counter = (p->counter >> 1) + p->priority;

// 乘以常量（编译器可能优化为移位）
int offset = inode_num * sizeof(struct d_inode);

// 页框号 ↔ 地址转换
int page_index = (addr - LOW_MEM) >> 12;
unsigned long addr = LOW_MEM + (page_index << 12);

// tab 对齐
cursor_x = (cursor_x + 8) & ~7;   // 向上取整到 8 的倍数
```

### 9.3 按位或标志组合

```c
// 页表标志
#define PAGE_PRESENT  0x001
#define PAGE_RW       0x002
#define PAGE_USER     0x004

// 组合标志
unsigned long pte_value = page_addr | PAGE_PRESENT | PAGE_RW | PAGE_USER;

// 检查标志
if (pte & PAGE_PRESENT) { /* 页存在 */ }
if ((pte & 0x007) == (PAGE_PRESENT | PAGE_RW | PAGE_USER)) { /* 用户可读写 */ }

// 提取物理地址
unsigned long phys_addr = pte & 0xFFFFF000;
```

---

## 10. 内存模型与对齐

### 10.1 内生对齐

```c
// 结构体内存对齐（无 packed）
struct example {
    char a;        // 偏移 0 (1 字节)
    // 3 字节填充
    int b;         // 偏移 4 (4 字节对齐)
    char c;        // 偏移 8
    // 3 字节填充（使结构体大小为 4 的倍数）
};
// sizeof = 12

// 同结构体使用 packed
struct example_packed {
    char a;        // 偏移 0
    int b;         // 偏移 1（可能非对齐访问）
    char c;        // 偏移 5
} __attribute__((packed));
// sizeof = 9
```

### 10.2 页面大小对齐

```c
// 确保数据按页对齐
char page_buffer[4096] __attribute__((aligned(4096)));

// 运行时对齐检查
if (addr & (PAGE_SIZE - 1)) return;  // 未对齐
```

### 10.3 小端序

x86 采用小端序（Little-Endian），低字节在低地址：

```c
unsigned short value = 0x1234;
// 内存中：[0x34][0x12]（低字节在前）

// 从磁盘读取 16 位值（磁盘也可能是小端序）
unsigned short inode_num = *(unsigned short *)buf;
```

---

## 附录：本项目中的关键 C 设计模式

### A.1 双向链表 + 多种索引

缓冲区同时存在于哈希链和 LRU 空闲链：

```c
// 从空闲链摘除
bh->b_next_free->b_prev_free = bh->b_prev_free;
bh->b_prev_free->b_next_free = bh->b_next_free;

// 插入哈希链
bh->b_next = hash_table[hash];
bh->b_prev = NULL;  // 头节点
hash_table[hash] = bh;
```

### A.2 引用计数

```c
// 文件对象的引用计数
f->f_count = 1;              // 新引用
f->f_count++;                // dup/fork 时递增
if (--f->f_count == 0)       // close 时递减并检查
    iput(f->f_inode);

// Inode 缓存引用计数
inode->i_count++;            // iget 递增
inode->i_count--;            // iput 递减
```

### A.3 状态机模式

```c
// 进程状态机
current->state = TASK_INTERRUPTIBLE;    // 进入睡眠
schedule();                              // 让出 CPU
// 被唤醒后
current->state = TASK_RUNNING;           // 恢复运行

// 缓冲区状态机
bh->b_lock = 1;                         // 锁定
// ... 使用 ...
bh->b_lock = 0;                         // 解锁
wake_up(&bh->b_wait);                   // 唤醒等待者
```

### A.4 延迟分配 / 懒加载

```c
// bmap 中的按需分配
unsigned short bmap(struct m_inode *inode, int block, int create) {
    if (block < 7) {
        if (create && !inode->i_zone[block])
            inode->i_zone[block] = new_block(inode->i_dev);  // 延迟分配
        return inode->i_zone[block];
    }
    // ...
}
```
