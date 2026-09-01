# §8 进程管理 — `kernel/process.c`

> 前置：§7 调度 · `task_struct` / `tss_struct`（sched.h）

## 1. 导出接口

| 函数 | 系统调用号 | 返回值 |
|------|-----------|--------|
| `sys_fork` | 2 | 父：子 PID；子：0（经 TSS.eax） |
| `sys_exit` | 1 | 不返回 |
| `sys_getpid` | 7 | `current->pid` |
| `sys_pause` | 8 | 0（被唤醒后） |

## 2. `sys_fork` 完整步骤

### 2.1 分配 PCB 页

```c
p = (struct task_struct *)get_free_page();
if (!p) return -1;
```

一页 4KB：低部放 `task_struct`，高部作内核栈（`esp0 = p + PAGE_SIZE`）。

### 2.2 登记 task 槽

```c
for (i = 0; i < NR_TASKS; i++) {
    if (task[i]) continue;
    task[i] = p; nr = i; pid = i + 1; break;
}
```

PID = 槽位 + 1；task0 的 pid 为 0。

### 2.3 复制与打开文件引用

```c
*p = *current;
for (i = 0; i < NR_OPEN; i++)
    if (p->filp[i]) p->filp[i]->f_count++;
p->pid = pid;
p->counter = p->priority;
p->state = TASK_RUNNING;
```

**必须 `f_count++`：** 否则父进程 close 会使子进程 filp 悬空。

### 2.4 复制内核栈 + 系统调用返回帧

依赖 `head.s` 保存的 `syscall_esp`（进入 `system_call` 时的 ESP）。

```
parent_top = current->tss.esp0
parent_sp  = syscall_esp + 12
size = parent_top - parent_sp
child_top = (long)p + PAGE_SIZE
child_sp  = child_top - size
memcpy(child_sp, parent_sp, size)
```

再在 `child_sp` 下方构造 14 个 long 的返回帧（从父系统调用帧拷贝），使子任务首次被调度时：

- `tss.eip = ret_from_sys_call`
- `tss.eax = 0` → 子进程“fork 返回 0”
- `tss.esp = child_frame`

父进程在 `sys_fork` 的 C 返回路径上返回 `pid`。

### 2.5 填写 TSS 与 GDT

```c
p->tss.esp0 = (long)p + PAGE_SIZE;
p->tss.ss0  = KERNEL_DS;
p->tss.cr3  = read_cr3();      // 共享页表
p->tss.eflags = 0x202;         // IF=1
// cs/ss/ds/... = KERNEL_*     // 本实现子进程从内核路径 resume
tss_entry = 8 + nr*2;
ldt_entry = tss_entry + 1;
set_tss_desc / set_ldt_desc
p->tss.ldt = ldt_entry * 8;
```

## 3. `sys_exit`

```c
// 关闭所有打开文件（filp f_count--、iput inode）；释放 pwd/root 引用
current->exit_code = ret;
current->state = TASK_ZOMBIE;   // 保留 task[] 槽与任务页
// 通知父进程：SIGCHLD 位 + 唤醒 TASK_INTERRUPTIBLE 的父
// （父 signal(SIGCHLD,SIG_IGN) 时跳过通知；僵尸由 schedule() 自动回收）
for (;;) schedule();            // zombie 永不被选中，循环让出 CPU
```

**为什么不能 free_page(current)：** 退出的任务仍运行在它的任务页上，切换时 CPU 会把
状态写回该 TSS——提前释放就是 use-after-free。所以先变 **TASK_ZOMBIE**，由父进程
`waitpid` 回收（读 exit_code + `free_page` + 清 task[] 槽）；init(task[0]) 保持空闲
锚点不退出。父忽略 SIGCHLD 或父已退出（孤儿）的僵尸，由 `schedule()` 的清理循环
自动回收。

## 4. `sys_pause`

```c
current->state = TASK_INTERRUPTIBLE;
schedule();
return 0;
```

被 `wake_up` 或其它路径置回 RUNNING 后继续。

## 5. 自检

1. 子进程如何得到返回值 0，而父进程得到 PID？
2. 为何需要 `syscall_esp`？
3. 打开文件不 `f_count++` 会出什么问题？
