#!/usr/bin/env python3
"""Static verification of the kernel's memory map.

This exists because the memory map used to be an unwritten convention
spread over a dozen files, and it broke: NR_BUFFERS = 512 placed the
buffer cache at ~0x370000, right on top of the user heap
[0x310000, 0x3FE000).  Ring0 ignores the PTE U/S bit, so a user program
handing its malloc'd bytes to the filesystem cache corrupted data
silently.

Two layers of defence now exist:

  * compile time  - STATIC_ASSERTs in include/linux/memmap.h
  * boot time     - mem_check() in mm/memcheck.c

This script is the third, and the only one that runs without a
compiler, so it can gate a change to the layout before anything is
even built.  It checks:

  1. every region of the map is ordered and disjoint;
  2. the assembler mirror (include/memlayout.inc) agrees with the C
     header (include/memlayout.h);
  3. the buffer cache really fits in the gap between the user heap and
     the top of RAM;
  4. no file outside the layout headers hard-codes a user-space address
     (the "magic address" class of bug that caused the original issue);
  5. if a built kernel exists, its link-time _end still fits below the
     ceiling reserved for the kernel image and the kernel did not grow
     into the page-allocator pool.

Usage:  python3 scripts/check-layout.py [--kernel kernel/system]
Exit:   0 = consistent, 1 = inconsistent (with a detailed report).
"""

import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FAILURES = []
NOTES = []


def fail(msg):
    FAILURES.append(msg)


def note(msg):
    NOTES.append(msg)


# --------------------------------------------------------------------
# 1. Parse the C layout header
# --------------------------------------------------------------------

def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    return text


def join_continuations(text):
    """Collapse backslash-newline continuations into a single line.

    NR_BUFFERS in include/linux/fs.h is a three-line object-like macro; a
    line-at-a-time scan would only ever see the '\\' and silently fall
    back to a default value, which is exactly the kind of blind spot this
    script exists to remove.  Backslash-newline is translated to a space,
    exactly as C does, and the replacement text is then read up to the
    first '#' — the next directive and therefore the end of the macro.
    """
    return re.sub(r'[ \t]*\\\r?\n[ \t]*', ' ', text)


