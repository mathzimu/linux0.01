# Minimal Linux 0.01 Equivalent Kernel - 高层次架构设计 (HLD)

## 1. 架构概览

### 1.1 系统分层图

```
┌─────────────────────────────────────────────────────┐
│                    Shell (init)                      │
│                (echo, ps, ls, cat)                    │
├─────────────────────────────────────────────────────┤
│              System Call Interface                    │
│           (int 0x80, sys_call_table)                  │
├──────────┬──────────┬──────────┬──────────────────────┤
│ Process   │ Memory   │ File     │    Device Drivers    │
│ Manager   │ Manager  │ System   │                      │
│ (kernel/) │ (mm/)    │ (fs/)    │ (drivers/)           │
│           │          │          │                      │
│ sched.c   │ memory.c │ minix.c  │ console.c keyboard.c │
│ process.c │ page.s   │ buffer.c │ hd.c    tty_io.c     │
│ sys.c     │          │ bitmap.c │                      │
├──────────┴──────────┴──────────┴──────────────────────┤
│              Kernel Core (main.c, asm.s)               │
│            GDT/IDT/TSS Management                      │
├───────────────────────────────────────────────────────┤
│               Head (head.s)                            │
│         Page Tables, IDT Setup, A20 Gate               │
├───────────────────────────────────────────────────────┤
│               Setup (setup.s)                          │
│         Hardware Probe, Enter Protected Mode            │
├───────────────────────────────────────────────────────┤
│               Boot (boot.s)                            │
│         512-byte Boot Sector, Load Kernel               │
└───────────────────────────────────────────────────────┘
```

### 1.2 内存布局

```
0x000000 ┌──────────────────────┐
         │    Real Mode IVT     │ 1KB
0x000400 ├──────────────────────┤
         │    BIOS Data Area    │
0x000500 ├──────────────────────┤
         │   Kernel Boot Params │
0x009000 ├──────────────────────┤
         │   Setup Code (4KB)   │
0x00A000 ├──────────────────────┤
         │   Stack (8KB)        │
0x00C000 ├──────────────────────┤
         │   Kernel Image       │
         │   (代码+数据+BSS)    │
0x010000 ├──────────────────────┤
         │       ...            │
         │                      │
0x100000 ├──────────────────────┤  ← 1MB: Page Directory
         │   Page Directory     │ 4KB
0x101000 ├──────────────────────┤
         │   0-4MB Page Table   │ 4KB
0x102000 ├──────────────────────┤
         │   4-8MB Page Table   │ 4KB (可选)
0x103000 ├──────────────────────┤
         │   Kernel Heap/Data   │
         │                      │
         │   Buffer Cache       │ ~2MB
         │                      │
         │   Process Data       │
         │                      │
0xFFFFFF └──────────────────────┘
```

### 1.3 段描述符布局 (GDT)

| Index | Name          | Base  | Limit    | DPL | Type       |
|-------|---------------|-------|----------|-----|------------|
| 0     | null          | 0     | 0        | -   | -          |
| 1     | kernel_code   | 0     | 16MB     | 0   | code/exec  |
| 2     | kernel_data   | 0     | 16MB     | 0   | data/rw    |
| 3     | user_code     | 0     | 16MB     | 3   | code/exec  |
| 4     | user_data     | 0     | 16MB     | 3   | data/rw    |
| 5     | task0_tss     | -     | -        | 3   | TSS        |
| 6     | task0_ldt     | -     | -        | 3   | LDT        |
| 7     | task1_tss     | -     | -        | 3   | TSS        |
| 8     | task1_ldt     | -     | -        | 3   | LDT        |
| ...   | ...           | ...   | ...      | ... | ...        |

### 1.4 中断向量表 (IDT)

