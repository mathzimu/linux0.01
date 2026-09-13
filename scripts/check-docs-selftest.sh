#!/usr/bin/env bash
# 反向测试：确认 scripts/check-docs.py 真能拦住布局漂移。
# 用法: bash scripts/check-docs-selftest.sh
set -u
cd "$(dirname "$0")/.."

PROBE=docs/_lint_probe.md
restore() { rm -f "$PROBE"; }
trap restore EXIT

fail=0
probe() {                       # probe <期望: FAIL|PASS> <内容> <说明>
    local want="$1" body="$2" desc="$3"
    printf '# probe\n\n%s\n' "$body" > "$PROBE"
    local out
    out=$(python3 scripts/check-docs.py 2>&1)
    local got=PASS
    printf '%s' "$out" | grep -q '^FAIL' && got=FAIL
    if [ "$got" = "$want" ]; then
        echo "ok   [$want] $desc"
    else
        echo "BAD  [want $want, got $got] $desc"
        printf '%s\n' "$out" | head -3
        fail=1
    fi
    rm -f "$PROBE"
}

probe FAIL '用户程序链接在 0x200000。'                    '旧程序链接地址应被拦下'
probe FAIL '用户堆是 0x310000 到 0x3FE000。'              '旧堆地址应被拦下'
probe FAIL '授权靠 grant_user_pages()。'                  '已删除的函数应被拦下'
probe FAIL 'qemu-system-i386 -fda Image -m 4M -boot a'    '旧内存参数应被拦下'
probe FAIL '用户栈顶是 0x083FF001。'                      '布局常量里的地址写错应被拦下'
probe FAIL '一键回归（12 个场景）'                        '场景数过时应被拦下'
probe PASS '用户栈顶是 0x083FF000（USER_STACK_TOP）。'     '正确常量应通过'
probe PASS 'M3 前程序链接在 0x200000，现在链接在 0x08000000。' '带历史标注的旧地址应通过'

if [ "$fail" -eq 0 ]; then
    echo 'check-docs self-test: all probes behaved as expected'
else
    echo 'check-docs self-test: FAILED'
fi
exit "$fail"
