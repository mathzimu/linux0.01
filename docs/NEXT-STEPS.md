# 后续工作清单（NEXT STEPS）

> Linux 0.01 功能对齐完成于提交 `5426b8b`；其后 main 持续推进（/bin 布局、目录扩容、
> 基础应用、SIGSEGV 语义、测试与 CI 加固）。当前主线是 **M1 → M2 → M3 三阶段演进**。

---

# 演进路线（M1 / M2 / M3）

## M1 — 内存地图固化与静默损坏修复 ✅ 已完成

**动因**：审查中发现两处会**静默写坏文件系统**的重叠（Ring0 无视 PTE 的 U/S 位，
所以既不会缺页也不会报错）：

1. `NR_BUFFERS = 512` → 缓冲区缓存位于 `~0x370000..0x3F2000`，
   **压在用户堆 `[0x310000,0x3FE000)` 上**：用户 `malloc` 的字节与文件系统块缓冲是同一批物理页
2. fork 的子进程用户栈锚点 `0x3E0000` **同样落在缓存区里**：
   Ring3 fork 会把用户栈副本直接写穿文件系统缓存

**已完成**：

- `include/memlayout.h`：内存地图唯一权威（内核 + 用户态共用）；
  `include/linux/memmap.h`：内核侧边界 + `STATIC_ASSERT`；
  `include/memlayout.inc`：汇编侧镜像（`-Iinclude` 后 `.include "memlayout.inc"`）
- 用户区窗口重排：程序 `[0x200000,0x300000)` / 堆 `[0x310000,0x340000)` /
  fork 子进程栈 `(0x300000,0x340000]` / 缓冲区缓存 `[0x3BC000,0x400000)`（256 × 1KB）
- `NR_BUFFERS` **由内存地图派生**（`include/linux/fs.h`），不再手工挑选；
  `buffer_init()` 装不下即 `panic`（不再静默重叠）
- `mm/memcheck.c` + `mem_check()`：启动即校验（内核 `_end` 上限、缓存 vs 堆/栈/子进程栈、
  缓存条数、用户区有序、页分配器上限），不一致 → 打印完整内存地图并 panic
- `get_free_page()` 加池上限守卫；`lib/malloc.c` 上界从 `memory_end-0x200000` 改为
  `KERNEL_HEAP_END`（旧上界会伸进页分配器池）
- `sys_execve()` 校验 ELF 入口与 LOAD 段必须落在用户程序镜像内（越界不再静默写内核区）
- fork 的用户栈副本改到专属区并加上限检查（放不下 → fork 返回 -1，不再覆盖相邻区域）
- `user/bigalloc.c` + 回归场景 11（堆填满 → 重读文件 → 逐字节校验）、场景 12（启动自检）
- `scripts/check-layout.py`（`make check-layout`）：**不需要编译器**即可校验地图；
  并扫描“用户区地址被硬编码到布局头之外”
- `.gitignore`/`git rm --cached`：根目录误跟踪的 `system`、`system.bin` 移出版本库

**M1 遗留（归入 M3）**：fork 仍是**代码段/堆共享 + 用户栈真实复制**，
不是 POSIX 语义（子进程写堆父进程可见、并发 fork 的子进程栈会互相覆盖）。
M1 只是把它限制在可检测/可控范围内，真正的修复是 M3 的 COW + 独立地址空间。

## M2 — 用户态能力补齐（进行中）

| 项 | 内容 | 关键文件 |
|----|------|----------|
| M2-1 | **自定义信号处理器 + `sigreturn`**：`signal()` 接受函数指针，进入处理器前构造返回帧（用户栈上放 sig + 保存的上下文），`do_signal` 在 `ret_from_sys_call` 路径投递 | `kernel/process.c`、`include/signal.h`、`user/lib.c` |
| M2-2 | **Ring3 用户态 shell（`/bin/sh`）**：把「谁能驱动系统」真正交给用户态进程；内核态 shell 保留为救援/调试入口（execve 失败可回退），内嵌工具继续可用 | `user/sh.c`、`kernel/main.c`、`init/shell.c` |
| M2-3 | **定时回写**：`do_timer` 按 jiffies 触发 `sync_dev`，异常退出不再丢数据，测试也不再需要每次重建 `minix.img` | `kernel/sched.c`、`fs/buffer.c` |

## M3 — 架构级内存模型（计划中）

放弃「单页表恒等映射 0–4MB + 固定用户地址」，改为**每进程独立页目录 + 帧分配器按页授权**：