| Vector | Name        | Handler       | Type       | DPL |
|--------|-------------|---------------|------------|-----|
| 0      | divide_error| divide_error  | Trap       | 0   |
| 1-13   | exceptions  | ...           | Trap       | 0   |
| 14     | page_fault  | page_fault    | Trap       | 0   |
| 16     | coprocessor | coprocessor   | Trap       | 0   |
| 32     | timer       | timer_interrupt| Interrupt  | 0   |
| 33     | keyboard    | keyboard_interrupt| Interrupt| 0   |
| 39     | hd          | hd_interrupt  | Interrupt  | 0   |
| 0x80   | sys_call    | system_call   | Trap       | 3   |

---

## 2. 核心数据结构

### 2.1 进程控制块 (task_struct)

```c
struct task_struct {
    long state;              // -1 unrunnable, 0 runnable, >0 stopped
    long counter;            // Time slice remaining
    long priority;           // Priority (initial counter value)
    long signal;             // Signal bitmap
    struct tss_struct tss;   // Task State Segment (hardware context)
    struct file *filp[NR_OPEN];  // File descriptor table
    unsigned long ss:16;     // Stack segment (format for lsl)
    unsigned long esp;       // Stack pointer
    uid_t uid;              // User ID (always 0)
    unsigned long pid;       // Process ID
    unsigned long pgrp;      // Process group (always 0)
    unsigned long alarm;     // Alarm timer
    unsigned long utime;     // User mode time
    unsigned long stime;     // Kernel mode time
    unsigned long cutime;    // Child user time
    unsigned long cstime;    // Child kernel time
    unsigned long start_code;// Code segment start
    unsigned long end_code;  // Code segment end
    unsigned long start_data;// Data segment start
    unsigned long end_data;  // Data segment end
    unsigned long brk;       // Heap end
    unsigned long start_stack;// Stack start
    long pid_leader;         // Session leader flag
    unsigned long session;   // Session ID
    struct desc_struct ldt[3];// Local descriptor table
};
```

### 2.2 TSS (Task State Segment)

```c
struct tss_struct {
    long back_link;    // 0
    long esp0;         // Kernel stack pointer
    long ss0;          // Kernel stack segment
    long esp1;         // 1
    long ss1;          // 1
    long esp2;         // 2
    long ss2;          // 2
    long cr3;          // Page directory base
    long eip;          // Instruction pointer
    long eflags;       // Flags
    long eax, ecx, edx, ebx; // Registers
    long esp, ebp, esi, edi;  // Registers
    long es, cs, ss, ds, fs, gs; // Segment registers
    long ldt;          // LDT selector
    long trace_bitmap; // I/O bitmap base
};
```

### 2.3 块设备缓冲区 (Buffer Head)

```c
struct buffer_head {
    char *b_data;              // Pointer to data block
    unsigned long b_blocknr;   // Block number
    unsigned short b_dev;      // Device number
    unsigned char b_uptodate;  // Data valid flag
    unsigned char b_dirt;      // Dirty flag (modified)
    unsigned char b_count;     // Usage count
    unsigned char b_lock;      // Lock flag
    struct task_struct *b_wait;// Wait queue
    struct buffer_head *b_prev;// Previous in list
    struct buffer_head *b_next;// Next in list
    struct buffer_head *b_prev_free;// Free list prev
    struct buffer_head *b_next_free;// Free list next
};
```

### 2.4 文件结构 (file_struct)

```c
struct file {
    unsigned short f_mode;     // Mode (read/write)
    unsigned short f_flags;    // Flags
    unsigned short f_count;    // Reference count
    struct m_inode *f_inode;   // Pointer to inode
    unsigned long f_pos;       // File position
};
```

### 2.5 MINIX i-node

```c
struct d_inode {
    unsigned short i_mode;     // File type and permissions
    unsigned short i_uid;      // User ID
    unsigned long i_size;      // File size
    unsigned long i_time;      // Modification time
    unsigned char i_gid;       // Group ID
    unsigned char i_nlinks;    // Link count
    unsigned short i_zone[9];  // Direct/indirect block pointers
};
```

