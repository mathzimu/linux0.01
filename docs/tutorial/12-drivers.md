# §12 设备驱动 — `drivers/*`

## 1. 文件

| 文件 | 功能 |
|------|------|
| `console.c` | VGA 80×25 文本，`con_init`/`con_write` |
| `keyboard.c` | PS/2 扫描码 → ASCII → TTY 读缓冲 |
| `hd.c` | IDE PIO 读扇区 |
| `tty_io.c` | TTY 环形缓冲与 `tty_write` |

## 2. 控制台 `console.c`

- 显存：`(unsigned short *)0xB8000`，属性默认 `0x07`
- 光标：`outb` CRT 索引 0x3D4/0x3D5，寄存器 14/15
- `con_write(tty)`：消费 `write_buf`，处理 `\n\r\t\b` 与滚动
- `con_init`：清屏（Shell `clear` 直接调它）

## 3. 键盘 `keyboard.c`

中断路径：`keyboard_interrupt`（head.s）→ `inb $0x60` → `kbd_interrupt_handler`

- 释放码 `scancode&0x80`：处理 Shift 释放后忽略  
- Shift：`0x2A/0x36` 与 `0xAA/0xB6`  
- 映射表 `scancode_table` / `shift_map`  
- 字符写入 `tty_table[0].read_buf`，若有 `read_waiter` 则唤醒  

## 4. 硬盘 `hd.c`

- 端口：`include/linux/hdreg.h`（0x1F0 数据等）  
- `hd_read_sectors(lba, nsects, buf)`：LBA 模式，轮询 DRQ，`insw` 256 字/扇区  
- `hd_interrupt_handler`：供 IRQ14；当前读路径以轮询为主  
- setup.s 曾 mask 从 PIC `0xFF`，硬盘中断默认被屏蔽  

## 5. TTY `tty_io.c`

```c
struct tty_struct {
  write_buf/head/tail/cnt;
  read_buf/head/tail/cnt;
  read_waiter;
  void (*write)(struct tty_struct *);  // → con_write
};
```

- `tty_init`：初始化 `tty_table[0]`，`write=con_write`，并 `con_init`  
- `tty_write`：填写环，满则调用 `tty->write` 冲刷；末尾再 flush  

## 6. 数据流

```
按键 → IRQ1 → kbd_handler → read 环
Shell read_line / sys_read(0) ← 消费 read 环

printk / sys_write(1) → tty_write → write 环 → con_write → VGA
```

## 7. 自检

1. 退格时 Shell 为何写 `"\b \b"`？  
2. TTY 写满时当前实现如何处理（阻塞还是冲刷）？  
3. 块号到 LBA 的换算在哪一层（buffer 还是 hd）？
