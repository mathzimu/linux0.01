#!/usr/bin/env bash
# 一键回归测试：为每个场景准备干净的 MINIX 盘 → 跑核心命令 → 断言输出。
# 用法: make test   (或 scripts/regress.sh)
# QEMU writeback 会污染 minix.img，故每个场景开始前都重新生成干净盘。
# 每个场景的串口日志保存在 ./test-logs/<name>.serial，便于排查失败。
set -u
cd "$(dirname "$0")/.."

PASS=0
FAIL=0
LOGDIR=${LOGDIR:-test-logs}
mkdir -p "$LOGDIR"

# Which disk to attach as the first IDE drive.  Default: the MINIX test
# image every scenario prepares.  An empty QEMU_HDA boots with no disk at
# all, which is what the "no root filesystem" scenario needs:
#
#     QEMU_HDA= run_case nodisk "$BASE" 'help\nexit\n' 'Warning: ...'
#
# It expands to two words on purpose (or none at all), so it must stay
# unquoted at the call site.
hda_args() {
    if [ -n "${QEMU_HDA-minix.img}" ]; then
        printf '%s' "--hda ${QEMU_HDA-minix.img}"
    fi
}

# check_no_reboot <name> <output> <expected-boots>
#
#   A reboot is not a pass.  A triple fault restarts the machine, and the
#   freshly booted kernel shell then consumes whatever keys the harness is
#   still typing: it prints "$" prompts, it accepts `exit` ("Goodbye."), it
#   even prints file contents - so every presence-based assertion can still
#   match and a *crashed* scenario looked green.  Counting the boot banner
#   in the run's own output is what tells the two apart.  (Found the hard
#   way: `cat file | cat` in Ring3 reboots the machine, and the pipeline
#   scenario was passing after the reboot.)
#
#   Counting banners ALONE is not enough either: a reset that happens
#   *before* the first banner - the kernel faults in early init, while it
#   is still mounting the root filesystem - leaves the banner count at 1
#   and every assertion still matches, because the retry boots all the way
#   to the shell.  That is exactly how "no root filesystem -> reset loop"
#   stayed invisible while the suite was green, so the early-init marker
#   (buffer_init's one line per boot) is counted as well.  It must never
#   appear more often than the expected number of boots.
check_no_reboot() {
    local name="$1" out="$2" want="$3" boots early
    boots=$(printf '%s' "$out" | grep -cF "Minimal Linux 0.01 Equivalent Kernel")
    early=$(printf '%s' "$out" | grep -cF "buffer cache: ")
    if [ "$boots" -gt "$want" ] || [ "$early" -gt "$want" ]; then
        echo "FAIL [$name]  machine rebooted ($boots boot banners, $early early-init markers, expected $want): the kernel faulted - see $LOGDIR/$name.serial"
        gha_error "FAIL [$name] machine rebooted ($boots boot banners, $early early-init markers): the kernel faulted"
        echo "---- tail $LOGDIR/$name.serial ----"
        tail -15 "$LOGDIR/$name.serial" 2>/dev/null
        echo "--------------------------------"
        FAIL=$((FAIL+1))
        return 1
    fi
    return 0
}

# run_case <name> <prep-cmd> <keys> <needle...>
#   prep-cmd : 准备干净盘的命令（须生成 minix.img）
#   keys     : 注入的按键（支持字面 \n 或真换行）
#   needle...: 断言——serial 输出须包含每个子串（出现才算过）
#   环境变量：QEMU_MEM（默认 16M）、QEMU_MIN_WAIT（默认 0，重压力场景需要更长
#   的总窗口——驱动默认的静默/总时长上限只有 10 秒）、QEMU_TAIL。
run_case() {
    local name="$1" prep="$2" keys="$3"
    shift 3
    local out
    if is_heavy_skipped "$name"; then return 0; fi
    if ! eval "$prep" >/dev/null 2>&1; then
        echo "FAIL [$name]  setup failed: $prep"
        FAIL=$((FAIL+1)); return 1
    fi
    out=$(python3 scripts/qemu-test.py --image Image $(hda_args) \
             --hold "${TEST_HOLD:-30}" \
             --tail "${QEMU_TAIL:-${TEST_TAIL:-1.5}}" \
             --min-wait "${QEMU_MIN_WAIT:-0}" \
             --mem "${QEMU_MEM:-16M}" \
             --type-delay "${TEST_TYPE_DELAY:-0.5}" \
             --extra "${TEST_EXTRA:-}" \
             --keys "$keys" 2>/dev/null)
    printf '%s' "$out" > "$LOGDIR/$name.serial"
    if ! check_no_reboot "$name" "$out" 1; then return 1; fi
    for needle in "$@"; do
        if ! printf '%s' "$out" | grep -qF "$needle"; then
            echo "FAIL [$name]  missing: \"$needle\"  (see $LOGDIR/$name.serial)"
            gha_error "FAIL [$name] missing: \"$needle\""
            echo "---- tail $LOGDIR/$name.serial ----"
            tail -15 "$LOGDIR/$name.serial" 2>/dev/null
            echo "--------------------------------"
            FAIL=$((FAIL+1)); return 1
        fi
    done
    echo "PASS [$name]"
    PASS=$((PASS+1))
}

