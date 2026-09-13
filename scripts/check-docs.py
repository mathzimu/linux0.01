#!/usr/bin/env python3
"""Keep the documentation honest about the memory map.

Why this exists
---------------
`scripts/check-layout.py` catches a layout change that the *code* has not
caught up with.  Nothing caught the other direction: after M3 moved the
user address space from fixed physical addresses (0x200000/0x310000/...)
to a per-process window at 0x08000000, the code was updated and several
tutorial chapters were not.  A reader following TUTORIAL.md would have
been taught a memory model the kernel no longer has - the worst kind of
documentation bug in a teaching repository, because the docs *are* the
product.

This script is the cheap, compiler-free guard for that:

  1. known-stale facts must not appear as current state.  A line that
     mentions one of the old addresses or macros is allowed only if it
     is explicitly marked as history ("M3 前", "历史", "原实现", "旧",
     "已删除", ...) - on that line or the one above it;
  2. layout numbers quoted in prose must match include/memlayout.h: any
     0x08xxxxxx address in the docs has to be one of the layout
     constants (a typo like 0x083FF001 is caught immediately);
  3. the scenario count quoted in the docs must equal the number of
     cases scripts/regress.sh actually runs;
  4. the QEMU memory size quoted in the docs must match the harness.

Usage:  python3 scripts/check-docs.py
Exit:   0 = consistent, 1 = drift (with file:line reports).
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DOC_FILES = ['README.md']
DOC_DIRS = ['docs']

# Same parser as check-layout.py, reused rather than duplicated.
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
from importlib import import_module

_layout = import_module('check-layout'.replace('-', '_')) if False else None
# check-layout.py is not importable as a module (hyphen in the name), so
# load it by path.
import importlib.util

_spec = importlib.util.spec_from_file_location(
    'check_layout', os.path.join(ROOT, 'scripts', 'check-layout.py'))
check_layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_layout)

FAILURES = []
NOTES = []


def fail(msg):
    FAILURES.append(msg)


def note(msg):
    NOTES.append(msg)


# --------------------------------------------------------------------
# 1. stale facts
# --------------------------------------------------------------------

# (regex, human explanation).  A match is fine when the line (or the one
# above it) is marked as historical.
STALE = [
    (r'0x200000\b', 'user programs used to be linked at physical 0x200000'),
    (r'0x310000\b', 'the user heap used to live at physical 0x310000'),
    (r'0x3FE000\b', 'the old user heap ceiling'),
    (r'0x3BC000\b', 'the old buffer cache placement'),
    (r'0x3FF000\b', 'the old user stack top'),
    (r'0x3FF00[48C]\b', 'the old argc/argv slots'),
    (r'0x3FF100\b', 'the old sigreturn stub address'),
    (r'0x3E0000\b', 'the old fork() child stack anchor (sat in the cache)'),
    (r'grant_user_pages', 'removed in M3: user access is now a property of '
                          'the per-process page table'),
    (r'CHILD_USER_STACK', 'removed in M3: the child stack is COW-shared now'),
    (r'IDENTITY_MAP_SIZE\b', 'renamed to KERNEL_IDENTITY_TOP in M3'),
    (r'-m 4M\b', 'QEMU runs with -m 16M now'),
    (r'仅 PDE\[0\]|只有 PDE\[0\]|只设置了一个 PDE|only PDE\[0\]',
     'the kernel maps 0-16MB with PDE[0..3] now'),
]

# A stale match is tolerated when the text around it says "this is
# history".  Keep this list short and explicit: the point is to force a
# marker on every reference to the old model.
HISTORY_MARKERS = [
    'M3 前', 'M3 之前', 'M3 已', '历史', '原实现', '原来的', '旧', '曾经',
    '已删除', '已废弃', '已取代', '已被取代', 'superseded', 'removed in M3',
    'used to', 'before M3', 'legacy',
]


def is_marked_history(lines, idx):
    for probe in (lines[idx], lines[idx - 1] if idx > 0 else ''):
        for marker in HISTORY_MARKERS:
            if marker in probe:
                return True
    return False


# --------------------------------------------------------------------
# 2. numeric facts quoted in prose
# --------------------------------------------------------------------

USER_ADDR = re.compile(r'0x(08[0-9A-Fa-f]{6})\b')


def iter_docs():
    files = [os.path.join(ROOT, f) for f in DOC_FILES]
    for d in DOC_DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, d)):
            dirnames[:] = [x for x in dirnames if x not in ('.git',)]
            for fn in sorted(filenames):
                if fn.endswith('.md'):
                    files.append(os.path.join(dirpath, fn))
    return files


def main():
    layout_path = os.path.join(ROOT, 'include', 'memlayout.h')
    L, _ = check_layout.eval_c_constants(layout_path)

    # The set of addresses the docs are allowed to quote as current.
    known = {}
    for name, value in L.items():
        if isinstance(value, int) and 0x08000000 <= value <= 0x08400000:
            known.setdefault(value, []).append(name)
    # A couple of values the docs quote that are not constants of their own.
    known.setdefault(L['USER_STACK_TOP'], []).append('USER_STACK_TOP')

    ncases = 0
    regress = os.path.join(ROOT, 'scripts', 'regress.sh')
    if os.path.exists(regress):
        for line in open(regress, encoding='utf-8'):
            # A case may be invoked with environment assignments in front of
            # it ("QEMU_MEM=4M run_case evict ..."), so match anywhere in the
            # line rather than anchoring at the start — but skip comments,
            # which document the usage ("# run_case <name> ...") and are not
            # invocations.
            if line.lstrip().startswith('#'):
                continue
            if re.search(r'(^|\s)run_case2?\s+\S', line):
                ncases += 1
        note('regress.sh runs %d scenarios' % ncases)

    harness_mem = None
    qemu_test = os.path.join(ROOT, 'scripts', 'qemu-test.py')
    if os.path.exists(qemu_test):
        m = re.search(r"'-m',\s*'(\d+M)'", open(qemu_test, encoding='utf-8').read())
        if m:
            harness_mem = m.group(1)
            note('qemu-test.py runs with -m %s' % harness_mem)

    for path in iter_docs():
        rel = os.path.relpath(path, ROOT)
        lines = open(path, encoding='utf-8').read().splitlines()

        for idx, line in enumerate(lines):
            for pattern, why in STALE:
                if not re.search(pattern, line):
                    continue
                if is_marked_history(lines, idx):
                    continue
                fail('%s:%d mentions %r outside a history note (%s)\n'
                     '      %s' % (rel, idx + 1, pattern, why, line.strip()))

            for m in USER_ADDR.finditer(line):
                value = int(m.group(1), 16)
                if value not in known:
                    fail('%s:%d quotes user address 0x%X, which is not a '
                         'layout constant (see include/memlayout.h)'
                         % (rel, idx + 1, value))

        # Numbers that must track the code.
        text = '\n'.join(lines)
        for m in re.finditer(r'(\d+)\s*个(?:默认)?场景', text):
            claimed = int(m.group(1))
            if ncases and claimed != ncases and '历史' not in text[max(0, m.start() - 80):m.start()]:
                fail('%s claims %d scenarios, scripts/regress.sh runs %d'
                     % (rel, claimed, ncases))
        if harness_mem:
            for m in re.finditer(r'-m (\d+M)\b', text):
                if m.group(1) != harness_mem:
                    fail('%s says "-m %s" but scripts/qemu-test.py uses "-m %s"'
                         % (rel, m.group(1), harness_mem))

    for n in NOTES:
        print('  note: ' + n)
    if FAILURES:
        print()
        for f in FAILURES:
            print('FAIL: ' + f)
        print('\ndocs: INCONSISTENT with the memory map (%d problem(s))'
              % len(FAILURES))
        return 1
    print('\ndocs: consistent with the memory map')
    return 0


if __name__ == '__main__':
    sys.exit(main())
