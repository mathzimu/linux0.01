#!/usr/bin/env bash
# 反复跑一个内存压力场景，检查稳定性。
# 用法: bash scripts/rerun-case.sh [evict|oom|swap] [times]
set -u
cd "$(dirname "$0")/.."

NAME=${1:-evict}
TIMES=${2:-3}
MEM=4M
MINWAIT=45
KEYS='exec /bin/evicttest\n'
GREP='evicttest:|evict: '

case "$NAME" in
  evict)
    rm -f minix.img && make prog NAME=evicttest >/dev/null 2>&1
    ;;
  oom)
    MINWAIT=90
    KEYS='exec /bin/oomtest\nls\n'
    GREP='oomtest:|out of memory|hello.txt'
    rm -f minix.img && make prog NAME=oomtest >/dev/null 2>&1
    ;;
  swap)
    MINWAIT=90
    KEYS='exec /bin/swaptest\nmemstat\n'
    GREP='swaptest:|swapped out|PAGE FAULT'
    rm -f minix.img && make prog NAME=swaptest >/dev/null 2>&1
    ;;
  *)
    echo "unknown case $NAME"; exit 2;;
esac

fail=0
for i in $(seq 1 "$TIMES"); do
    out=$(python3 scripts/qemu-test.py --image Image --hda minix.img \
              --mem "$MEM" --hold 3 --tail 6 --min-wait "$MINWAIT" \
              --keys "$KEYS" 2>&1)
    if printf '%s' "$out" | grep -qF 'PASS' || printf '%s' "$out" | grep -qF 'oomtest: done'; then
        echo "run $i: OK"
    else
        echo "run $i: FAIL"
        fail=1
    fi
    printf '%s' "$out" | grep -E "$GREP" | tail -8
    echo "  ----"
done
exit "$fail"