# run_case2 <name> <prep-cmd> <keys1> <min-wait> <keys2> <needle...>
#   两阶段用例：共用同一张 minix.img。第一阶段（可等待周期性事件）结束后
#   直接关机，第二阶段冷启动再看盘上的内容——以此证明数据真的落到磁盘，
#   而不是停留在缓冲区缓存里。
run_case2() {
    local name="$1" prep="$2" keys1="$3" minwait="$4" keys2="$5"
    shift 5
    local out
    if is_heavy_skipped "$name"; then return 0; fi
    if ! eval "$prep" >/dev/null 2>&1; then
        echo "FAIL [$name]  setup failed: $prep"
        FAIL=$((FAIL+1)); return 1
    fi
    out=$(python3 scripts/qemu-test.py --image Image $(hda_args) \
             --hold "${TEST_HOLD:-30}" --tail "${TEST_TAIL:-1.5}" \
             --mem "${QEMU_MEM:-16M}" \
             --type-delay "${TEST_TYPE_DELAY:-0.5}" \
             --min-wait "$minwait" --keys "$keys1" 2>/dev/null)
    printf '%s' "$out" > "$LOGDIR/$name.1.serial"
    if ! check_no_reboot "$name.1" "$out" 1; then return 1; fi
    out="$out
$(python3 scripts/qemu-test.py --image Image $(hda_args) \
             --hold "${TEST_HOLD:-30}" --tail "${TEST_TAIL:-1.5}" \
             --mem "${QEMU_MEM:-16M}" \
             --type-delay "${TEST_TYPE_DELAY:-0.5}" \
             --keys "$keys2" 2>/dev/null)"
    printf '%s' "$out" > "$LOGDIR/$name.serial"
    if ! check_no_reboot "$name" "$out" 2; then return 1; fi
    for needle in "$@"; do
        if ! printf '%s' "$out" | grep -qF "$needle"; then
            echo "FAIL [$name]  missing: \"$needle\"  (see $LOGDIR/$name.serial)"
            gha_error "FAIL [$name] missing: \"$needle\""
            echo "---- tail $LOGDIR/$name.serial ----"
            tail -15 "$LOGDIR/$name.serial" 2>/dev/null
            echo "--------------------------------"
            FAIL=$((FAIL+1)); return 1
        fi
    done
    echo "PASS [$name]"
    PASS=$((PASS+1))
}

# 重场景开关：TEST_SKIP_HEAVY=1 时跳过最慢的几个（内存压力、双阶段等待），
# 给 PR 用快集；推送到 main 时跑全集。全集在本机（Docker，同样是 TCG 无 KVM）
# 实测约 10.5 分钟，而 CI 作业超时是 20 分钟——大部分时间其实花在"敲键盘"上
# （harness 每字符默认等 TEST_TYPE_DELAY=0.5 秒）。不要靠压低打字速度省时间：
# 低于 ~0.2 秒实测会丢键（8042 只有一字节缓冲，guest 跟不上就丢整条命令），
# 所以宁可拆成两集。
HEAVY_CASES=" autosync oom evict swap "

# CI 可观测性：GitHub Actions 会把 `::error::` 开头的行变成**注解**（annotations），
# 那是公开可读的——而作业日志接口需要 admin 权限。CI 红的时候，如果失败场景名
# 只在日志里，本地没权限的人就只能干看着（这次就是这样：CI 从 M3 起一直是红的，
# 只知道 `make test` 失败，不知道是哪一条）。所以失败路径同时发注解 + 写 step
# summary，运行页面上直接能看到 `FAIL [name] missing "needle"`。
gha_error() {
    if [ -n "${GITHUB_ACTIONS:-}" ]; then
        echo "::error title=regression failure::$*"
    fi
}

gha_note() {
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        echo "$*" >> "$GITHUB_STEP_SUMMARY"
    fi
}

