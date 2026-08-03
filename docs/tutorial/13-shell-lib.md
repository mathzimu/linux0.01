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
| echo | `printk` 参数 |
| help | 打印列表 |
| ps | 遍历 `task[]` 打 pid/state/counter |
| clear | `con_init()` |
| exit | `sys_exit(0)` |
| ls/cat | 提示 not implemented |

## 3. `lib/string.c`

自实现：`strcpy` `strcmp` `strlen` `memcpy` `memset` `memmove` 等。  
`-fno-builtin` 下内核必须自带。

## 4. `lib/ctype.c`

`isdigit` `isspace` `isalpha` `tolower` `toupper` 等。

## 5. `lib/malloc.c`

- bump：从 `_end+0x40000` 向上  
- 上界约 `memory_end-0x200000`  
- **无 free**；与页分配器独立  

## 6. `lib/close.c`

薄封装 `close → sys_close`（若有用户态链接需求）。

## 7. 自检

1. Shell 阻塞读键盘时，谁负责唤醒？  
2. 为何 `ps` 不需要 `/proc`？  
3. `malloc` 失败返回什么？