| 项 | 内容 |
|----|------|
| M3-1 | 每进程页目录/页表、`get_free_page` 之外的帧分配器改造、`free_page_tables` 真正可用 |
| M3-2 | `execve` 按 ELF 段建立映射（不再按 link vaddr 直接写）+ 按需调页（`do_no_page` 分配而非杀进程） |
| M3-3 | `copy_page_tables` + COW（`un_wp_page`）：fork 语义真正正确，子进程栈/堆私有 |
| M3-4 | fork/exit 路径适配、映射扩容（8/16MB）、用户区搬离固定地址 |
| M3-5 | 回归与文档收尾（新场景：fork 后父子堆互不可见、缺页计数、OOM 行为） |

> M3 会**冲淡**现有“内存隔离”亮点（COW 之后所有用户页本来就要 U/S=1），
> 换来的是真正的多进程能力。`docs/GIT-WORKFLOW.md` §6 已为它预留 major 版本语义。

---

## 当前状态（一句话）

**67 个系统调用（编号与 1991 Linux 0.01 完全一致）**、23 条 Shell 命令的教学内核：
进程生命周期完整（fork/execve/waitpid/信号/管道）、MINIX FS 增删改查 + 硬链接/重命名、
Ring3 用户态 + 编程工具链（`make prog NAME=xxx` → `exec /xxx`）、内存隔离、chdir。

## Linux 0.01 功能对齐（`39f1b72`→`5426b8b`）

- **系统调用编号 0-66 与 Linux 0.01 的 sys_call_table 完全一致**（unistd.h 同步）
- 新增实现：creat、link（硬链接）、rename、stat/fstat、chmod/chown、access、
  umask、uname、stime、utime、setuid/getuid/setgid/getgid/geteuid/getegid、
  alarm（SIGALRM）、nice、times、setpgid/getpgrp/setsid、chroot、fcntl（F_DUPFD）、
  brk、**pipe（管道，0.01 fs/pipe.c 移植）**
- open 改 3 参数（flag/mode + O_CREAT/O_TRUNC + umask）
- stub 保留 -1 的：break/mount/umount/ptrace/stty/gtty/ftime/prof/acct/phys/
  lock/mpx/ulimit/ustat/ioctl —— **与 Linux 0.01 自身 -ENOSYS 完全一致**
- 比 0.01 强：mknod/rename/chroot 是 0.01 的 stub，我们已真实现；有内存隔离
- 验证：user/sysdemo.c、user/pipedemo.c + shell ln/mv/stat/id 命令

## 后续功能清单（按优先级）

### 1. ~~printf 增强（user/lib.c）~~ ✅ 完成（`e8681e6`）
- 已补：`%ld/%lu/%lx`（long 修饰符）、精度 `%.d`（数字补零 / 字符串截断）、
  宽度 `%Ns`、左对齐 `%-`、`%#x/%#o` 前缀（`0x`/`0`）
- 验证：`user/printf.c` 演示程序（`make prog NAME=printf` → `exec /bin/printf`），回归全过

### 2. ~~readdir 便捷接口（用户库）~~ ✅ 完成（`35ad6e2`）
- `lib.h` 提供 `struct dirent`（d_ino + d_name[15]）、`DIR`、`opendir/readdir/closedir`
- 选择**用户库封装**而非新增 `sys_getdents`（syscall 编号保持与 Linux 0.01 一致，更简单）
- readdir 内部读 16 字节目录项、跳过 ino==0 空槽、name 复制为 NUL 结尾
- 示例：`user/ls.c`（`make prog NAME=ls` → `exec /bin/ls` / `exec /bin/ls /docs`），QEMU 验证通过

### 3. ~~更多 libc（user/lib.c）~~ ✅ 完成（`c483d76`）
- `atoi/strtol`（支持 base 0/2..36、前导空白、符号、0x/0 前缀、endptr 停靠点）
- 字符串函数副本（与内核 lib/string.c 一致）：strcpy/strncpy/strcmp/strncmp/strcat/
  strlen/strchr/strrchr/memcpy/memset/memcmp/memmove
- ctype 副本：isdigit/isspace/isalpha/isalnum/isupper/islower/tolower/toupper
- 全部声明在 user/lib.h（用户 include lib.h 即可，无需 include <string.h>）
- 示例：`user/str.c`（`make prog NAME=str` → `exec /bin/str`），QEMU 验证通过

### 4. ~~SIGCHLD 完整语义（内核）~~ ✅ 完成（`4aca6df`）
- 新增 **syscall 22 `signal(sig, handler)`**（SIG_DFL/SIG_IGN，SIGKILL 不可忽略；
  fork 继承 dispositions——`*p = *current`）
  > M2 更新：`signal()` 现已支持**自定义处理器**（Ring3 函数指针，越界地址被拒），
  > 通过 `sigreturn`（syscall 67）恢复被中断的上下文；dispositions 存于
  > `task_struct.handlers[32]`（取代旧的 `sig_ignore_mask`）。