is_heavy_skipped() {
    if [ "${TEST_SKIP_HEAVY:-0}" != "1" ]; then
        return 1
    fi
    case " $HEAVY_CASES " in
        *" $1 "*) echo "SKIP [$1] (heavy; TEST_SKIP_HEAVY=1)"; return 0 ;;
    esac
    return 1
}

BASE='rm -f minix.img'

# 场景 1: execve + argv + 退出码
run_case hello  'rm -f minix.img && make prog NAME=hello' 'exec /bin/hello a b\n' \
    'hello from user program: argc=3' 'exec: child 1 exit_code=42'

# 场景 2: 管道（fork + pipe 通信）
run_case pipe   'rm -f minix.img && make prog NAME=pipedemo' 'exec /bin/pipedemo\n' \
    'hello from child via pipe!' 'parent read after EOF: 0'

# 场景 3: chdir + 相对路径 + cat
run_case chdir  "$BASE && make minix.img" 'cd /docs\ncat note.txt\n' \
    'A file inside a subdirectory.'

# 场景 4: 硬链接 + stat
run_case link   "$BASE && make minix.img" 'ln /hello.txt /h\nstat /hello.txt\n' \
    'nlink=2'

# 场景 5: fork + waitpid 回收
run_case spawn  "$BASE && make minix.img" 'spawn\n' \
    'spawn done' 'waitpid(1) = 1'

# 场景 6: 信号（kill → 默认动作 SIGINT 终止，exit_code=128+2）
run_case sig    "$BASE && make minix.img" 'sig\n' \
    'exit_code=130'

# 场景 7: 多系统调用（stat / uid / umask / uname / fcntl）
run_case sysdemo 'rm -f minix.img && make prog NAME=sysdemo' 'exec /bin/sysdemo\n' \
    'uname: linux' 'fcntl F_DUPFD: fd=4 dup=5'

# 场景 8: 内存隔离（Ring3 访问内核页 → SIGSEGV 终止肇事进程，不 panic 内核）
run_case bad    'rm -f minix.img && make prog NAME=bad' 'exec /bin/bad\n' \
    'PAGE FAULT' 'exec: child 1 exit_code=139'

# 场景 9: 目录扩容（>64 项自动进单间接块）
run_case bigdir 'rm -f minix.img && make prog NAME=bigdir' 'exec /bin/bigdir\n' \
    'created 70 files under /big' 'readdir /big = 72 entries'

# 场景 10: 基础应用程序（cat / wc / grep / cp / touch）
APPS='rm -f minix.img && make user/cat.elf user/wc.elf user/grep.elf user/cp.elf user/touch.elf && tools/mkminix minix.img user/cat.elf:cat user/wc.elf:wc user/grep.elf:grep user/cp.elf:cp user/touch.elf:touch'
run_case apps "$APPS" 'exec /bin/cat /readme.txt\nexec /bin/wc /readme.txt\nexec /bin/grep kernel /readme.txt\nexec /bin/cp /hello.txt /c2.txt\nexec /bin/touch /n.txt\n' \
    'Minimal Linux 0.01 equivalent kernel.' \
    '3 19 129 /readme.txt' \
    'cp: /hello.txt -> /c2.txt done'

# 场景 11: 用户堆与缓冲区缓存不得重叠（M1 回归）
#   NR_BUFFERS = 512 时缓存落在 ~0x370000，与用户堆共用物理页：
#   用户把数据写进文件系统的块缓冲，或反之，都不报错。此用例把堆填满后
#   重读文件并逐字节校验堆内容。
run_case bigalloc 'rm -f minix.img && make prog NAME=bigalloc' 'exec /bin/bigalloc\n' \
    'bigalloc: heap vs buffer cache' \
    'bigalloc: PASS (heap and buffer cache are disjoint)'

# 场景 12: 启动时内存地图自检通过（不一致会 panic 而不是继续跑）
#   顺带断言缓冲区缓存的启动日志，它暴露了缓存的真实位置与数量。
run_case memcheck "$BASE && make minix.img" 'ls\n' \
    'buffer cache:' 'hello.txt'

# 场景 13: 自定义信号处理器 + sigreturn（M2-1）
#   handler 返回后必须精确回到被中断的指令继续执行。
run_case sigdemo 'rm -f minix.img && make prog NAME=sigdemo' 'exec /bin/sigdemo\n' \
    'sigdemo: handler ran (sig=14, hits=1)' \
    'sigdemo: after alarm: hits=1 last_sig=14' \
    'sigdemo: PASS'

