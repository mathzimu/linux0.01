# §15 端到端场景

把分散模块串成完整运行时故事。结合 [LIMITATIONS.md](../LIMITATIONS.md)。

---

## 场景 A：冷启动到提示符

```
BIOS → 0x7C00 boot.s
  读 setup@0x10000、kernel@0x10800
  ljmp setup
setup.s
  测内存→0x10002，A20，PIC 重映射，LGDT，CR0.PE
  far jmp 0x08:0x10800
head.s startup_32
  段寄存器，栈=_end+0x1000
  页目录+页表0（0–4MB），CR0.PG
  IDT/GDT，call main
main.c
  mem_init → buffer_init → tty_init → sys_setup
  sched_init（PIT+task0）→ sti → shell_main
shell
  printk 欢迎语 → "$ "
```

**观察点：** QEMU 窗口出现 `$ `；若无盘，`sys_setup` 可能警告。

---

## 场景 B：按下一个字母键

```
1. 硬件 IRQ1 → 向量 0x21
2. keyboard_interrupt (head.s)
   保存寄存器，DS=KERNEL_DS
   inb 0x60 → scancode
   call kbd_interrupt_handler
   EOI 0x20
3. keyboard.c
   映射 ASCII，写入 tty_table[0].read_* 
   若 read_waiter 非空 → state=RUNNING
4. 若 Shell 在 read_line 里 schedule 睡着：
   下次时钟/调度选中它 → 读出字符 → tty_write 回显
```

---

## 场景 C：执行 `echo hi`

```
read_line 得到 "echo hi\0"
parse_args → argv[0]="echo", argv[1]="hi"
cmd_echo → printk("%s\n", "hi")
  vsprintf → tty_write → con_write → 0xB8000
```

**不经过** `int 0x80`。

---

## 场景 D：时钟滴答与调度

```
PIT → IRQ0 → 0x20 → timer_interrupt
  EOI → do_timer
    jiffies++
    current->counter--
    if 0 → schedule → 可能 ljmp TSS
```

单任务时：`switch_to` 发现 next==current 则不切换。

---

## 场景 E：`int 0x80` 系统调用（机制）

```
用户/测试代码:
  eax=调用号, ebx/ecx/edx=参数
  int $0x80
system_call:
  存 syscall_esp，切 DS/ES/GS=内核，FS=USER_DS
  cmpl $67,%eax; jb → call sys_call_table[eax]（越界返回 -1）
  返回值写回栈上 eax 槽 → iret
```

Shell 的 `ls`/`cat`/`stat`/`touch`/`mkdir`/`exec` 等命令与用户程序全部走这条路径；
用户程序（Ring3）在 `ret_from_sys_call` 返回前会先投递待处理信号。

---

## 场景 F：读一个文件系统块（若盘可用）

```
sys_open("/x") 或 namei
  iget → read_inode → bread(dev, block)
    getblk → ll_rw_block(READ)
      lba = block*2
      hd_read_sectors → 0x1F0 PIO
    b_uptodate=1
  数据在 bh->b_data
```

无可用 MINIX 根设备时，`sys_setup`/`namei` 失败。

---

## 场景 G：fork（若被调用）

```
int 0x80 eax=2
sys_fork:
  新页 PCB + 栈拷贝 + f_count++ + GDT TSS/LDT
  父返回 pid；子被调度时从 ret_from_sys_call 返回 0
```

Shell 当前**不** fork 子进程跑命令。

---

## 调试建议

```bash
make debug
# 另一终端:
gdb kernel/system
(gdb) target remote :1234
(gdb) b main
(gdb) b schedule
(gdb) b system_call
(gdb) c
```

| 想看 | 断点/检查 |
|------|-----------|
| 进 C | `main` |
| 按键 | `kbd_interrupt_handler` |
| 调度 | `schedule` / `do_timer` |
| 读盘 | `bread` / `hd_read_sectors` |
| panic | `panic` |

---

## 场景自检

1. 从按键到屏幕出现字符，最少经过哪些函数？  
2. 为何 `echo` 不增加 `jiffies` 相关系统调用？  
3. 若去掉 `sti()`，Shell 能否读到键盘？
