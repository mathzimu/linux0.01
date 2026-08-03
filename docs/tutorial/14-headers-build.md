# §14 头文件与构建 — `include/*` · `kernel.ld` · `Makefile`

## 1. 头文件地图

### `include/linux/`

| 头文件 | 内容 |
|--------|------|
| `kernel.h` | NULL、`printk`/`panic` |
| `sched.h` | 任务状态、NR_TASKS、TSS/task_struct/file、系统调用号与原型 |
| `fs.h` | buffer_head、inode、super_block、bread/getblk、new_block 等 |
| `mm.h` | LOW_MEM、PAGE_SIZE、mem_init/get_free_page |
| `tty.h` | tty_struct、TTY_BUF_SIZE、tty_table |
| `head.h` | GDT/IDT、`KERNEL_CS/DS`、`USER_CS=0x1B`、`USER_DS=0x23` |
| `hdreg.h` | IDE 端口与命令 |

### `include/asm/`

| 头文件 | 内容 |
|--------|------|
| `system.h` | sti/cli、CR0/CR3、`set_tss_desc`/`set_ldt_desc`/`switch_to` |
| `io.h` | inb/outb/inw/... |
| `segment.h` | get_fs_byte/put_fs_byte（%%fs:） |
| `memory.h` | copy_page 宏 |

### 其它

- `string.h` / `sys/types.h`：库与类型

## 2. `kernel.ld`

```
OUTPUT_FORMAT(elf32-i386)
ENTRY(startup_32)
. = 0x10800;     /* 与 boot 加载地址一致 */
.text / .data / .bss
_end = .;
```

`objcopy -O binary` 去掉 ELF 头，供 `tools/build` 拼接。

## 3. `Makefile` 流程

```
boot.s → boot.o → objcopy → boot/boot
setup.s → setup
head.o + *.o → ld kernel.ld → kernel/system → system.bin
tools/build boot setup system.bin → Image
可选 mkiso.sh → kernel.iso
```

### 工具链分支

| 环境 | 工具 |
|------|------|
| Linux | `gcc -m32` `as --32` |
| 有 i386-elf-gcc | 交叉编译 |
| 否则 macOS | Docker 镜像编译 |

### 重要 CFLAGS

`-nostdinc -Iinclude -fno-builtin -fno-stack-protector -ffreestanding -O0`

### 目标

| make 目标 | 作用 |
|-----------|------|
| all / Image | 软盘镜像 |
| iso | ISO |
| run / run-cd | QEMU |
| debug | QEMU `-s -S`（GDB :1234） |
| clean | 清理 |

## 4. 镜像布局复习

```
扇区0     boot (512, AA55)
扇区1-4   setup (固定 4 扇区)
扇区5+    kernel raw @ 加载到 0x10800
```

`build.c` 把 setup/kernel 扇区数写入 boot 偏移 `0x1F0`。

## 5. 自检

1. 为何链接地址必须是 `0x10800`？  
2. `-fno-stack-protector` 去掉会怎样？  
3. `ENTRY(startup_32)` 与 boot 跳转目标如何对齐？