# 场景 14: Ring3 用户态 shell（M2-2，M3 落地后才真正可用）
#   /bin/sh 完全跑在用户态：它自己 read 键盘、自己 fork+execve 子程序。
#   M2 时这里只能验证失败路径：子进程一旦**成功** execve，就会把新镜像写进
#   恒等映射的固定物理页，而那正是父 shell 正在执行的代码页，父进程随即跑飞。
#   M3 给每个进程独立地址空间之后，这条路才成立（也是 M3 的验收点之一）。
SH_PREP='rm -f minix.img && make user/sh.elf user/hello.elf && tools/mkminix minix.img user/sh.elf:sh user/hello.elf:hello'
run_case usersh "$SH_PREP" 'exec /bin/sh\nhello a b\nexit\n' \
    'sh: user-mode shell (Ring3)' \
    'hello from user program: argc=3' \
    'sh: hello exited with 42'

# 场景 15: 定时回写（M2-3）
#   全程不调用 sync：第一阶段 touch 一个文件后空转 8 秒（>5s 回写周期），
#   第二阶段冷启动 ls 必须看到它——证明脏缓冲是定时任务刷到盘上的。
run_case2 autosync "$BASE && make minix.img" 'touch /syncmark\n' 8 'ls\n' \
    'syncmark' \
    'sync:'
# 场景 16: 写时复制（M3）—— fork 后子进程的写必须对父进程不可见
#   M1/M2 时子进程与父进程共享代码段/堆，子进程写进去的值父进程直接看得见；
#   现在两侧共享只读页，首次写各自复制。父进程四个变量的值必须原封不动。
run_case cow "$BASE && make prog NAME=cowtest" 'exec /bin/cowtest\n' \
    'cowtest: child PASS' \
    'cowtest: parent after child exit: global=100 bss=0 heap=111 stack=7' \
    'cowtest: PASS (fork gave the child private pages)'

# 场景 17: 按需调页（M3）—— .bss 与堆的页在第一次访问时才分配
#   384KB 未触碰区域读出来必须是 0（新分配的零页，而不是残留数据），写入后
#   再读回必须一致（页确实建好了）。memstat 顺带验证计数器与空闲页统计。
run_case demand "$BASE && make prog NAME=demandtest" \
    'memstat\nexec /bin/demandtest\nmemstat\n' \
    'demandtest: 384KB touched, 0 mismatches' \
    'demandtest: PASS (BSS and heap arrived zeroed, on demand)' \
    'mem: ' \
    'COW breaks'

# 场景 18: 内存耗尽（M3/B3）—— 分配不到页是正常情况，不是内核 panic
#   用 4MB 机器（约 695 个空闲页）+ 6 个子进程各占 768KB **不可回收**的私有脏页
#   （写 0xAA；写成 0 会被 B3 的回收器当零页收走，见场景 22），合计 4.6MB，
#   必然装不下。缺页处理打印 OOM 并只杀肇事进程：子进程成批倒下，父进程跑完
#   全程（done）并返回 0，内核与文件系统照常工作（后面的 ls 能列出 hello.txt）。
#   ⚠️ 这条曾经跑在 16MB 上要 40 个子进程、耗时几十秒——CI 的 runner 更慢，
#   总窗口一到就被截断，于是 CI 红而本地绿。现在改用小内存 + 显式窗口，
#   工作量小了十几倍，任何 runner 上都稳。
QEMU_MEM=4M QEMU_MIN_WAIT=180 run_case oom "$BASE && make prog NAME=oomtest" \
    'exec /bin/oomtest\nls\n' \
    'children are holding memory' \
    'PAGE FAULT: out of memory for pid=' \
    'oomtest: done' \
    'hello.txt'

# 场景 19: 文件权限模型（M4）—— mode/uid/gid 终于会被检查
#   i_mode/i_uid/i_gid 存了很久却从没被用过：系统里只有 uid 0，"谁能做什么"
#   这个问题不出现。permtest 用两个 setuid(1000) 的子进程验证两个方向：
#   越权操作全部被拒、root 放宽权限后同样的操作又能成功，且自己创建的文件
#   属于自己（uid=1000）。任一方向反了都会打印 FAILED 并让最终断言失败。
run_case perm "$BASE && make prog NAME=permtest" 'exec /bin/permtest\n' \
    'permtest: --- phase A: unprivileged child (should be denied) ---' \
    'permtest:   denied  open(secret, O_RDONLY)           ok' \
    'permtest:   denied  creat(/ptest/newfile)            ok' \
    'permtest: child B: /ptest/owned uid=1000 (want 1000)' \
    'permtest: PASS (permissions enforced in both directions)'

