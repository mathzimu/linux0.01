# §9 系统调用与内核输出 — `sys.c` / `vsprintf.c` / `panic.c` / `asm.s`

> 入口汇编见 TUTORIAL §5 `system_call`；表在 `head.s` `sys_call_table`。

## 1. 调用约定

| 寄存器 | 含义 |
|--------|------|
| EAX | 调用号 0–9，返回值 |
| EBX | 参数 0 |
| ECX | 参数 1 |
| EDX | 参数 2 |

`system_call` 把 ebx..ebp 压栈后 `call *sys_call_table(,%eax,4)`，C 侧按栈传参。FS=`USER_DS` 供 `get_fs_byte`/`put_fs_byte`。

## 2. `kernel/sys.c`

### `sys_time`

`return jiffies / HZ;` — 启动后秒数，非 wall clock。

### `sys_write`

- `fd==1||fd==2`：逐字节 `get_fs_byte`，`\n` 前补 `\r`，`tty_write`
- 其它 fd：`file_write` 并推进 `f_pos`
- `fd==0` 写失败

### `sys_read`

- `fd==0`：从 `tty_table[0]` 环形缓冲读；空则 `TASK_INTERRUPTIBLE` + `read_waiter` + `schedule`；遇 `\n` 结束
- 文件：`file_read`

### `sys_open`

1. 找 `current->filp[]` 空槽 → fd  
2. 找 `file_table[]` 中 `f_count==0`  
3. `namei(filename)` → inode  
4. 初始化 `f_mode/f_count/f_inode/f_pos`，挂到 filp  

### `sys_close`

```c
current->filp[fd]=NULL;
f->f_count--;
if (f->f_count==0) iput(f->f_inode);
```

## 3. `kernel/vsprintf.c` — `printk`

- 本地 `vsprintf`：支持 `%d %i %u %x %s %c`
- 有符号数用 `(unsigned long)(-(long)num)` 避免 INT_MIN UB
- 缓冲 1024，经 `tty_write(&tty_table[0], ...)` 输出
- **不走系统调用**；内核直接打日志

## 4. `kernel/panic.c`

- `cli` 后写 VGA `0xB8000` 前缀 `KERNEL PANIC:` + msg  
- 有 `video_end` 边界  
- 死循环 `hlt`

## 5. `kernel/asm.s`

```as
ltr:  // 加载任务寄存器，参数在栈上
```

供 `sched_init` 的 `ltr(64)`。

## 6. Shell 与系统调用的关系

| 路径 | 实际 |
|------|------|
| 打印提示符 | `printk` → tty |
| 读行 | 直接操作 `tty_table` + `schedule` |
| exit | 直接 `sys_exit(0)` |
| 用户程序 `int 0x80` | 入口已就绪，本仓库无独立用户 ELF |

## 7. 自检

1. 为何写控制台要 `\n`→`\r\n`？  
2. `get_fs_byte` 在 Shell 直调 `sys_write` 时 FS 是否一定正确？  
3. `printk` 与 `sys_write(1,...)` 区别？