---

## 3. 主要流程

### 3.1 系统启动流程

```
Power On
  │
  ├── BIOS POST
  │
  ├── BIOS loads boot sector (boot.s) → 0x7C00
  │   └── Boot sector loads setup.s + kernel to 0x10000 (64KB)
  │
  ├── Jump to setup.s (0x10000)
  │   ├── Get hardware parameters (memory size, disk params)
  │   ├── Set A20 Gate (enable address line 20)
  │   ├── Initialize 8259A PIC (remap IRQs)
  │   ├── Set GDT for protected mode
  │   └── Switch to protected mode (set CR0.PE)
  │
  ├── Jump to head.s (0x100100)
  │   ├── Set up page directory + page tables (identity map 0-16MB)
  │   ├── Enable paging (set CR0.PG)
  │   ├── Set up IDT (256 interrupt gates)
  │   ├── Initialize kernel segments (CS=0x08, DS=0x10)
  │   ├── Set up kernel stack
  │   └── Call main.c
  │
  └── main.c
      ├── mem_init()       → Initialize memory management
      ├── buffer_init()    → Initialize buffer cache
      ├── hd_init()        → Initialize IDE hard disk
      ├── tty_init()       → Initialize console/keyboard
      ├── sched_init()     → Initialize scheduler + timer
      ├── ipc_init()       → (No-op, placeholder)
      ├── sti()            → Enable interrupts
      ├── move_to_user_mode()→ Switch to user mode (CPL=3)
      └── shell()          → Enter shell (init process)
```

### 3.2 上下文切换流程

```
Timer Interrupt (IRQ0, 100Hz)
  │
  ├── CPU pushes EFLAGS, CS, EIP
  ├── Jump to timer_interrupt (IDT entry 32)
  │   ├── Save registers (pusha)
  │   ├── Send EOI to PIC
  │   │
  │   ├── Call do_timer()
  │   │   ├── current->counter--
  │   │   ├── if (current->counter > 0) return
  │   │   ├── current->counter = 0
  │   │   ├── schedule()
  │   │   │   ├── Find next runnable task
  │   │   │   ├── if (next == current) return
  │   │   │   ├── switch_to(next)
  │   │   │   │   └── Hardware TSS switch:
  │   │   │   │       jmp next->tss
  │   │   │   │       (CPU saves/restores all registers)
  │   │   │   └── current = next
  │   │   └── return
  │   │
  │   └── Restore registers (popa; iret)
  │
  └── Return to user process
```

### 3.3 系统调用流程

```
User: int 0x80 (EAX = syscall number, EBX = arg1, ...)
  │
  ├── CPU: push SS, ESP, EFLAGS, CS, EIP
  ├── Switch to kernel stack (TSS.ss0:esp0)
  │
  ├── system_call:
  │   ├── Save registers (pusha; push ds,es,fs,gs)
  │   ├── Call sys_call_table[EAX](EBX, ECX, EDX)
  │   │   ├── Validate arguments
  │   │   ├── Execute kernel function
  │   │   └── Return result in EAX
  │   ├── Restore registers (pop gs,fs,es,ds; popa)
  │   └── iret → Return to user mode
  │
  └── User: result in EAX
```

### 3.4 中断处理流程

```
Device Interrupt (e.g., Keyboard IRQ1)
  │
  ├── CPU pushes EFLAGS, CS, EIP
  ├── Check IF bit → if disabled, wait
  │
  ├── keyboard_interrupt:
  │   ├── Save registers
  │   ├── Read scan code from port 0x60
  │   ├── Send EOI to PIC (port 0x20)
  │   ├── Convert scan code to ASCII
  │   ├── Put character into TTY input buffer
  │   ├── If echo enabled, write to console
  │   ├── Restore registers
  │   └── iret
  │
  └── Return to interrupted process
```

---

## 4. 模块接口定义

### 4.1 C/汇编接口

