# 后续工作清单（NEXT STEPS）

> Linux 0.01 功能对齐完成于提交 `5426b8b`；其后 main 持续推进（/bin 布局、目录扩容、
> 基础应用、SIGSEGV 语义、测试与 CI 加固）。当前主线是 **M1 → M2 → M3 三阶段演进**。

---

# 演进路线（M1 / M2 / M3）

## M1 — 内存地图固化与静默损坏修复 ✅ 已完成

**动因**：审查中发现两处会**静默写坏文件系统**的重叠（Ring0 无视 PTE 的 U/S 位，
所以既不会缺页也不会报错）：

1. `NR_BUFFERS = 512` → 缓冲区缓存位于 `~0x370000..0x3F2000`，
   **压在用户堆 `[0x310000,0x3FE000)` 上**（M3 前布局）：用户 `malloc` 的字节与文件系统块缓冲是同一批物理页
2. fork 的子进程用户栈锚点 `0x3E0000` **同样落在缓存区里**（M3 前布局）：
   Ring3 fork 会把用户栈副本直接写穿文件系统缓存

**已完成**：

- `include/memlayout.h`：内存地图唯一权威（内核 + 用户态共用）；
  `include/linux/memmap.h`：内核侧边界 + `STATIC_ASSERT`；
  `include/memlayout.inc`：汇编侧镜像（`-Iinclude` 后 `.include "memlayout.inc"`）
- 用户区窗口重排（M3 前布局，M3 已整体废弃）：程序 `[0x200000,0x300000)` / 堆 `[0x310000,0x340000)` /
  fork 子进程栈 `(0x300000,0x340000]` / 缓冲区缓存 `[0x3BC000,0x400000)`（256 × 1KB）
- `NR_BUFFERS` **由内存地图派生**（`include/linux/fs.h`），不再手工挑选；
  `buffer_init()` 装不下即 `panic`（不再静默重叠）
- `mm/memcheck.c` + `mem_check()`：启动即校验（内核 `_end` 上限、缓存 vs 堆/栈/子进程栈、
  缓存条数、用户区有序、页分配器上限），不一致 → 打印完整内存地图并 panic
- `get_free_page()` 加池上限守卫（M3 已改为「耗尽返回 0」）；`lib/malloc.c` 上界从
  `memory_end-0x200000`（M3 前的旧上界）改为
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
→ **M3 已关闭**：见下文 M3 小节，回归场景 16 `cow` 验证「子进程的写对父进程不可见」。

## M2 — 用户态能力补齐 ✅ 已完成（M2-2 有一处架构性遗留，转 M3）

| 项 | 状态 | 内容 | 关键文件 |
|----|------|------|----------|
| M2-1 | ✅ | **自定义信号处理器 + `sigreturn`**：`signal()` 接受 Ring3 函数指针，投递时在用户栈上构造 sigframe（信号号 + 返回地址 + 80 字节上下文），`do_signal` 在 `ret_from_sys_call` 路径投递；`sigreturn`（syscall 67）校验 magic/retaddr/cs/ss/esp 后精确恢复上下文 | `kernel/process.c`、`kernel/asm.s`、`boot/head.s`、`include/signal.h`、`include/memlayout.h` |
| M2-2 | ⚠️ 部分 | **Ring3 用户态 shell（`/bin/sh`）**：内建 cd/pwd/exit/help，其余按 `/bin/<name>` fork+execve+waitpid；Ring3 标准输入（`read(0,…)`）已通。**遗留**：让子程序*成功* execve 会写坏父 shell 的代码页（见下），故只把 `/bin/sh` 作为救援入口的替代品提供，回归场景默认不跑 | `user/sh.c`、`kernel/main.c`、`init/shell.c` |
| M2-3 | ✅ | **定时回写**：`do_timer` 置标志 + 唤醒专用回写任务（`kernel/sync.c`，占最后一个任务槽），每 5s `sync_dev()`；`sync_dev` 返回真实写入块数，只在 >0 时打印。异常退出不再丢数据 | `kernel/sched.c`、`kernel/sync.c`、`fs/buffer.c` |

**M2-2 的架构性遗留（M3 的入口）**：`execve` 把 ELF 段按 vaddr 直接写进**恒等映射的物理页**。
内核态 shell `exec /bin/x` 没事（父进程是内核），但用户态 shell fork 出子进程后让子进程
execve，子进程就会把新镜像写进 0x200000（M3 前的固定物理地址）—— 那正是父 shell 正在执行的代码页。
现象：父 shell 恢复执行时 EIP 落在指令中间、CPL=3 空转（QEMU `info registers` 可见），
串口输出恰好断在子程序最后一行。这不是 sh 的 bug，而是 M1 遗留（代码段/堆父子共享）
在用户态的第一处硬伤：**只有每进程独立地址空间才能修**。

