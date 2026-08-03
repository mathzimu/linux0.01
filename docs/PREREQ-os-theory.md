# 前置知识四：操作系统理论

本教程讲解理解 Linux 0.01 所需的核心操作系统理论概念。

---

## 目录

1. [操作系统结构](#1-操作系统结构)
2. [进程模型](#2-进程模型)
3. [进程调度](#3-进程调度)
4. [进程间通信](#4-进程间通信)
5. [内存管理](#5-内存管理)
6. [文件系统](#6-文件系统)
7. [设备管理](#7-设备管理)
8. [系统调用接口](#8-系统调用接口)
9. [并发与同步](#9-并发与同步)

---

## 1. 操作系统结构

### 1.1 宏内核 (Monolithic Kernel)

Linux 0.01 是典型的**宏内核**架构：

```
┌────────────────────────────────────────────┐
│               用户空间 (Ring 3)             │
│         ┌─────────────┐                    │
│         │   Shell     │  用户程序           │
│         └──────┬──────┘                    │
├────────────────┼───────────────────────────┤
│                │  系统调用接口 (int 0x80)    │
│         ┌──────┴──────────────────────┐    │
│         │        内核空间 (Ring 0)      │    │
│  ┌──────┴──────┐  ┌──────┐  ┌───────┐ │    │
│  │  进程管理    │  │ 内存  │  │  文件  │ │    │
│  │  调度器     │  │ 管理  │  │  系统  │ │    │
│  └──────┬──────┘  └──┬───┘  └───┬───┘ │    │
│         └────────────┼──────────┘      │    │
│               ┌──────┴──────┐          │    │
│               │   设备驱动    │          │    │
│               │ 键盘│硬盘│VGA│          │    │
│               └─────────────┘          │    │
└─────────────────────────────────────────┘
```

**宏内核的特点：**
- 所有内核服务运行在同一个地址空间
- 内核模块间直接函数调用（无需 IPC）
- 效率高，但一个模块崩溃可能导致整个系统崩溃
- 对比：微内核将驱动、文件系统等放在独立的用户态进程中

### 1.2 层次化设计

```
Layer 7: Shell (用户态)
────────────────────────── 系统调用接口
Layer 6: 系统调用 (sys_read, sys_write, ...)
Layer 5: 文件系统 (namei, file_read, ...)
Layer 4: 缓冲区缓存 (bread, getblk, ...)
Layer 3: 设备驱动 (hd_read, con_write, ...)
Layer 2: 进程管理 (fork, exit, schedule, ...)
Layer 1: 中断处理 (timer, keyboard, system_call, ...)
Layer 0: 硬件初始化 (boot, setup, head, main)
```

依赖方向：上层依赖下层，同层可互相调用。

### 1.3 特权级模型

```
Ring 0: 内核模式
  - 可执行特权指令 (LGDT, LIDT, MOV CR0, HLT, ...)
  - 可访问所有 I/O 端口
  - 可访问所有物理内存
  - 中断处理程序运行在此级

Ring 1-2: (Linux 0.01 未使用)

Ring 3: 用户模式
  - 不能执行特权指令 → 触发 #GP
  - 只能通过系统调用 (int 0x80) 请求内核服务
  - 只能访问用户页 (U/S=1)
  - 经典 OS 中 Shell/用户程序运行在此级
  - **本仓库现状：** Shell 在 Ring 0（见 LIMITATIONS.md）；GDT 仍保留 USER_CS/DS
```

**特权级切换：**

```
Ring 3 → Ring 0:
  1. 中断 / 异常 / INT 指令触发
  2. CPU 从 TSS 加载 SS0:ESP0 (Ring 0 栈)
  3. 保存 SS3:ESP3, EFLAGS, CS:EIP 到 Ring 0 栈
  4. 加载 IDT 中的 CS:EIP (Ring 0 代码)
  5. 开始执行内核代码

Ring 0 → Ring 3:
  1. 执行 IRET 指令
  2. CPU 从栈弹出 EIP, CS, EFLAGS
  3. 如果 CS.RPL > CPL，额外弹出 ESP, SS
  4. 返回用户态继续执行
```

---

## 2. 进程模型

### 2.1 进程 vs 线程

在本项目中，每个进程只有一个内核线程：

```
进程 = 地址空间 + 一个或多个线程

Linux 0.01:
  进程 = task_struct + 用户页表 + 内核线程
  线程概念未支持（每个进程只有一个执行上下文）
```

### 2.2 进程控制块 (PCB)

Linux 0.01 的 PCB 是 `task_struct`：

```c
struct task_struct {
    // === 调度相关 ===
    long state;      // TASK_RUNNING / INTERRUPTIBLE / UNINTERRUPTIBLE
    long counter;    // 剩余时间片（动态优先级）
    long priority;   // 静态优先级基数

    // === 硬件上下文 ===
    struct tss_struct tss;  // TSS: 保存 CPU 寄存器快照

    // === 文件系统 ===
    struct file *filp[NR_OPEN];  // 打开文件表

    // === 进程标识 ===
    int pid;         // 进程 ID
    int uid;         // 用户 ID
    int pgrp;        // 进程组

    // === 内存管理 ===
    struct desc_struct ldt[3];   // LDT: 定义进程地址空间
    unsigned long start_code, end_code, brk, start_stack;

    // === 时间统计 ===
    long cutime, cstime;  // 子进程的用户/系统时间
    long start_time;      // 启动时间
};
```

### 2.3 进程状态转换

```
                    ┌──────────┐
         fork()     │          │
     ┌─────────────→│ RUNNING  │←──────────────┐
     │              │ (可运行)  │               │
     │              └────┬─────┘               │
     │                   │                     │
     │                   │ schedule()            │ wake_up()
     │                   │ (被调度器选中)         │
     │                   ▼                     │
     │              ┌──────────┐               │
     │              │ RUNNING  │               │
     │              │ (运行中)  │               │
     │              └────┬─────┘               │
     │                   │                     │
     │     sleep_on()    │ 时钟中断/时间片用完    │
     │     sys_pause()   │                     │
     │                   ▼                     │
     │   ┌──────────────────────────┐          │
     │   │  INTERRUPTIBLE /         │──────────┘
     │   │  UNINTERRUPTIBLE         │
     │   │  (睡眠中)                 │
     │   └──────────────────────────┘
     │
     │     exit()
     │              ┌──────────┐
     └─────────────→│ ZOMBIE   │
                    │ (已退出)  │
                    └──────────┘
```

**状态详解：**

| 状态 | 值 | 含义 | 触发 |
|------|-----|------|------|
| TASK_RUNNING | 0 | 可运行或在运行中 | fork, wake_up |
| TASK_INTERRUPTIBLE | 1 | 可中断睡眠 | sys_pause, sleep_on(可中断) |
| TASK_UNINTERRUPTIBLE | 2 | 不可中断睡眠 (D状态) | sleep_on(缓冲区等待) |
| TASK_ZOMBIE | 3 | 已退出等待回收 | exit |

### 2.4 fork 的语义

```c
pid_t pid = fork();
if (pid == 0) {
    // 子进程：fork 返回 0
    exec("program");
} else if (pid > 0) {
    // 父进程：fork 返回子进程的 PID
    wait(NULL);  // 等待子进程结束
}
```

**fork 创建了什么？**
- 完整的 task_struct 副本
- 独立的内核栈副本
- 独立的 LDT（定义独立的地址空间）
- 共享的打开文件表引用（但 f_count 会递增）
- 此项目中：共享页表（没有 COW）

---

## 3. 进程调度

### 3.1 调度器基本概念

```
调度器任务：
  1. 从就绪队列中选择下一个要运行的进程
  2. 执行上下文切换
  3. 管理进程优先级和时间片

调度触发时机：
  - 时钟中断：当前进程时间片用完
  - 主动让出：进程调用 sleep_on / sys_pause
  - 进程退出：sys_exit
```

### 3.2 调度算法分类

| 算法 | 描述 | 复杂度 | 本项目使用 |
|------|------|--------|-----------|
| FCFS | 先来先服务 | O(1) | 否 |
| SJF | 最短作业优先 | O(N)/O(logN) | 否 |
| RR | 轮转调度 | O(1) | 是（部分） |
| 优先级 | 基于优先级选择 | O(N)/O(logN) | 是（counter 机制） |
| 多级反馈队列 | 多队列 + 优先级升降 | O(1)/O(N) | 否 |

Linux 0.01 使用的是 **优先级轮转 + 动态优先级** 的混合算法：

```
选择最高 counter 的 TASK_RUNNING 进程
  │
  ├─ 找到了 → 切换
  └─ 没找到 (counter 全为 0)
      │
      └─ 重新计算所有进程的 counter：
          counter = (counter >> 1) + priority
          └ 让等待的进程积累"时间片红利"
```

### 3.3 counter 算法的精妙之处

```
进程 A (CPU 密集): counter 快速消耗，每次触发调度
进程 B (I/O 密集): counter 几乎不消耗，大部分时间在睡眠

当所有进程 counter 耗尽后重新计算：
  A: counter = (0 >> 1) + 15 = 15  ticks
  B: counter = (12 >> 1) + 15 = 21 ticks
                          ~
                          B 获得更多时间片！

长期效果：
  - CPU 密集进程获得基础时间片 (priority)
  - I/O 密集进程获得基础 + 累积红利 (counter >> 1)
  - 自然形成 I/O 优先的调度策略
```

### 3.4 上下文切换开销

Linux 0.01 使用 x86 硬件任务切换，开销包括：

```
1. 保存所有寄存器到当前 TSS (~104 字节写入)
2. TR 更新
3. CR3 更新 (如果不同)
4. 从新 TSS 加载所有寄存器 (~104 字节读取)
5. 加载 LDTR
6. TLB 刷新 (硬件触发)
7. 代码/数据 cache 污染

总开销: ~数百个 CPU 周期 (现代 CPU 优化后更少)
```

现代 Linux 使用软件切换（手动保存/恢复寄存器），因为：
- 灵活性更高（可以选择性保存）
- 可以省略不必要的寄存器
- 避免 TSS 内存访问瓶颈

---

## 4. 进程间通信

### 4.1 Linux 0.01 的 IPC 支持

Linux 0.01 的进程间通信非常原始：

| 机制 | 支持 | 说明 |
|------|------|------|
| fork/exit | 是 | 父子进程关系 |
| 管道 (pipe) | 否 | 未实现 |
| 信号 (signal) | 否 | task_struct.signal 字段存在但未使用 |
| 共享内存 | 否 | 但 fork 后父子进程共享页表（无 COW） |
| 消息队列 | 否 | 未实现 |
| 信号量 | 否 | 未实现 |

### 4.2 sleep_on 隐式等待队列

Linux 0.01 使用 `sleep_on` 实现进程等待，虽然没有显式的等待队列数据结构：

```
多个进程等待同一个资源 (如缓冲区):
  B 调用 sleep_on(&wait):
    tmp = wait(=NULL)
    wait = &B
    B 睡眠 → schedule()

  A 调用 sleep_on(&wait):
    tmp = wait(=B)
    wait = &A   ← 现在 wait 指向 A
    A 睡眠 → schedule()

唤醒链: wait → A (tmp→B) → B (tmp→NULL)

wake_up(&wait) 唤醒 A;
A 恢复后，在 sleep_on 返回前唤醒 B (tmp);
B 恢复后，检查 tmp==NULL，不唤醒其他人。

效果：所有等待者依次被唤醒
```

---

## 5. 内存管理

### 5.1 内存管理单元 (MMU) 功能

```
MMU 提供的核心功能:
  1. 地址转换: 虚拟地址 → 物理地址
  2. 内存保护: 阻止非法访问
  3. 页面级权限: 读/写/执行控制
  4. 缺页支持: 按需加载页

Linux 0.01 使用了 1 和 2（通过恒等映射简化了地址转换）
```

### 5.2 分配策略

```
物理内存分配策略:
  ┌─────────────────────────────────────┐
  │  首次适应 (First Fit)              │
  │  从低地址向高地址查找第一个足够大    │
  │  的空闲块。实现简单，可能产生       │
  │  外部碎片。                         │
  └─────────────────────────────────────┘

Linux 0.01 使用 buddy system 的简化版:
  - 所有页大小相同 (4KB)
  - 位图管理：mem_map[i] = 0(空闲) / 1(已用) / 100(保留)
  - 线性扫描找第一个空闲页 (First Fit)
  - 无碎片问题 (所有分配单位相同)
```

### 5.3 虚拟内存 vs 物理内存

```
虚拟内存 (Virtual Memory) = 操作系统给进程的抽象地址空间
    │
    │ 页表映射
    │
物理内存 (Physical Memory) = 实际 RAM

Linux 0.01 的特点:
  - 使用恒等映射: VA == PA (虚拟地址等于物理地址)
  - 这意味着进程可以直接看到物理内存布局
  - 简化了实现，但牺牲了隔离性和灵活性

Linux 0.12 之后引入了真正的虚拟内存:
  - 每个进程有独立的页目录 CR3
  - VA 不等于 PA
  - 支持按需换页 (demand paging)
  - 支持 Copy-On-Write
```

### 5.4 内存碎片

```
内部碎片 (Internal Fragmentation):
  - 分配单位(4KB)大于实际需要的浪费
  - 例如：只需要 100 字节，但分配了一整页

外部碎片 (External Fragmentation):
  - 空闲内存总量足够但分散在不连续的块中
  - Linux 0.01 不存在此问题 (固定 4KB 分配)
```

---

## 6. 文件系统

### 6.1 文件系统层次

```
应用层          open("/etc/passwd", O_RDONLY)
                  │
系统调用层       sys_open() → 路径解析
                  │
文件系统层       namei() → iget() → file_read()
  (VFS)           │
                  │
缓冲区缓存层     bread() / getblk()
                  │
设备驱动层       hd_read_sectors() / ll_rw_block()
                  │
硬件层          IDE 硬盘控制器
```

### 6.2 磁盘布局设计原则

```
设计目标:
  1. 快速定位文件数据 (inode → 数据块映射)
  2. 高效的磁盘空间管理 (位图)
  3. 可靠性 (超级块冗余、一致性检查)
  4. 碎片最小化 (块大小选择)

MINIX v1 的设计选择:
  - 块大小 1KB (4096 字节页面包含 4 个磁盘块)
  - 固定 14 字符文件名
  - 9 个间接块指针 (7 直接 + 1 一级间接 + 1 二级间接)
  - 位图管理空闲 inode 和数据块
```

### 6.3 缓冲区缓存的作用

```
目的: 减少磁盘 I/O 次数

工作原理:
  1. 读取磁盘块时，先检查缓存 (哈希查找)
  2. 缓存命中 → 直接返回，跳过磁盘 I/O
  3. 缓存未命中 → 分配缓冲区 → 发起磁盘读 → 等待完成
  4. 写操作标记脏位 (b_dirt=1)，延迟写回
  5. 缓存满时，回收 LRU 最老的干净缓冲区

性能收益:
  - 目录 inode 和数据块被频繁访问 → 高缓存命中率
  - 超级块和位图块常在缓存中
  - 写合并：多次写同一块只需一次磁盘写回
```

### 6.4 路径解析 (namei) 算法

```
namei("/usr/bin/sh"):
  1. 从根目录 inode (nr=1) 开始
  2. 读取 "/":
     inode = iget(dev, 1)  // 根目录 inode
  3. 解析 "usr":
     find_entry(inode, "usr", 3, &ino)
     ├─ 读取 inode 指向的目录块
     ├─ 逐个比较目录项 (minix_dir_entry)
     └─ 找到匹配项 → 获取 inode 号
     iput(inode); inode = iget(dev, ino);
  4. 解析 "bin":
     同上
  5. 解析 "sh":
     同上
  6. 返回最终 inode
```

**时间复杂度：** O(P × D)，P=路径深度，D=目录大小。

---

## 7. 设备管理

### 7.1 设备分类

```
字符设备 (Character Device):
  - 按字节流访问 (每次一个字符)
  - 不支持随机寻址
  - 例: 键盘、串口、控制台

块设备 (Block Device):
  - 按固定大小的块访问 (512B/1KB/4KB)
  - 支持随机寻址
  - 例: 硬盘、软盘

Linux 0.01 中的设备:
  字符设备: 控制台 (VGA + 键盘)
  块设备:   IDE 硬盘 (MINIX 文件系统)
```

### 7.2 设备驱动模型

```
Linux 0.01 的驱动模型 (简化):

  系统调用层
      │
      ▼
  TTY 子系统 ← 统一接口
      │
      ├──→ con_write()  ← 控制台驱动 (VGA 文本模式)
      │
      ├──→ (串口驱动)   ← 未实现
      │
      └──→ (pty 驱动)   ← 未实现

  文件系统层
      │
      ▼
  ll_rw_block() ← 块设备统一接口
      │
      ▼
  hd_read_sectors() ← IDE 硬盘驱动
```

### 7.3 中断驱动 vs 轮询

```
中断驱动 (Interrupt-Driven):
  键盘输入:
    按键 → IRQ1 → do_keyboard() → put_queue(c)
  优点: CPU 不需要轮询，响应快
  缺点: 中断处理有开销

轮询 (Polling):
  硬盘 PIO 读:
    发送命令 → while(!(status & DRQ)) → 读取数据
  优点: 简单，适用于短等待
  缺点: CPU 在等待期间被占用

Linux 0.01 的混合策略:
  键盘: 中断驱动 (必须，因键盘输入不可预测)
  硬盘: 轮询 (简化实现，因为磁盘操作总有固定延时)
  时钟: 中断驱动 (PIT 周期性触发)
```

---

## 8. 系统调用接口

### 8.1 系统调用机制

```
用户态调用 write(fd, buf, 5):

1. C 库函数 (lib/close.c 模式):
   int write(int fd, const char *buf, int count) {
       int ret;
       __asm__("int $0x80"
               : "=a"(ret)
               : "a"(__NR_write), "b"(fd), "c"(buf), "d"(count));
       return ret;
   }

2. CPU 陷阱到内核:
   - 保存 CS:EIP, EFLAGS, SS:ESP (通过 TSS 切换到 Ring 0 栈)
   - 加载 IDT[0x80] 中的 CS:EIP

3. 内核分发:
   _system_call:
     call *sys_call_table[4]  → sys_write()

4. 返回用户态:
   iret → 恢复所有寄存器 → 回到 C 库函数
```

### 8.2 系统调用 vs 库函数

```
系统调用:
  - 由内核实现
  - 通过 int 0x80 进入内核态
  - 可以访问硬件和内核数据结构
  - 例: sys_write, sys_read, sys_open

库函数:
  - 在用户态运行
  - 可能封装系统调用 (如 write() 封装 sys_write)
  - 也可能纯粹是用户态计算 (如 strlen, sprintf)
  - 例: printf → 格式化 + write()
```

### 8.3 参数和返回值传递

```
系统调用约定:
  输入:
    EAX = 系统调用号
    EBX = 参数 1
    ECX = 参数 2
    EDX = 参数 3
    (更多参数通过栈或寄存器对传递)

  输出:
    EAX = 返回值
    (负数 = 错误码)

  示例: sys_write(fd, buf, count)
    EAX = 4        (__NR_write)
    EBX = fd       (文件描述符)
    ECX = buf      (缓冲区指针)
    EDX = count    (字节数)
    返回: EAX = 实际写入字节数
```

---

## 9. 并发与同步

### 9.1 临界区问题

```
多个进程共享内核数据:
  例: 缓冲区缓存
    进程 A: bread(dev, 5) → getblk → 修改缓冲区
    进程 B: bread(dev, 5) → getblk → 修改同一缓冲区

  如果 A 和 B 交替执行，可能产生不一致状态。
```

### 9.2 Linux 0.01 的同步机制

**单 CPU、非抢占式内核 = 简化同步**

```
为什么不需要复杂锁:

1. 单 CPU: 同一时刻只有一个进程在执行
2. 非抢占式内核: 在内核态运行时不会被抢占
   (只有主动调用 schedule() 或从中断返回时才切换)

因此:
  - 系统调用执行时是原子的 (到 sleep_on/schedule 为止)
  - 中断处理程序只需关中断 (cli/sti) 保护关键区

临界区保护:
  方法一: 关中断
    cli()
    // 临界区代码
    sti()

  方法二: 睡眠锁 (sleep_on)
    while (bh->b_lock) {
        sleep_on(&bh->b_wait);   // 等待锁释放
    }
    bh->b_lock = 1;              // 获得锁
    // 使用缓冲区
    bh->b_lock = 0;              // 释放锁
    wake_up(&bh->b_wait);         // 唤醒等待者
```

### 9.3 sleep_on 的同步语义

```c
void sleep_on(struct task_struct **p) {
    struct task_struct *tmp;
    tmp = *p;         // 保存旧的等待者
    *p = current;     // 将自己设为新的等待者
    current->state = TASK_UNINTERRUPTIBLE;  // 设为不可中断睡眠
    schedule();       // 让出 CPU
    // 被唤醒后检查旧等待者是否需要唤醒
    if (tmp && (tmp->state == TASK_INTERRUPTIBLE ||
                tmp->state == TASK_UNINTERRUPTIBLE))
        tmp->state = TASK_RUNNING;  // 级联唤醒
}
```

**这是一个"自提交"的等待队列：**
1. 借助中断禁用保证 `tmp = *p; *p = current` 的原子性
2. 等待者链通过内核栈隐式维护（不需要显式链表节点）
3. wake_up 只唤醒等待队列的头，头醒来后级联唤醒下一个

### 9.4 死锁风险

```
死锁四个必要条件:
  1. 互斥: 资源不能共享
  2. 持有等待: 持有资源的同时等待其他资源
  3. 非抢占: 资源不能强制释放
  4. 循环等待: A等B, B等C, C等A

Linux 0.01 中可能的死锁场景:
  - sleep_on(&bh->b_wait) 在缓冲区已锁时调用
  - 如果两个进程互相等待对方释放锁 → 死锁
  - 但由于单CPU+非抢占，实际上串行执行，较安全
```

---

## 附录：关键术语表

| 术语 | 英文 | 说明 |
|------|------|------|
| PCB | Process Control Block | 进程控制块，task_struct |
| GDT | Global Descriptor Table | 全局描述符表 |
| LDT | Local Descriptor Table | 局部描述符表 |
| IDT | Interrupt Descriptor Table | 中断描述符表 |
| TSS | Task State Segment | 任务状态段 |
| PDE | Page Directory Entry | 页目录条目 |
| PTE | Page Table Entry | 页表条目 |
| TLB | Translation Lookaside Buffer | 页表缓存 |
| PIC | Programmable Interrupt Controller | 可编程中断控制器 |
| PIT | Programmable Interval Timer | 可编程间隔定时器 |
| PIO | Programmed I/O | 程序控制 I/O |
| DMA | Direct Memory Access | 直接内存访问 |
| MMIO | Memory-Mapped I/O | 内存映射 I/O |
| IRQ | Interrupt Request | 中断请求线 |
| EOI | End of Interrupt | 中断结束信号 |
| IVT | Interrupt Vector Table | 实模式中断向量表 |
| BDA | BIOS Data Area | BIOS 数据区 |
| MBR | Master Boot Record | 主引导记录 |
| LBA | Logical Block Addressing | 逻辑块寻址 |
| CHS | Cylinder-Head-Sector | 柱面-磁头-扇区 |
| LRU | Least Recently Used | 最近最少使用 |
| COW | Copy-On-Write | 写时复制 |
| IPC | Inter-Process Communication | 进程间通信 |