# 场景 20: 管道与重定向（C1）—— Ring3 shell 用 pipe/dup2 连起两个进程
#   `echo` / `<` / `>` / `>>` / `|` 全部走系统调用：pipe(42) 与 dup2(63) 早就
#   实现了，但一直没人用它们把两个进程接起来。为了让 `>` 真的能改变 fd 1 的
#   去向，内核这一轮把控制台也放进了 fd 表（fd 0/1/2 = tty_file），不再是
#   sys_write 里的硬编码分支。
#   数值是算得出来的：alpha beta gamma\n = 17B，delta\n = 6B，追加后 23B/2 行/4 词。
QEMU_MIN_WAIT=60 run_case shpipe \
    'rm -f minix.img && make user/sh.elf user/cat.elf user/wc.elf user/hello.elf && tools/mkminix minix.img user/sh.elf:sh user/cat.elf:cat user/wc.elf:wc user/hello.elf:hello' \
    'exec /bin/sh\necho alpha beta gamma > /p.txt\necho delta > /q.txt\ncat /q.txt >> /p.txt\ncat /p.txt\nwc < /p.txt\ncat /p.txt | wc\ncat /q.txt | wc\nexit\n' \
    'sh: supports < > >> | and &' \
    'alpha beta gamma' \
    'delta' \
    '2 4 23 -' \
    '1 1 6 -' \
    'exec: child 1 exit_code=0'

# 场景 21: 中断驱动磁盘（B2）
#   IRQ14 此前被从片掩码整片屏蔽，hd_interrupt_handler 是死代码，读写全靠
#   轮询。现在发命令的任务睡眠、由 IRQ14 唤醒，写路径要经得起"睡在驱动里"
#   带来的并发（定时回写任务与用户任务同时做磁盘 I/O）。这里写一遍、读回来
#   校验内容，再看 memstat 的 IRQ14 计数——标签本身也证明计数器接上了。
run_case diskio "$BASE && make minix.img" 'wtest\ncat /hello.txt\nmemstat\n' \
    'wtest: wrote 37 bytes to /hello.txt' \
    'Minimal Linux 0.01 write path works!' \
    'disk interrupts (IRQ14)'

# 场景 22: 内存压力下的页回收（B3）
#   故意在 4MB（约 695 个空闲页）下要 ~770 页：父进程 + 4 个子进程各占
#   512KB 零 BSS + 128KB 私有堆。零页与只读正文页可回收（丢弃后按需重建/
#   回读），私有脏页不可回收——所以系统必须靠回收撑过去，而且每个进程最后
#   都要能读回自己的数据。断言里带上内核的 `evict:` 轨迹行：回收真的发生了。
#   子进程用 alarm(3) 自己退场（期望 rc=142）；若被 OOM 杀掉会是 139，
#   测试会打印 FAIL。没有回收时这里必然失败。
QEMU_MEM=4M QEMU_MIN_WAIT=150 run_case evict "$BASE && make prog NAME=evicttest" \
    'exec /bin/evicttest\n' \
    'evicttest: forked 2 children' \
    'evicttest: 2 children survived to their own timeout' \
    'evict: ' \
    'evicttest: PASS (integrity kept across eviction)' \
    'exec: child 1 exit_code=0'

# 场景 23: 信号投递时机（B2'）—— 不进内核的进程也必须能被信号带走
#   内核原先只在**系统调用返回**时投递信号，于是纯计算循环里的进程既杀不掉、
#   也等不到 alarm（B3 的内存压力测试正是被这一点卡住的：卡在缺页循环里的
#   子进程收不到任何信号）。现在定时器中断返回路径上也会投递。
#   期望：设置 alarm(1) 后进入无系统调用的死循环，约 1 秒后以 142
#   （128+SIGALRM）退出；修复前这里会永远挂住、连 exit_code 都不会出现。
run_case spinkill "$BASE && make prog NAME=spintest" 'exec /bin/spintest\nls\n' \
    'installing alarm(1), then spinning with no syscalls' \
    'exec: child 1 exit_code=142' \
    'hello.txt'