def eval_c_constants(path, seed=None):
    """Evaluate #define NAME (expr) for every object-like macro.

    Handles the subset of C the layout headers actually use: integer
    literals, other macro names, + - * / and parentheses.  `seed`
    pre-loads constants from an included header (the layout headers
    derive values from include/memlayout.h, and this parser deliberately
    does not chase #include).  Returns (constants, raw).  Raises
    ValueError on anything it cannot parse.
    """
    raw = open(path, encoding='utf-8').read()
    body = join_continuations(strip_comments(raw))

    defines = {}
    order = []
    # `\r?\n` is required in the terminator class: this repository stores
    # text files with CRLF endings.
    pat = (r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)([ \t(]*[^#\r\n]*)')
    for m in re.finditer(pat, body, flags=re.M):
        name = m.group(1)
        rest = m.group(2)
        # The whitespace between the macro name and "(" is significant: in
        # C `#define F(x) ...` is function-like while `#define F (x) ...`
        # is object-like with a parenthesised value.
        if rest.startswith('('):
            continue                       # function-like macro
        defines[name] = rest.strip()
        order.append(name)

    consts = dict(seed or {})

    def unwrap(expr):
        """Strip balanced outer parentheses: (E) -> E."""
        expr = expr.strip()
        while expr.startswith('(') and expr.endswith(')'):
            depth = 0
            balanced = True
            for i, ch in enumerate(expr):
                if ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                    if depth == 0 and i != len(expr) - 1:
                        balanced = False
                        break
            if not balanced or depth != 0:
                break
            expr = expr[1:-1].strip()
        return expr

    def split_ternary(expr):
        """Split a top-level C conditional expression C ? A : B."""
        depth = 0
        q = None
        for i, ch in enumerate(expr):
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == '?' and depth == 0:
                q = i
                break
        if q is None:
            return None
        depth = 0
        for j in range(q + 1, len(expr)):
            ch = expr[j]
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ':' and depth == 0:
                return expr[:q], expr[q + 1:j], expr[j + 1:]
        return None

    def eval_expr(name, expr):
        """Evaluate a C integer constant expression in Python."""
        expr = re.sub(r'\b([A-Za-z_]\w*)\b',
                      lambda mm: str(resolve(mm.group(1), [name])), expr)
        # C integer suffixes
        expr = re.sub(r'(?<=\d)[uUlL]+\b', '', expr)
        # C octal literals (mode bits like 00400) are not Python decimal
        expr = re.sub(r'(?<![\w.])0([0-7]+)\b', r'0o\1', expr)
        # Validate the C expression *before* translating it: the Python
        # conditional built below contains "if"/"else" keywords, which this
        # whitelist deliberately does not allow.
        if not re.fullmatch(r'[0-9a-fA-FoxX\s()+\-*/?:<>=!]+', expr):
            raise ValueError('cannot evaluate %s = %r' % (name, expr))
        tri = split_ternary(unwrap(expr))
        if tri:
            cond, a, b = tri
            expr = '((%s) if (%s) else (%s))' % (unwrap(a), unwrap(cond), unwrap(b))
        try:
            return int(eval(expr, {'__builtins__': {}}, {}))
        except SyntaxError as exc:
            raise ValueError('cannot evaluate %s = %r (%s)' % (name, expr, exc))

    def resolve(name, seen):
        if name in consts:
            return consts[name]
        if name in seen:
            raise ValueError('cyclic macro: ' + ' -> '.join(seen + [name]))
        if name not in defines:
            raise ValueError('macro %r is not defined in %s' % (name, path))
        consts[name] = eval_expr(name, defines[name])
        return consts[name]

    for name in order:
        try:
            resolve(name, [])
        except ValueError as exc:
            # Non-arithmetic macros (strings, expressions this deliberately
            # simple evaluator does not model) are ignored; set
            # CHECK_LAYOUT_DEBUG=1 to see exactly which ones and why.
            if os.environ.get('CHECK_LAYOUT_DEBUG'):
                sys.stderr.write('  [debug] %s: %s\n' % (name, exc))
    return consts, raw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', default=os.path.join(ROOT, 'kernel', 'system'),
                    help='built kernel ELF to cross-check (skipped if absent)')
    args = ap.parse_args()

    layout_path = os.path.join(ROOT, 'include', 'memlayout.h')
    if not os.path.exists(layout_path):
        fail('missing %s' % layout_path)
        return report()

    try:
        L, layout_raw = eval_c_constants(layout_path)
    except ValueError as exc:
        fail('include/memlayout.h: %s' % exc)
        return report()

    for name in ('KERNEL_IDENTITY_TOP', 'KERNEL_TABLES_END', 'USER_BASE',
                 'USER_WINDOW_TOP', 'USER_PDE_INDEX', 'USER_PROG_START',
                 'USER_PROG_END', 'USER_HEAP_START', 'USER_HEAP_END',
                 'USER_STACK_FLOOR', 'USER_STACK_TOP', 'USER_STACK_END',
                 'USER_ARGC_ADDR', 'USER_ARGV_ADDR', 'USER_ARGV_STR_TOP',
                 'USER_TAIL_TOP',
                 'KERNEL_IMG_BASE', 'PAGE_DIRECTORY', 'PAGE_TABLE_0'):
        if name not in L:
            fail('include/memlayout.h does not define %s' % name)
    if FAILURES:
        return report()

    top = L['KERNEL_IDENTITY_TOP']

    # --- 2. ordering / disjointness of the user regions -------------
    # M3: the user regions live in their own virtual window (one page
    # directory entry per process), NOT inside the kernel identity map,
    # so they are checked against the window rather than against RAM.
    win_lo = L['USER_BASE']
    win_hi = L['USER_WINDOW_TOP']
    regions = [
        ('kernel image',  L['KERNEL_IMG_BASE'], None),   # open ended: grows up
        ('page dir',      L['PAGE_DIRECTORY'],  L['PAGE_DIRECTORY'] + 0x1000),
        ('page table 0',  L['PAGE_TABLE_0'],    L['KERNEL_TABLES_END']),
        ('user program',  L['USER_PROG_START'], L['USER_PROG_END']),
        ('user heap',     L['USER_HEAP_START'], L['USER_HEAP_END']),
        ('user stack',    L['USER_STACK_FLOOR'], L['USER_STACK_TOP']),
    ]
    for name, lo, hi in regions:
        if hi is None:                       # open-ended region
            if lo >= L['PAGE_DIRECTORY']:
                fail('region %s starts at 0x%x, past the kernel page tables '
                     '(0x%x)' % (name, lo, L['PAGE_DIRECTORY']))
            continue
        if lo >= hi:
            fail('region %s is empty or inverted: [0x%x, 0x%x)' % (name, lo, hi))

    bounds = dict((n, (lo, hi)) for n, lo, hi in regions)
    if not (L['KERNEL_IMG_BASE'] < L['PAGE_DIRECTORY']):
        fail('the kernel image is linked at 0x%x but the page directory is at '
             '0x%x: the image would overwrite it'
             % (L['KERNEL_IMG_BASE'], L['PAGE_DIRECTORY']))
    if not (L['KERNEL_TABLES_END'] <= top):
        fail('the kernel page tables (end 0x%x) do not fit below the identity '
             'map top (0x%x)' % (L['KERNEL_TABLES_END'], top))

    # The identity map must be exactly what KERNEL_PT_COUNT tables cover:
    # PDE[0..count-1] * 4MB.  Getting this wrong means the kernel cannot
    # address all of RAM (or maps memory that is not there).
    if L['KERNEL_TABLES_END'] != L['PAGE_TABLE_0'] + L['KERNEL_PT_COUNT'] * 0x1000:
        fail('KERNEL_TABLES_END 0x%x is not PAGE_TABLE_0 + KERNEL_PT_COUNT '
             'pages' % L['KERNEL_TABLES_END'])
    if L['KERNEL_IDENTITY_TOP'] != L['KERNEL_PT_COUNT'] * 0x400000:
        fail('KERNEL_IDENTITY_TOP 0x%x is not KERNEL_PT_COUNT * 4MB'
             % L['KERNEL_IDENTITY_TOP'])

    ordered = ['user program', 'user heap', 'user stack']
    for a, b in zip(ordered, ordered[1:]):
        a_hi = bounds[a][1]
        b_lo = bounds[b][0]
        if a_hi > b_lo:
            fail('%s (ends 0x%x) overlaps %s (starts 0x%x)'
                 % (a, a_hi, b, b_lo))

    for name in ('user program', 'user heap', 'user stack'):
        lo, hi = bounds[name]
        if lo < win_lo or hi > win_hi:
            fail('%s [0x%x, 0x%x) is outside the user window [0x%x, 0x%x): a '
                 'process gets exactly one page table, so everything must fit '
                 'in one page-directory entry'
                 % (name, lo, hi, win_lo, win_hi))
    if win_lo != (L['USER_PDE_INDEX'] << 22):
        fail('USER_PDE_INDEX (%d) does not match USER_BASE 0x%x'
             % (L['USER_PDE_INDEX'], win_lo))
    if win_hi - win_lo != 0x400000:
        fail('the user window is 0x%x bytes; it must be exactly one 4MB '
             'page-directory entry' % (win_hi - win_lo))

    if L['USER_STACK_TOP'] % 0x1000:
        fail('USER_STACK_TOP 0x%x is not page aligned' % L['USER_STACK_TOP'])
    if L['USER_PROG_START'] % 0x1000:
        fail('USER_PROG_START 0x%x is not page aligned' % L['USER_PROG_START'])
    if L['USER_HEAP_START'] % 0x1000:
        note('USER_HEAP_START 0x%x is not page aligned (fine, but the '
             'mapped region gets rounded up)' % L['USER_HEAP_START'])

    # The argv block, the sigreturn stub and the strings all live in the
    # stack's tail page, in a fixed order: argc/argv slots at the bottom
    # (above the stack top), strings and stub higher up, and the tail page
    # ends exactly where the user window does.
    if not (L['USER_STACK_TOP'] < L['USER_ARGC_ADDR'] < L['USER_ARGV_ADDR']
            < L['USER_ARGV_STR_TOP']):
        fail('the argc/argv block does not fit above the stack top '
             '([0x%x, 0x%x))' % (L['USER_ARGC_ADDR'], L['USER_ARGV_STR_TOP']))
    if L['USER_SIGRETURN_ENTRY'] < L['USER_ARGV_STR_TOP']:
        fail('USER_SIGRETURN_ENTRY 0x%x overlaps the argv string area that '
             'packs down from 0x%x'
             % (L['USER_SIGRETURN_ENTRY'], L['USER_ARGV_STR_TOP']))
    if not (L['USER_SIGRETURN_ENTRY'] + 20 <= L['USER_TAIL_TOP']):
        fail('the sigreturn stub does not fit below USER_TAIL_TOP 0x%x '
             '(stub at 0x%x)'
             % (L['USER_TAIL_TOP'], L['USER_SIGRETURN_ENTRY']))
    if L['USER_TAIL_TOP'] != win_hi:
        fail('USER_TAIL_TOP 0x%x is not the top of the user window 0x%x'
             % (L['USER_TAIL_TOP'], win_hi))

    # --- 3. buffer cache must fit below BUFFER_CACHE_FLOOR ----------
    # The authoritative definition of NR_BUFFERS lives in fs.h, and it is
    # derived from the layout, so seed the parser with include/memlayout.h
    # (this parser deliberately does not chase #include).  Since M3 the
    # cache window itself is a kernel-side boundary, so it lives in
    # include/linux/memmap.h.
    memmap_path = os.path.join(ROOT, 'include', 'linux', 'memmap.h')
    if not os.path.exists(memmap_path):
        fail('missing %s' % memmap_path)
        return report()
    try:
        M, _ = eval_c_constants(memmap_path, seed=L)
    except ValueError as exc:
        fail('include/linux/memmap.h: %s' % exc)
        M = {}

    nrbuf, nrbuf_max = 64, 256
    floor = M.get('BUFFER_CACHE_FLOOR')
    if floor is None:
        fail('include/linux/memmap.h does not define BUFFER_CACHE_FLOOR')
        return report()
    if not (M.get('KERNEL_TABLES_END', L['KERNEL_TABLES_END']) <= floor):
        fail('BUFFER_CACHE_FLOOR 0x%x is below the end of the kernel page '
             'tables 0x%x' % (floor, L['KERNEL_TABLES_END']))
    try:
        F, _ = eval_c_constants(os.path.join(ROOT, 'include', 'linux', 'fs.h'),
                                seed=dict(list(L.items()) + list(M.items())))
        if 'NR_BUFFERS' in F:
            nrbuf = F['NR_BUFFERS']
        if 'NR_BUFFERS_MAX' in F:
            nrbuf_max = F['NR_BUFFERS_MAX']
    except ValueError as exc:
        fail('include/linux/fs.h: %s' % exc)

    if nrbuf <= 0 or (nrbuf & (nrbuf - 1)) != 0:
        fail('NR_BUFFERS = %d is not a positive power of two '
             '(getblk indexes the hash with & (NR_BUFFERS - 1))' % nrbuf)
    if nrbuf > nrbuf_max:
        fail('NR_BUFFERS = %d exceeds NR_BUFFERS_MAX = %d' % (nrbuf, nrbuf_max))

    # struct buffer_head is 32 bytes on i386; budget 64 to stay honest if
    # a field is ever added.  The cache grows DOWN from BUFFER_CACHE_TOP.
    bh_size = 64
    cache_bytes = nrbuf * (1024 + bh_size)
    cache_top = M.get('BUFFER_CACHE_TOP')
    if cache_top is None:
        fail('include/linux/memmap.h does not define BUFFER_CACHE_TOP')
        return report()
    cache_start = cache_top - cache_bytes
    if cache_start < floor:
        fail('buffer cache [0x%x, 0x%x) grows below BUFFER_CACHE_FLOOR 0x%x '
             '— NR_BUFFERS=%d does not fit the window [0x%x, 0x%x); this is '
             'the original corruption bug'
             % (cache_start, cache_top, floor, nrbuf, floor, cache_top))
    else:
        note('buffer cache: %d buffers, [0x%x, 0x%x), %d bytes clear of the '
             'floor (0x%x)'
             % (nrbuf, cache_start, cache_top, cache_start - floor, floor))

    # --- 4. kernel-side boundaries from linux/memmap.h --------------
    checks = [
        ('KERNEL_IMAGE_LIMIT', 'KERNEL_HEAP_START'),
        ('KERNEL_HEAP_END', 'KERNEL_LOW_MEM'),
        ('KERNEL_LOW_MEM', 'PAGE_DIRECTORY'),
        ('KERNEL_TABLES_END', 'BUFFER_CACHE_FLOOR'),
        ('BUFFER_CACHE_TOP', 'KERNEL_IDENTITY_TOP'),
        ('MEMORY_END_MINIMUM', 'KERNEL_IDENTITY_TOP'),
    ]
    for a, b in checks:
        if a in M and b in M and M[a] > M[b]:
            fail('%s (0x%x) is above %s (0x%x)' % (a, M[a], b, M[b]))
    for name in ('KERNEL_LOW_MEM', 'KERNEL_IDENTITY_TOP', 'KERNEL_TABLES_END'):
        if name not in M:
            fail('include/linux/memmap.h does not define %s' % name)

    # --- 5. assembler mirror must agree with the C header -----------
    inc_path = os.path.join(ROOT, 'include', 'memlayout.inc')
    if not os.path.exists(inc_path):
        fail('missing %s (boot/*.s and user/*.s include it)' % inc_path)
    else:
        inc = {}
        for m in re.finditer(r'^[ \t]*\.equ[ \t]+(\w+)[ \t]*,[ \t]*(0x[0-9A-Fa-f]+)',
                             open(inc_path, encoding='utf-8').read(), flags=re.M):
            inc[m.group(1)] = int(m.group(2), 16)
        if not inc:
            fail('include/memlayout.inc defines no .equ constants')
        for name, value in sorted(inc.items()):
            if name not in L:
                fail('memlayout.inc defines %s but memlayout.h does not' % name)
            elif L[name] != value:
                fail('%s: memlayout.inc says 0x%x, memlayout.h says 0x%x'
                     % (name, value, L[name]))
        note('memlayout.inc: %d constants agree with memlayout.h' % len(inc))

    # --- 6. no hard-coded user addresses outside the layout files ---
    # Every address inside the user window is a layout fact, and the
    # original bug was exactly such a literal living in user/lib.c.
    window_lo, window_hi = L['USER_BASE'], L['USER_WINDOW_TOP']
    allow = {
        os.path.relpath(layout_path, ROOT),
        os.path.relpath(inc_path, ROOT),
        os.path.relpath(memmap_path, ROOT),
        os.path.relpath(os.path.join(ROOT, 'mm', 'memcheck.c'), ROOT),
        os.path.relpath(os.path.join(ROOT, 'scripts', 'check-layout.py'), ROOT),
        os.path.relpath(os.path.join(ROOT, 'boot', 'head.s'), ROOT),
        os.path.relpath(os.path.join(ROOT, 'kernel.ld'), ROOT),
        # kernel/asm.s unwinds the signal frame and has to name the same
        # boundaries; it is checked against include/memlayout.inc by hand
        # (the .inc mirror is validated above).
        os.path.relpath(os.path.join(ROOT, 'kernel', 'asm.s'), ROOT),
    }
    offenders = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames
                       if d not in ('.git', 'test-logs', '.iso_tmp')]
        for fn in filenames:
            if not fn.endswith(('.c', '.h', '.s')):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, ROOT)
            if rel in allow:
                continue
            try:
                text = strip_comments(open(full, encoding='utf-8',
                                           errors='replace').read())
            except OSError:
                continue
            for m in re.finditer(r'0x([0-9A-Fa-f]{5,8})\b', text):
                value = int(m.group(1), 16)
                if window_lo <= value < window_hi:
                    line = text[:m.start()].count('\n') + 1
                    offenders.append('%s:%d: 0x%X' % (rel, line, value))
    if offenders:
        fail('user-space addresses hard-coded outside the layout headers '
             '(use include/memlayout.h):\n    ' + '\n    '.join(offenders))

    # --- 7. built kernel: is _end still under the ceiling? ----------
    kernel = args.kernel
    if os.path.exists(kernel):
        end = elf_symbol(kernel, '_end')
        if end is None:
            note('%s has no symbol table; skipped the _end check'
                 % os.path.relpath(kernel, ROOT))
        else:
            limit = None
            try:
                M2, _ = eval_c_constants(memmap_path, seed=L)
                limit = M2.get('KERNEL_IMAGE_LIMIT')
            except ValueError:
                pass
            if limit is None:
                fail('KERNEL_IMAGE_LIMIT missing; cannot validate _end')
            elif end >= limit:
                fail('kernel _end = 0x%x has reached KERNEL_IMAGE_LIMIT = 0x%x: '
                     'the image is about to grow into the kernel heap'
                     % (end, limit))
            else:
                note('kernel _end = 0x%x, %d bytes below KERNEL_IMAGE_LIMIT '
                     '(0x%x)' % (end, limit - end, limit))
    else:
        note('no built kernel at %s; skipped the _end check'
             % os.path.relpath(kernel, ROOT))

    return report()


def elf_symbol(path, name):
    """Return the value of a symbol from a 32-bit little-endian ELF."""
    try:
        data = open(path, 'rb').read()
    except OSError:
        return None
    if data[:4] != b'\x7fELF':
        return None
    try:
        e_shoff, = struct.unpack_from('<I', data, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x2e)
    except struct.error:
        return None
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        fields = struct.unpack_from('<10I', data, off)
        sections.append(dict(name=fields[0], typ=fields[1], offset=fields[4],
                             size=fields[5], link=fields[6]))
    for sec in sections:
        if sec['typ'] != 2:                     # SHT_SYMTAB
            continue
        strtab = sections[sec['link']]
        for j in range(sec['size'] // 16):
            nameoff, value = struct.unpack_from('<II', data, sec['offset'] + j * 16)
            start = strtab['offset'] + nameoff
            stop = data.index(b'\0', start)
            if data[start:stop].decode('latin-1') == name:
                return value
    return None


def report():
    for n in NOTES:
        print('  note: ' + n)
    if FAILURES:
        print()
        for f in FAILURES:
            print('FAIL: ' + f)
        print('\nmemory map: INCONSISTENT (%d problem(s))' % len(FAILURES))
        return 1
    print('\nmemory map: consistent (no compiler required)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