**M2 新增回归**：场景 14 `autosync`（两阶段：touch 后空转 8s 不调用 sync → 冷启动 `ls` 必须看到文件）；
`TEST_USERSH=1` 时另跑 Ring3 shell 场景（M3 后转正）。

## M3 — 架构级内存模型 ✅ 已完成

放弃「单页表恒等映射 0–4MB + 固定用户地址」，改为**每进程独立页目录 + 帧分配器按页授权**。

**新模型**：

```
内核恒等映射 0..16MB（PDE[0..3]，4 张内核页表，全 supervisor-only，所有进程共享）
  + 页目录 0x100000、内核页表 0x101000..0x105000、页帧池 0x105000 起、缓冲区缓存窗口、mem_map 末尾

用户地址空间（每进程一份页目录，只占 PDE[32] = 0x08000000..0x08400000）
  0x08000000 程序镜像（ELF LOAD 段按页装入新帧）
  0x08100000 堆（首次访问才分配）
  0x08200000 保护空洞（访问 = SIGSEGV）
  0x08300000 栈下界（向下按需增长）
  0x083FF000 栈顶尾页：argc/argv/sigreturn stub
```

| 项 | 状态 | 内容 |
|----|------|------|
| M3-1 | ✅ | `alloc_user_pgdir()`（页目录 + 一张用户页表）/`free_user_space()`/`alloc_user_page()`/`map_user_page()`；内核页表全局共享，`get_free_page()` 上限取消，池 = mem_map 里所有空闲页（16MB 下约 3700 页） |
| M3-2 | ✅ | `execve` 在**新地址空间**里按 ELF LOAD 段建页（内核经恒等映射填帧内容），纯 BSS 页不预先建 → 首次访问触发 `do_no_page` 按需分配；非法区域访问仍 SIGSEGV |
| M3-3 | ✅ | `copy_page_tables()` 让父子共享只读页（PTE 打上软件 COW 位 0x200），`un_wp_page()` 在写缺页时复制；fork 不再复制用户栈，也不再需要「子进程栈区」 |
| M3-4 | ✅ | fork/exit/execve 适配（exit 先切回内核页目录再释放地址空间）；`boot/head.s` 建 4 张内核页表把恒等映射扩到 16MB；QEMU 统一 `-m 16M` |
| M3-5 | ✅ | 新回归场景 16 `cow`（子进程写对父进程不可见）、17 `demand`（384KB BSS/堆首访为 0）、18 `oom`（池耗尽只杀肇事进程，内核存活）；`memstat` 暴露空闲页/缺页/COW/OOM 计数 |

**M3 顺带修掉的两个老 bug**：

1. `mm/page.s` 的 `page_fault` **从不丢弃 CPU 压入的错误码**：以前 `do_no_page` 从不返回
   （一律杀进程），所以 iret 少弹一个字的错误永远暴露不出来；一旦开始按需调页/COW 就会
   iret 到错误码上然后 #GP 死循环。现在返回前 `add $4,%esp`。
2. `mm/page.s` 读取错误码的偏移写成了 `0x34`（两次 push 之后应为 `0x38`），
   传给 `do_no_page` 的「错误码」其实是 eip。

**M3 让 M2-2 的遗留闭环**：Ring3 `/bin/sh` 现在可以真正 fork+execve 子程序
（回归场景 14 `usersh` 默认跑），因为子进程的 execve 不再写进父进程正在执行的物理页。


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
> **⚠️ M3 已被取代**：下面这套「单页表 + `grant_user_pages` 改 U/S 位」的做法在 M3 整体删除，
> 现在是「内核恒等映射全 0x03 + 每进程私有页表里的 PTE=0x07」。保留这段是因为其中的
> PTE 标志教训（0x06 缺 P 位）和越权处理的行为仍然成立。
- 页表 0 全部 PTE 由 0x07 改 **0x03（P+RW，无 U/S）** —— 0-4MB 默认内核专属
  （⚠️ 教训：0x06=0b110 没有 P 位，会整页 not-present，曾致启动即崩；
   内核页标志是 0x03，不是 0x06）
- `grant_user_pages(from,size)`（mm/memory.c，M3 已删除）：按页把 PTE 置 U/S 位
  （|= 4 → 0x07），并重载 CR3 刷 TLB
- 授权区域（M3 前）：启动时（main.c）堆+栈 [0x310000, 0x400000)；execve 时按
  ELF 段尾授权程序区 [0x200000, max_end)；run_user_program 同