# 场景 24: 匿名页换出/换入（B4）
#   4MB（约 695 帧）下要 768 页**私有脏页**：3 个子进程各填 768KB 堆后持有
#   （alarm 自行退场，142=128+SIGALRM；若被 OOM 杀会是 139，测试直接 FAIL），
#   父进程在它们持有期间再填自己的 768KB。零页/正文页回收不够用时，B4 把脏页
#   写到镜像末尾的裸 swap 区、缺页时读回——所有数据必须逐字节一致。
#   重场景：磁盘 I/O 密集，需要长窗口。
QEMU_MEM=4M QEMU_MIN_WAIT=180 run_case swap "$BASE && make prog NAME=swaptest" \
    'exec /bin/swaptest\nmemstat\n' \
    'swaptest: forked 3 children' \
    'swaptest: PASS (3 children, data intact across swap)' \
    'pages swapped out' \
    'exec: child 1 exit_code=0'

# 场景 25: 信号屏蔽与 sigsuspend（B5）
#   阻塞的信号不能被丢掉：它留在 pending 集合里，解除阻塞的**那一次**
#   sigprocmask 返回时立刻投递（不需要再发一次信号）。sigsuspend 则要
#   原子地"换掩码 + 睡觉"——POSIX 要求唤醒它的处理器在 sigsuspend 返回**之前**
#   跑完，所以临时掩码必须一直生效到进入 handler 为止（内核在这里把旧掩码的
#   恢复推迟到 do_signal 里）。这条场景同时钉住一个老 bug：从 handler 返回时
#   sys_sigreturn 曾把被中断系统调用的返回值清成 0（sigdemo 只检查 pause()，
#   返回值没人看，所以一直没暴露）。
run_case sigblock "$BASE && make prog NAME=sigblock" 'exec /bin/sigblock\n' \
    'sigblock: raised SIGUSR1 while blocked: hits=0' \
    'sigblock: after unblock: hits=1 last_sig=10' \
    'sigblock: sigsuspend returned -1, hits=2 last_sig=10' \
    'sigblock: SIGKILL is still unblockable (hits=2)' \
    'sigblock: PASS'

# 场景 26: sigaction（B5 第 3 步）—— 持久处理器 + sa_mask
#   signal() 的处理器在运行前会被重置成 SIG_DFL（所以程序必须在处理器里重新装一次，
#   见场景 25 的测试程序）；sigaction() 装的处理器**保持有效**。POSIX 还要求"正在
#   处理的信号在处理器执行期间被屏蔽"，否则处理器会被自己递归打断。这条场景同时验证
#   自阻塞、sa_mask 生效、以及处理器返回后 pending 的信号按序补投。
run_case sigaction "$BASE && make prog NAME=sigactiontest" 'exec /bin/sigactiontest\n' \
    'sigaction: in handler: usr2_hits=0 (want 0)' \
    'sigaction: after one raise: usr1=2 usr2=1' \
    'sigaction: after a second raise: usr1=3 (want 3)' \
    'sigaction: PASS'

# 场景 27: sleep() 与 select()（B5.7）—— 内核第一次有"到期唤醒"
#   在此之前内核没有"过一会儿叫醒我"的手段：alarm() 是靠**投递信号**叫醒任务的，
#   而"超时"不是信号。sleep_deadline[]（按 pid 一个 jiffies 截止时间，由 do_timer 检查）
#   补上了这一层，两个系统调用都建在它上面。这条场景验证：sleep 真的睡够、select 在
#   无人输入时按超时返回 0 且清空集合、控制台可写所以 select(fd1) 立刻返回 1、
#   以及信号能打断 sleep 并报告剩余秒数（POSIX 语义）。
run_case seltest "$BASE && make prog NAME=seltest" 'exec /bin/seltest\n' \
    'seltest: sleep(2) -> 0' \
    'seltest: select(fd0, 1s) -> 0, rfds=0' \
    'seltest: select(fd1, no timeout) -> 1, wfds=2' \
    'seltest: sleep(10) with alarm(1) -> 9 left, hits=1' \
    'seltest: PASS'

# 场景 28: Shell 的后台任务与 wait（B5.8）
#   `cmd &` 用**子 shell**实现：父 shell fork 之后立刻回到提示符，子进程照常调用
#   run_pipeline()——所以 run_pipeline/run_one 完全不需要知道"后台"这回事。
#   没有作业控制（没有 SIGSTOP/SIGCONT，也没有和终端绑定的进程组），所以只能跟踪 pid、
#   每次打印提示符前用 WNOHANG 收尸、`wait` 阻塞等待。`sleep` 现在是 shell 内建命令，
#   它直接调用内核的新系统调用 71，正好把 B5.7 的能力暴露给使用者。
#   注：harness 只断言"这些行出现过"，不断言先后顺序；因此这条场景检查的是功能
#   （后台任务被记录、能收尸、wait 不吞前台状态、sleep 内建可用），而不是时序。
SH_PREP2='rm -f minix.img && make user/sh.elf user/hello.elf && tools/mkminix minix.img user/sh.elf:sh user/hello.elf:hello'
QEMU_MIN_WAIT=40 run_case shbg "$SH_PREP2" \
    'exec /bin/sh\nsleep 1\nsleep 2 &\necho foreground ran\nwait\nexit\n' \
    'sh: slept 1 s, 0 left' \
    'sh: [2] running in background' \
    'foreground ran' \
    'sh: [2] done (status 0)' \
    'exec: child 1 exit_code=0'

