# §7 调度器 — `kernel/sched.c`

> 前置：`PREREQ-os-theory.md` 调度章 · `include/linux/sched.h` · `include/asm/system.h`

## 1. 文件职责

| 符号 | 作用 |
|------|------|
| `init_task` / `task[]` / `current` | 全局进程表与当前任务 |
| `sched_init` | 注册 task0、TSS/LDT、PIT 100Hz |
| `schedule` | 选最高 counter 的 RUNNING 任务并 `switch_to` |
| `do_timer` | 时钟中断：jiffies++、减时间片、可能调度 |
| `jiffies` | 全局滴答，`HZ=100` |

## 2. 全局数据

```c
int jiffies = 0;
struct task_struct *current = NULL;
struct task_struct *task[NR_TASKS] = {NULL,};  // NR_TASKS=64
```

`init_task` 静态初始化：`state=0`，`counter=priority=15`，其余清零。task0 不通过 `get_free_page` 分配。

## 3. `sched_init` 逐步

1. 清空 `task[]`，`task[0]=current=&init_task`
2. `tss.ss0=KERNEL_DS`，`tss.esp0=&_end+0x1000`（内核栈顶）
3. 填 LDT[0]/[1] 为用户态代码/数据描述符模板（平坦 4GB，DPL=3）
4. GDT[8]=TSS0，GDT[9]=LDT0（`set_tss_desc` / `set_ldt_desc`）
5. `tss.ldt=72`（选择子 9×8），`ltr(64)`（TR=TSS 选择子 8×8）
6. 编程 PIT：命令 `0x36`，计数 `0x2E9B` → ≈100Hz

**GDT 任务槽公式**（与 fork 一致）：

```
TSS_n 入口 = 8 + n*2
LDT_n 入口 = 9 + n*2
选择子 = 入口 * 8
```

## 4. `schedule` 算法

```
while (1):
  从 task[NR_TASKS-1] 扫到 task[0]
  在 state==TASK_RUNNING 中找 counter 最大者 → next, c
  if c > 0: break          // 找到可跑任务
  if c < 0:                // 没有任何 RUNNING
    若 IF 关则 sti; hlt; return
  // c == 0：全部时间片耗尽
  对每个 task: counter = (counter>>1) + priority
切换:
  找 current 下标；若 next 不同则 current=task[next]; switch_to(next)
```

**为何从高下标往低扫？** 与历史 Linux 0.xx 风格一致；无功能依赖。

**空闲时 `hlt`：** 必须保证 IF=1，否则时钟无法唤醒（本仓库已在 `c<0` 分支补 `sti`）。

## 5. `switch_to(n)`（`asm/system.h`）

```c
#define _TSS(n) ((8 + (n) * 2) * 8)   // TSS 选择子
// ljmp 到 TSS 选择子 → 硬件任务切换
// 若 task[n]==current 则跳过
```

硬件会保存/恢复完整 TSS（含 EIP/ESP/段寄存器/CR3）。本仓库 fork 后父子共享 CR3。

## 6. `do_timer`

由 `head.s` 的 `timer_interrupt` 调用（先 EOI 再 call）：

```c
jiffies++;
if (current->alarm && jiffies >= current->alarm) {   // alarm(2)
    current->signal |= (1 << SIGALRM);
    current->alarm = 0;
}
if (current->counter > 0) current->counter--;
if (current->counter > 0) return;
schedule();
```

`schedule()` 开头还有一步：**自动回收**父忽略 SIGCHLD（或父已退出）的僵尸——
`task[i]=NULL; free_page(z)`（跳过 current：退出中的任务页不能提前释放）。

## 7. 与源码其它文件的关系

| 调用方 | 场景 |
|--------|------|
| `timer_interrupt` | 抢占式时间片 |
| `sys_pause` / Shell `read_line` | 主动睡眠后 `schedule` |
| `sleep_on`（buffer.c） | 等缓冲区 |
| `sys_exit` | 退出后必须切走 |

## 8. 自检

1. 为何 `counter = (counter>>1)+priority` 偏向 I/O 型任务？
2. `ltr(64)` 的 64 对应 GDT 哪一项？
3. 仅一个 RUNNING 且 counter 不断被重置时，会不会 `switch_to` 自己？