- `signal(SIGCHLD, SIG_IGN)`：waitpid 立即返回 -1（ECHILD）；子进程 exit 时
  不通知父（不置 SIGCHLD 位/不唤醒）；**调度器 schedule() 自动回收**这些僵尸
  （也顺带清理孤儿僵尸——本内核无收养，父退出后僵尸无人 reap 的问题一并解决；
  清理循环必须跳过 current：sys_exit 正在 schedule() 让出，页不能提前释放）
- 演示：`user/sigchld.c` 三阶段（忽略→ECHILD+自动回收 / 默认→waitpid(-1) /
  WNOHANG→0 非阻塞），QEMU 验证通过
- ⚠️ 教训：head.s system_call 的 syscall 号上限检查 `cmpl $22,%eax; jb` 要同步
  放宽到 23，否则新 syscall 一律返回 -1

### 5. ~~内存隔离（内核，大工程）~~ ✅ 完成（`795364a`）
- 页表 0 全部 PTE 由 0x07 改 **0x03（P+RW，无 U/S）** —— 0-4MB 默认内核专属
  （⚠️ 教训：0x06=0b110 没有 P 位，会整页 not-present，曾致启动即崩；
   内核页标志是 0x03，不是 0x06）
- `grant_user_pages(from,size)`（mm/memory.c）：按页把 PTE 置 U/S 位
  （|= 4 → 0x07），并重载 CR3 刷 TLB
- 授权区域：启动时（main.c）堆+栈 [0x310000, 0x400000)；execve 时按
  ELF 段尾授权程序区 [0x200000, max_end)；run_user_program 同
- **效果**：Ring3 只能访问程序/堆/栈页；内核页（含 buffer cache、
  任务页、页表）用户不可访问；越权访问 → page fault → do_no_page
  panic（教学行为：非法访问即崩溃）
- 验证：`user/bad.c` 读 0x0 → `PAGE FAULT` → 肇事进程 SIGSEGV 终止（exit 139），
  内核继续运行（不再 panic）；hello/printf/ls/catfile/memtest/sigchld + 内建命令全回归

### 6. ~~chdir / 相对路径（内核）~~ ✅ 完成（`81c7f1d`）
- task_struct 加 `struct m_inode *pwd`；init（sched_init 里 FS 挂载后）pwd=根
  inode；fork 继承（`p->pwd->i_count++` 共享引用）；sys_exit iput 释放
- **syscall 23 `chdir(path)`**：namei 解析（相对旧 pwd）、非目录返回 -1、
  iput 旧 pwd 换新
- namei 支持相对路径（非 `/` 开头从 current->pwd 起步，walk 期间多持一
  引用，iput 平衡）；`""`=当前目录；`.` 特判（`./x`、`.`）
- split_path 的 bare name 由 `/`（根）改为 `""`（当前目录）→ touch/mkdir/
  rm/rmdir 相对路径生效
- **mkminix 目录补 `.`/`..` 项**（root 6 项、docs 3 项、注入后 size 刷新
  +2）——否则 `cd /docs` 后 `cd ..` 失败（基础目录无 .. 项）
- shell 加 `cd` 命令；`ls` 默认当前目录
- 验证：`cd /docs`→`ls`→`cd ..`、`touch x`/`rm x` 相对创建删除、
  `mkdir sub`→`cd sub`（. / .. 指向正确）→`rmdir sub`、`exec /bin/ls .` 全通

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
make test                   # 一键回归（scripts/regress.sh，8 个核心场景断言）
# 注意：QEMU writeback 会把测试中的脏块刷进 minix.img —— 测试前 rm -f minix.img && make minix.img
#       （make test 每个场景自动重建干净盘）

