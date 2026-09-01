# 后续工作清单（NEXT STEPS）

> 当前阶段收尾于提交 `0d3c58e`。本文档保存未完成的工作与继续所需的关键上下文。

## 当前状态（一句话）

22 个系统调用、19 条 Shell 命令的教学内核：进程生命周期完整（fork/execve/waitpid/信号）、
MINIX FS 增删改查闭环、Ring3 用户态 + 编程工具链（`make prog NAME=xxx` → `exec /xxx`）全通。

## 后续功能清单（按优先级）

### 1. ~~printf 增强（user/lib.c）~~ ✅ 完成（`e8681e6`）
- 已补：`%ld/%lu/%lx`（long 修饰符）、精度 `%.d`（数字补零 / 字符串截断）、
  宽度 `%Ns`、左对齐 `%-`、`%#x/%#o` 前缀（`0x`/`0`）
- 验证：`user/printf.c` 演示程序（`make prog NAME=printf` → `exec /printf`），回归全过

### 2. ~~readdir 便捷接口（用户库）~~ ✅ 完成（commit 待填）
- `lib.h` 提供 `struct dirent`（d_ino + d_name[15]）、`DIR`、`opendir/readdir/closedir`
- 选择**用户库封装**而非新增 `sys_getdents`（保持 syscall 数 22 不变，更简单）
- readdir 内部读 16 字节目录项、跳过 ino==0 空槽、name 复制为 NUL 结尾
- 示例：`user/ls.c`（`make prog NAME=ls` → `exec /ls` / `exec /ls /docs`），QEMU 验证通过

### 3. 更多 libc（user/lib.c）
- `atoi/strtol`（解析数字，配合 printf %d）
- `strlen/strcpy/strcmp` 等（内核 `lib/string.c` 已有，可移植进用户库或让用户 -Iinclude 复用）
- 注意：include/string.h 的函数是内核编译的，用户程序链接需用户库副本

### 4. SIGCHLD 完整语义（内核）
- 现状：子进程 exit 发 SIGCHLD 唤醒父；do_signal 忽略 SIGCHLD
- 目标：父可 `signal(SIGCHLD, SIG_IGN)` 时子进程自动回收（避免僵尸累积）
- 或 waitpid(-1, ...) 任意子进程的 Shell 演示（WNOHANG 已有实现未演示）

### 5. 内存隔离（内核，大工程）
- 现状：页表 U/S=1，用户可访问全部 0-4MB（教学取舍）
- 目标：用户段页 U/S=0（内核）→ 保护内核；execve 时为用户程序映射专用页
- 风险高：fork 的页表复制、用户栈/堆的页分配都要改
- **建议留到最后**，改动前先备份当前可运行状态

### 6. chdir / 相对路径（内核）
- 现状：namei 硬编码从根 `/` 解析
- 目标：task_struct 加 `pwd` inode；namei 支持相对路径
- 涉及：sys_chdir（新增 syscall）、open/mkdir 等路径解析

## 工具链速查（继续工作必备）

```bash
# 构建
make                      # 内核（i686-elf 交叉工具链）
make minix.img            # MINIX 测试盘（注入 hello + 参数程序）
make prog NAME=xxx        # 编译 user/xxx.c 并注入
make Image                # 引导镜像

# 运行/验证
qemu-system-i386 -fda Image -hda minix.img -m 4M -boot a
python3 scripts/qemu-test.py --image Image --hda minix.img --keys $'cmd\n'
# 注意：QEMU writeback 会把测试中的脏块刷进 minix.img —— 测试前 rm -f minix.img && make minix.img

# 用户程序写法
# user/xxx.c: #include "lib.h"; int main(int argc, char *argv[]) {...}
```

## 已知限制 / 注意事项

1. **无内存隔离**（U/S=1）—— 用户可读写内核内存（教学取舍，见清单 5）
2. **无 chdir** —— 路径必须从 `/` 写全
3. **无自定义信号处理器** —— 只有默认动作（SIGINT/KILL 杀进程）
4. **目录满不扩容** —— 单目录 >64 项需间接块目录（未实现）
5. **用户栈**：顶 0x3FF000，crt 从 0x3FF004/0x3FF008 读 argc/argv（execve 约定）
6. **用户堆**：0x310000-0x3FE000（lib.c bump+freelist）
7. **程序链接地址**：0x200000（固定，无 PIE）
8. **printf %s 需 NUL 终止**（read 后手动补）

## 关键文件地图

| 文件 | 作用 |
|------|------|
| `kernel/sys.c` | 系统调用实现（含 sys_execve） |
| `kernel/process.c` | fork/waitpid/exit/do_signal |
| `boot/head.s` | system_call 入口（syscall_cpl 检测）、sys_call_table |
| `fs/*` | MINIX FS（inode.c 的 iget/read_inode 有历史 bug 修复记录） |
| `init/shell.c` | Shell 命令 + run_user_program |
| `user/lib.h/.c` | 用户态库（printf/malloc/syscall 包装） |
| `user/crt.s` | 用户程序入口（读 0x3FF004/0x3FF008） |
| `user/hello.c catfile.c memtest.c printf.c ls.c` | 示例程序（printf 演示 / readdir 列目录） |
| `tools/mkminix.c` | 镜像制作（`tools/mkminix minix.img prog.elf:name` 注入） |
| `tools/build.c` | 引导镜像拼接 |
| `scripts/qemu-test.py` | 无头回归驱动 |
| `docs/LIMITATIONS.md` | 实现边界（权威：源码 > 本文件） |

## 历史 bug 修复备忘（改相关代码前必读）

- **iget 假 bug**（`5cc7871`）：曾被误判为 inode 缓存缺陷，实为 mkminix 的
  imap 写入顺序（put_inode 必须在 memcpy(imap) 之前）
- **schedule current 预赋值**（`372529c`）：switch_to 的 `cmpl %ecx,current; je`
  要求 current 不能在 schedule 里预先赋值
- **init_task.tss.cr3**（`372529c`）：必须显式设置，否则切回 init 时 CR3=0
- **sys_exit use-after-free**（`372529c`）：zombie 语义（`ac0d136`）后由 waitpid 回收任务页
- **fork 帧**：Ring3 16 项（含 ss/esp）、Ring0 14 项；`child_top` 必须赋值
- **syscall 号保存**：system_call 里 CPL 检测必须在 `push %eax`（保存 syscall 号）之后