```
head.s → main.c:        void main(void)
head.s → mem_init:      void mem_init(long start, long end)
head.s → sched_init:    void sched_init(void)
head.s → buffer_init:   void buffer_init(long buffer_end)
head.s → hd_init:       void hd_init(void)
head.s → tty_init:      void tty_init(void)

asm.s exports:
    _divide_error, _timer_interrupt, _system_call
    _keyboard_interrupt, _hd_interrupt

sched.c exports:
    void schedule(void)
    int sys_fork(void)
    int sys_pause(void)

memory.c exports:
    int get_free_page(void)
    void free_page(unsigned long addr)
    int free_page_tables(unsigned long from, unsigned long size)

console.c exports:
    void con_init(void)
    void con_write(struct tty_struct *tty)

keyboard.c exports:
    void keyboard_interrupt(void)
    void kbd_init(void)

hd.c exports:
    void hd_init(void)
    int hd_request(void)
```

### 4.2 系统调用表 (sys_call_table)

```c
sys_call_table[NR_syscalls] = {
    sys_setup,     // 0
    sys_exit,      // 1
    sys_fork,      // 2
    sys_read,      // 3
    sys_write,     // 4
    sys_open,      // 5
    sys_close,     // 6
    sys_getpid,    // 7
    sys_pause,     // 8
    sys_time,      // 9
};
```

---

## 5. 硬件资源映射

| 硬件 | 端口/地址 | 说明 |
|------|-----------|------|
| 8259A (Master) | 0x20, 0x21 | PIC主片 |
| 8259A (Slave) | 0xA0, 0xA1 | PIC从片 |
| PIT (Timer) | 0x40-0x43 | 系统时钟 (100Hz) |
| Keyboard | 0x60, 0x64 | 键盘控制器 |
| VGA Text | 0xB8000 | 显存起始地址 |
| IDE Primary | 0x1F0-0x1F7 | 主IDE通道 |
| IDE Control | 0x3F6 | IDE控制寄存器 |
| A20 Gate | 0x92 (PS/2) | 地址线A20 |
| CMOS | 0x70, 0x71 | RTC/CMOS RAM |

---

## 6. 成功验证标准

1. **QEMU可引导**: `qemu-system-i386 -kernel Image` 成功启动
2. **Shell提示符**: 显示 `$ ` 或 `> ` 等待输入
3. **命令执行**: `echo hello` 输出 hello
4. **进程列表**: `ps` 列出至少2个进程
5. **多任务**: 多个进程并发运行
6. **文件系统**: `ls` 和 `cat` 可访问Minix文件系统

---

## 7. 文件清单

```
boot/boot.s    - 引导扇区 (512B)
boot/setup.s   - 实模式设置 → 保护模式
boot/head.s    - 保护模式初始化 → main()
kernel/main.c  - 内核主函数
kernel/sched.c - 调度器
kernel/process.c - 进程管理
kernel/sys.c   - 系统调用实现
kernel/asm.s   - 汇编辅助函数
kernel/vsprintf.c - 格式化输出
kernel/panic.c - 内核panic
mm/memory.c    - 内存管理
mm/page.s      - 页错误处理
fs/minix.c     - MINIX文件系统
fs/buffer.c    - 块设备缓冲
fs/bitmap.c    - 位图操作
fs/inode.c     - inode管理
fs/file_dev.c  - 文件设备操作
fs/namei.c     - 路径名解析
drivers/console.c - VGA控制台
drivers/keyboard.c - 键盘驱动
drivers/hd.c   - 硬盘驱动
drivers/tty_io.c - TTY输入输出
lib/string.c   - 字符串函数
lib/ctype.c    - 字符类型
lib/malloc.c   - 内存分配
lib/close.c    - close系统调用封装
init/shell.c   - 简单shell
tools/build.c  - 构建工具
Makefile       - 构建文件
kernel.ld      - 链接脚本
```
