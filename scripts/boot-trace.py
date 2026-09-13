#!/usr/bin/env python3
"""Diagnose the boot path: build, boot under QEMU, and report how far the
kernel gets plus what the loader staged.

This exists because the image did not boot in a local QEMU container and
that had to be chased with traces rather than guesswork.  It is kept so
the remaining puzzle (the kernel triple-faults at the protected-mode
"ljmp $KERNEL_CS, ..." even though the GDT descriptor it loads is
correct) can be picked up again quickly.

Usage:  python3 scripts/boot-trace.py [--image Image] [--hda minix.img]
        (run inside the build container; needs qemu-system-i386 and nm)
"""
import argparse
import os
import re
import subprocess
import sys

LOAD_BASE = 0x10800


def symbols(path='kernel/system'):
    sym = {}
    try:
        out = subprocess.run(['nm', path], capture_output=True, text=True).stdout
    except FileNotFoundError:
        return sym
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            sym[p[2]] = int(p[0], 16)
    return sym


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', default='Image')
    ap.add_argument('--hda', default=None)
    ap.add_argument('--seconds', type=int, default=40)
    ap.add_argument('--serial', default='/tmp/boot-trace.serial')
    ap.add_argument('--log', default='/tmp/boot-trace.log')
    args = ap.parse_args()

    sym = symbols()
    cmd = ['qemu-system-i386', '-fda', args.image, '-m', '16M', '-boot', 'a',
           '-display', 'none', '-monitor', 'none',
           '-serial', 'file:%s' % args.serial, '-no-reboot',
           '-d', 'in_asm,cpu_reset', '-D', args.log]
    if args.hda:
        cmd += ['-hda', args.hda]
    proc = subprocess.Popen(cmd)
    try:
        proc.wait(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        proc.terminate()

    log = open(args.log, errors='replace').read().splitlines()
    addrs = [int(m.group(1), 16) for line in log
             if (m := re.match(r'^(0x[0-9a-f]+):', line))]

    print('instruction blocks          :', len(addrs))
    for name in ('startup_32', 'setup_paging', 'setup_idt', 'flush_cs', 'main'):
        if name in sym:
            print('  %-12s %#08x  reached %d time(s)'
                  % (name, sym[name], sum(1 for a in addrs if a == sym[name])))
    print('triple faults               :',
          sum(1 for l in log if 'Triple fault' in l))
    print('exceptions logged           :', sum(1 for l in log if 'v=' in l))
    print('serial bytes                :',
          os.path.getsize(args.serial) if os.path.exists(args.serial) else 0)

    # Where does execution stop when it does reach the kernel?
    if addrs:
        print('last trace lines:')
        for line in log[-6:]:
            print('   ', line.strip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