# 用户程序写法
# user/xxx.c: #include "lib.h"; int main(int argc, char *argv[]) {...}
# make prog NAME=xxx 每次重建 minix.img（含 /hello + /xxx）；多程序用
#   tools/mkminix minix.img user/a.elf:a user/b.elf:b 一次性注入
```

## 已知限制 / 注意事项

1. **内存隔离已实现**：内核页 supervisor-only；用户仅可访问程序/堆/栈页
   （越权访问 → SIGSEGV 终止肇事进程）
2. **内存地图唯一来源**：`include/memlayout.h`（汇编镜像 `include/memlayout.inc`）；
   写死用户区地址会被 `make check-layout` 拦下（见 M1）
3. **chdir 已支持**（syscall 23）；`..` 依赖目录的 `..` 项（mkminix 已写入；
   `mkdir` 建的目录自带 . / ..）
4. **无自定义信号处理器** —— 只有默认动作（SIGINT/KILL 杀进程）；M2-1 目标
5. **目录已支持扩容** —— >64 项时自动分配单间接块（`ensure_dir_block`；
   `rmdir` 释放全部目录 zone）。多级间接（>519 项）不支持。
6. **用户栈**：顶 `USER_STACK_TOP`(0x3FF000)，crt 从 `USER_ARGC_ADDR`/`USER_ARGV_ADDR`
   读 argc/argv（execve 约定）
7. **用户堆**：`[USER_HEAP_START, USER_HEAP_END)` = `[0x310000,0x340000)`（lib.c）
8. **程序链接地址**：`USER_PROG_START`(0x200000)，固定无 PIE；
   `user/lib.h` 里有链接地址断言（链错地址 = 编译失败）
9. **fork 仍非 POSIX 语义**：用户栈是真实副本，但**代码段/堆父子共享**（无 COW）。
   fork 后立即 execve 是当前唯一稳妥用法 —— M3 修复
10. **printf %s 需 NUL 终止**（read 后手动补）

## 关键文件地图

| 文件 | 作用 |
|------|------|
| `include/memlayout.h` | ★ 内存地图唯一权威（内核+用户态共用） |
| `include/linux/memmap.h` | 内核侧边界 + `STATIC_ASSERT` 布局自检 |
| `include/memlayout.inc` | 汇编侧 `.equ` 镜像（与上面同步校验） |
| `mm/memcheck.c` | `mem_check()`：启动自检，不一致即 panic + 地图 dump |
| `scripts/check-layout.py` | 无编译器的静态地图校验（`make check-layout`） |
| `kernel/sys.c` | 系统调用实现（含 sys_execve） |
| `kernel/process.c` | fork/waitpid/exit/do_signal |
| `boot/head.s` | system_call 入口（syscall_cpl 检测）、sys_call_table、setup_paging（PTE 0x03） |
| `mm/memory.c` | 物理内存管理 + `grant_user_pages`（内存隔离授权） |
| `mm/page.s` | page_fault 处理（do_no_page） |
| `fs/*` | MINIX FS（inode.c 的 iget/read_inode 有历史 bug 修复记录） |
| `init/shell.c` | Shell 命令 + run_user_program |
| `user/lib.h/.c` | 用户态库（printf/malloc/syscall 包装） |
| `user/crt.s` | 用户程序入口（读 `USER_ARGC_ADDR`/`USER_ARGV_ADDR`） |
| `user/hello.c … bigalloc.c` | 示例程序（printf / readdir / libc / SIGCHLD / 隔离 / 堆-缓存不重叠） |
| `tools/mkminix.c` | 镜像制作（`tools/mkminix minix.img prog.elf:name` 注入；目录含 . / ..） |
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
- **syscall 上限检查**：head.s `cmpl $N,%eax; jb` 的 N 必须与 sys_call_table 项数
  同步（本次新增 signal=22 时忘了改，新 syscall 全被拦返回 -1）
- **页表标志**（`81c7f1d` 后）：内核页 PTE=0x03（P+RW 无 U/S）；写页表标志时
  0x06=0b110 **没有 P 位**（曾致 0-4MB 全 not-present、启动即崩）；授权用户页
  用 `|= 4`（0x03→0x07）
- **缓冲区缓存压在用户堆上**（M1）：`NR_BUFFERS=512` 时缓存 `[0x370000,0x3F2000)` 与
  用户堆 `[0x310000,0x3FE000)` 重叠；Ring0 无视 PTE 的 U/S 位 → 用户 malloc 的字节
  就是文件系统块缓冲，**写坏文件系统而毫无提示**。修复：缓存条数由内存地图派生 +
  静态断言 + `mem_check()` 启动自检 + `bigalloc` 回归用例
- **fork 子进程用户栈落进缓存区**（M1）：旧锚点 `0x3E0000` 在缓存范围内，
  Ring3 fork 会把用户栈副本写穿文件系统缓存。修复：独立子进程栈区
  `(0x300000,0x340000]` + 上限检查（放不下则 fork 返回 -1）
- **`lib/malloc.c` 上界错误**（M1）：旧上界 `memory_end - 0x200000` 会把内核 bump 堆
  伸进页分配器池（0x18A000 以上）；现为 `KERNEL_HEAP_END`
- **恒等映射下 `get_free_page` 能撞用户区**（M1）：页分配器从 0x100000 顺序扫描，
  不设上限会走到用户程序镜像所在的物理页；现在越界即 `panic`
