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

# run_case <name> <prep-cmd> <keys> <needle...>
#   prep-cmd : 准备干净盘的命令（须生成 minix.img）
#   keys     : 注入的按键（支持字面 \n 或真换行）
#   needle...: 断言——serial 输出须包含每个子串（出现才算过）
run_case() {
    local name="$1" prep="$2" keys="$3"
    shift 3
    local out
    if ! eval "$prep" >/dev/null 2>&1; then
        echo "FAIL [$name]  setup failed: $prep"
        FAIL=$((FAIL+1)); return 1
    fi
    out=$(python3 scripts/qemu-test.py --image Image --hda minix.img \
             --hold "${TEST_HOLD:-1.2}" --tail "${TEST_TAIL:-1.5}" \
             --extra "${TEST_EXTRA:-}" \
             --keys "$keys" 2>/dev/null)
    printf '%s' "$out" > "$LOGDIR/$name.serial"
    for needle in "$@"; do
        if ! printf '%s' "$out" | grep -qF "$needle"; then
            echo "FAIL [$name]  missing: \"$needle\"  (see $LOGDIR/$name.serial)"
            echo "---- tail $LOGDIR/$name.serial ----"
            tail -15 "$LOGDIR/$name.serial" 2>/dev/null
            echo "--------------------------------"
            FAIL=$((FAIL+1)); return 1
        fi
    done
    echo "PASS [$name]"
    PASS=$((PASS+1))
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

echo
echo "================================"
echo "  $PASS passed, $FAIL failed"
echo "================================"
[ "$FAIL" -eq 0 ]