- **效果**：Ring3 只能访问程序/堆/栈页；内核页（含 buffer cache、
  任务页、页表）用户不可访问；越权访问 → page fault → 终止肇事进程
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
qemu-system-i386 -fda Image -hda minix.img -m 16M -boot a
python3 scripts/qemu-test.py --image Image --hda minix.img --keys $'cmd\n'
make test                   # 一键回归（scripts/regress.sh，18 个场景断言）
make check                  # 静态校验：内存地图 + 文档一致性 + lint 反向自测
make check-layout           # 只校验内存地图（含 _end 未越界）
make check-docs             # 只校验文档里引用的布局常量/场景数与源码一致
make check-docs-selftest    # 反向测试 check-docs（8 个坏样本必须被拦下）
# 注意：QEMU writeback 会把测试中的脏块刷进 minix.img —— 测试前 rm -f minix.img && make minix.img
#       （make test 每个场景自动重建干净盘；定时回写已实现，场景 15 专门验证它）

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
4. **自定义信号处理器已实现**（M2-1）—— 默认动作 + 用户态 handler（`sigreturn` 精确恢复上下文）
5. **目录已支持扩容** —— >64 项时自动分配单间接块（`ensure_dir_block`；
   `rmdir` 释放全部目录 zone）。多级间接（>519 项）不支持。
6. **用户栈**：顶 `USER_STACK_TOP`(0x083FF000)，crt 从 `USER_ARGC_ADDR`/`USER_ARGV_PTR_ADDR`
   读 argc/argv（execve 约定）；栈页按需增长
7. **用户堆**：`[USER_HEAP_START, USER_HEAP_END)` = `[0x08100000,0x08200000)`（lib.c，1MB）
8. **程序链接地址**：`USER_PROG_START`(0x08000000)，固定无 PIE；
   `user/lib.h` 里有链接地址断言（链错地址 = 编译失败）
9. **fork 已是 POSIX 语义**（M3）：用户页父子共享只读 + 写时复制，子进程的写父进程看不见；
   地址空间彼此独立，子进程 execve 不再影响父进程
10. **printf %s 需 NUL 终止**（read 后手动补）

## 关键文件地图

| 文件 | 作用 |
|------|------|
| `include/memlayout.h` | ★ 内存地图唯一权威（内核+用户态共用） |
| `include/linux/memmap.h` | 内核侧边界 + `STATIC_ASSERT` 布局自检 |
| `include/memlayout.inc` | 汇编侧 `.equ` 镜像（与上面同步校验） |
| `mm/memcheck.c` | `mem_check()`：启动自检，不一致即 panic + 地图 dump |
| `scripts/check-layout.py` | 无编译器的静态地图校验（`make check-layout`） |
| `scripts/check-docs.py` | 文档 vs 内存地图一致性校验（`make check-docs`）；`scripts/check-docs-selftest.sh` 反向测试它 |
| `kernel/sys.c` | 系统调用实现（含 sys_execve） |
| `kernel/process.c` | fork/waitpid/exit/do_signal |
| `boot/head.s` | system_call 入口（syscall_cpl 检测）、sys_call_table、setup_paging（4 张内核页表恒等映射 0–16MB） |
| `mm/memory.c` | ★ 页帧分配器 + 每进程页目录 + 按需调页 + COW（`copy_page_tables`/`un_wp_page`）+ `mm_report()` |
| `mm/page.s` | page_fault 处理（do_no_page；记得它也负责丢弃错误码） |
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
  用户堆 `[0x310000,0x3FE000)`（M3 前布局）重叠；Ring0 无视 PTE 的 U/S 位 → 用户 malloc 的字节
  就是文件系统块缓冲，**写坏文件系统而毫无提示**。修复：缓存条数由内存地图派生 +
  静态断言 + `mem_check()` 启动自检 + `bigalloc` 回归用例
- **fork 子进程用户栈落进缓存区**（M1）：旧锚点 `0x3E0000` 在缓存范围内，
  Ring3 fork 会把用户栈副本写穿文件系统缓存。修复：独立子进程栈区
  `(0x300000,0x340000]` + 上限检查（放不下则 fork 返回 -1）
- **`lib/malloc.c` 上界错误**（M1）：旧上界 `memory_end - 0x200000` 会把内核 bump 堆
  伸进页分配器池（0x18A000 以上）；现为 `KERNEL_HEAP_END`
- **恒等映射下 `get_free_page` 能撞用户区**（M1）：页分配器从 0x100000 顺序扫描，
  不设上限会走到用户程序镜像所在的物理页；现在越界即 `panic`