# 场景 29: 间接块大文件（文件系统）
#   /big.txt 是 18KB = 18 个块，而 inode 只有 7 个直接块，所以第 8..18 块必须走
#   i_zone[7] 指向的单级间接块（mkminix 生成时就是这么布置的）。用**重定向**读它并
#   断言精确字节数 18432：任何一个间接块读错，行数/字数/字节数都会变。
#   （单级间接 = 7 + 512 块 ≈ 519KB，已经覆盖整个 1MB 文件系统，所以不需要双级间接。）
#   ⚠️ 别用 `cat /big.txt | wc` 来测这条：一个会话里的第二个管道会触发另一个既有缺陷
#   （见 docs/roadmap.md「已知问题：一个会话里的第二个管道会让内核三重故障」）。
SH_PREP3='rm -f minix.img && make user/sh.elf user/wc.elf && tools/mkminix minix.img user/sh.elf:sh user/wc.elf:wc'
run_case bigfile "$SH_PREP3" 'exec /bin/sh\nwc < /big.txt\nexit\n' \
    '1008 1009 18432 -' \
    'exec: child 1 exit_code=0'

# 场景 30: 同一个程序被并发 exec（inode 缓存加载竞态，P1）
#   `cat f | cat` 曾经必然失败：iget() 在 read_inode()（要睡在磁盘读上）完成之前就把槽位
#   （i_dev/i_num/i_count）暴露出去，第二个并发查找命中它，拿到 i_mode==0 的 inode，于是
#   namei 拒绝穿越 /bin（`namei: cannot traverse ino=7 mode=00`）。
#   shell 管道只是靠运气碰到那个窗口，所以这条场景用 fork N + **每个子进程 execve 同一个**
#   /bin/cat 把它确定性地压出来。窗口要够长：4 个子进程各自按需调页并读文件，实测约 40s。
#   但如果同一台机器上还有别的 QEMU 在跑（并行调试时很常见），60s 会不够、场景会假失败，
#   所以给到 90s。
QEMU_MIN_WAIT=90 run_case execrace \
    'rm -f minix.img && make user/execrace.elf user/cat.elf && tools/mkminix minix.img user/execrace.elf:execrace user/cat.elf:cat' \
    'exec /bin/execrace 4 /bin/cat /hello.txt\n' \
    'execrace: forked 4 children' \
    'execrace: 4/4 children' \
    'execrace: PASS'

# 场景 31: 默认镜像自带一套用户态工具
#   `make minix.img` 现在除了 mkminix 自带的 /bin/hello，还注入
#   /bin/{ls,cat,cp,grep,touch,wc,sh}，所以 `exec /bin/sh` 之后敲 ls/cat/wc/cp
#   是真能跑起来的，而不是 `sh: /bin/ls: cannot execute`。
#   这条场景锁住那个默认值：Makefile 里的注入列表一旦被改坏（例如用 patsubst
#   生成 "path:name"，它只替换第一个 %，名字会变成字面量 '%'），这里立刻红。
run_case userland 'rm -f minix.img && make minix.img' \
    'exec /bin/sh\ncat /hello.txt\nwc < /readme.txt\ncp /hello.txt /c2\ntouch /t3\nls /\nls /docs\nexit\n' \
    'Hello from MINIX v1!' \
    '3 19 129 -' \
    'cp: /hello.txt -> /c2 done' \
    't3' \
    'note.txt' \
    'exec: child 1 exit_code=0'

