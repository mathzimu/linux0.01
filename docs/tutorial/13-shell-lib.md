# §13 Shell 与 C 库 — `init/shell.c` · `lib/*`

## 1. 特权级事实

`shell_main` 由 `main()` **直接调用**，运行在 **Ring 0**。  
不是独立用户进程；`ps` 能直接扫 `task[]`。

## 2. `shell_main` 主循环

```
打印欢迎语
while(1):
  打印 "$ "
  read_line(buf)
  parse_args → argv
  分派 echo|help|ps|clear|exit|ls|cat|unknown
```

### `read_line`

- 等 `tty_table[0].read_cnt`  
- 空：设 `TASK_INTERRUPTIBLE`、`read_waiter=current`、`schedule`  
- `\n` 结束；`\b` 删字符并回显 `\b \b`  
- 普通字符写入 buf 并 `tty_write` 回显  

### 命令

| 命令 | 行为 |
|------|------|
| echo / help / clear | `printk`、帮助、清屏 |
| ps | 遍历 `task[]` 打 pid/state/counter |
| pid / ppid / id / time | getpid/getppid/uid-gid/times 演示 |
| sys / spawn / sig / wait | syscall 路径 / fork×2 / kill+waitpid / exit(42) |
| ls / cat / cd / stat | open/read/close、chdir、stat 演示 |
| touch / mkdir / rm / rmdir | mknod / mkdir / unlink / rmdir |
| ln / mv | 硬链接 / 重命名 |
| wtest / sync | 写路径 + 脏缓冲落盘 |
| fdtest / seektest | dup 共享偏移 / lseek |
| exec | fork + execve 运行 MINIX 里的 ELF（`exec /hello a b`） |
| user | iret 到 Ring3 运行内嵌程序（`run_user_program`，user_data.c） |

> 命令全部走 `int 0x80` 系统调用（67 个，编号 = Linux 0.01）。

## 3. 用户态编程工具链（`user/`）

- `make prog NAME=xxx`：编译 `user/xxx.c`（链接 0x200000）→ `user/xxx.elf` → `tools/mkminix` 注入 MINIX 镜像
- 用户库 `user/lib.h/.c`：`printf`（宽度/精度/long）、`malloc/free`（0x310000–0x3FE000 bump+freelist）、
  `opendir/readdir`、字符串/ctype/atoi/strtol；syscall 包装在 `include/unistd.h`
- 示例：hello（argv）、catfile（读文件）、memtest（堆复用）、printf、ls、str、sigchld、pipedemo、sysdemo
- crt.s：从 0x3FF004/0x3FF008 读 argc/argv，设 esp=0x3FF000，call main，`int 0x80` exit

## 4. `lib/string.c`（内核侧）

自实现：`strcpy` `strcmp` `strlen` `memcpy` `memset` `memmove` 等。  
`-fno-builtin` 下内核必须自带。（用户程序副本在 `user/lib.c`）

自实现：`strcpy` `strcmp` `strlen` `memcpy` `memset` `memmove` 等。  
`-fno-builtin` 下内核必须自带。

## 5. `lib/ctype.c`

`isdigit` `isspace` `isalpha` `tolower` `toupper` 等。

## 6. 内核 `lib/malloc.c`

- bump：从 `_end+0x40000` 向上  
- 上界约 `memory_end-0x200000`  
- **无 free**；与页分配器独立（用户态 malloc 在 `user/lib.c`，带 free）

## 6. `lib/close.c`

薄封装 `close → sys_close`（若有用户态链接需求）。

## 7. 自检

1. Shell 阻塞读键盘时，谁负责唤醒？  
2. 为何 `ps` 不需要 `/proc`？  
3. `malloc` 失败返回什么？  
4. `exec /hello a b` 里 argc/argv 放在哪个地址？为什么不会被用户栈覆盖？  
5. `signal(SIGCHLD, SIG_IGN)` 之后子进程谁回收？