# 场景 32: 没有根文件系统时也必须进 shell（而不是复位循环）
#   曾经的故障链：main() 先 sys_setup() 后 sched_init()，而读超级块要经过块设备层；
#   hd_lock() 拿到锁时会 sti()，于是 hd_wait_bits() 认为"可以睡"→ 进 schedule()，
#   而那时 task[] 还没初始化（BSS 全 0，槽位被当成 0xffffffff 解引用）：
#     PAGE FAULT: addr=0xffffffff err=0x0 eip=0x10eb4 (= schedule+0xc7)
#   → 三重故障 → CPU 复位。带盘时磁盘读偶尔在睡之前就完成（不睡），所以"一般能起来"；
#   无盘时每次都睡、每次都崩 ⇒ 无限复位。而且复位发生在**第一条启动横幅之前**，
#   横幅计数抓不到它（这就是它长期潜伏、套件仍全绿的原因；check_no_reboot 现在
#   也数 buffer_init 那一行）。这条场景**不挂盘**启动：必须只启动一次、不出现
#   PAGE FAULT、打印找不到根文件系统的警告，并且照样进 shell 接受命令。
QEMU_HDA= run_case nodisk "$BASE" 'help\nexit\n' \
    'Warning: no root filesystem found' \
    "Type 'help' for commands" \
    'Goodbye.'

# 场景 33: 单文件自启动镜像（引导扇区 + setup + 内核 + MINIX FS 合一）
#   `make disk` 生成 linux.img：LBA 0 是引导扇区，随后是 setup 与内核镜像，MINIX 文件系统
#   放在按柱面对齐的 LBA 上（本次 192），而这个基址由 tools/mkdisk 写进引导扇区的参数块、
#   内核从那里读出来——两边不各自硬编码一个偏移，所以镜像和内核不会悄悄错位。
#   这条场景**只挂这一块盘**（QEMU_HDA=linux.img + `-boot c`），验证文件系统真的在硬盘上
#   可用：ls/cat 走 Ring0 shell，exec /bin/sh 进 Ring3 再 ls /bin 与重定向读 /readme.txt。
QEMU_HDA=linux.img TEST_EXTRA='-boot c' \
run_case selfdisk 'rm -f minix.img linux.img disk-fs.img && make disk' \
    'ls\ncat /hello.txt\nexec /bin/sh\nls /bin\nwc < /readme.txt\nexit\nexit\n' \
    'MINIX: root filesystem at LBA' \
    'MINIX: superblock loaded, magic=0x137f' \
    'Hello from MINIX v1!' \
    '3 19 129 -' \
    'user-mode shell (Ring3)' \
    'exec: child 1 exit_code=0'
# `VAR=value func` persists after the function returns in bash, so clear
# them or every later scenario would boot linux.img with -boot c too.
unset QEMU_HDA TEST_EXTRA

# 场景 34: 同一会话里的第二个管道（wake_up 不清队列头 → 回收来的页目录被写零）
#   症状：`exec /bin/sh` 之后连跑两条 `cat /hello.txt | wc`，第二条一敲下去机器立刻三重
#   故障重启，串口里既没有 PAGE FAULT 也没有 panic。用 `-d int,cpu_reset` 才看见：崩在
#   switch_to() 尾部的 `ljmp *0x8(%esp)`，页错误地址是 `_gdt+0x68`（读 GDT 里的 LDT 描述符），
#   紧接着取 `_idt+0x70`（#PF 门）也失败 → #DF → 三重故障。也就是那一刻 CR3 指向的页目录
#   已经不含内核恒等映射。
#   根因在 fs/buffer.c 的 wake_up()：它只把 `(*p)->state` 置回 TASK_RUNNING，**不清队列头**，
#   队列因此一直指着一个已被唤醒、随后退出并被回收的 task_struct。那个 task 页回到
#   get_free_page() 后可能被当成页目录发放，而下一次 wake_up() 就把 0 写进它的偏移 0 ——
#   那正是页目录的 PDE[0]。Linux 0.01 原版是 `(**p).state = 0; *p = NULL;`，缺的就是第二句。
#   同一场景还守着第二个独立缺陷（它表现为第一条管道丢掉 wc 的输出）：sys_open() 在 namei()
#   **之前**就占用 file_table[] 槽位、最后才置 f_count=1，于是并发打开会共用同一个 struct
#   file，execve 会读回另一个进程的 inode（`wc` 那一级实际跑的是 `cat` 的镜像）。两个缺陷都
#   修好后，每条管道都应打印一次 `1 4 21 -`。
QEMU_MIN_WAIT=45 run_case pipe2 'rm -f minix.img && make minix.img' \
    'exec /bin/sh\ncat /hello.txt | wc\ncat /hello.txt | wc\ncat /hello.txt | wc\nexit\n' \
    '1 4 21 -' \
    'Hello from MINIX v1!' \
    'exec: child 1 exit_code=0'

echo
echo "================================"
echo "  $PASS passed, $FAIL failed"
echo "================================"
gha_note "### regression: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    gha_error "$FAIL scenario(s) failed - see the annotations above"
fi
[ "$FAIL" -eq 0 ]
