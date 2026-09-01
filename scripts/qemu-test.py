#!/usr/bin/env python3
"""Headless QEMU test harness for the Linux 0.01 teaching kernel.

The kernel mirrors console output to COM1, so we capture exact text via
`-serial file:...`. Keyboard input is injected through the HMP monitor
(sendkey), and optional PPM screenshots are taken for visual backup.

Usage:
  qemu-test.py --image Image --keys "spawn\ntime\n" --hold 1.5
  qemu-test.py --image Image --hda minix.img --keys "ls\ncat /hello.txt\n"
"""
import argparse
import os
import socket
import subprocess
import sys
import time

SENDKEY = {
    'a': 'a', 'b': 'b', 'c': 'c', 'd': 'd', 'e': 'e', 'f': 'f', 'g': 'g',
    'h': 'h', 'i': 'i', 'j': 'j', 'k': 'k', 'l': 'l', 'm': 'm', 'n': 'n',
    'o': 'o', 'p': 'p', 'q': 'q', 'r': 'r', 's': 's', 't': 't', 'u': 'u',
    'v': 'v', 'w': 'w', 'x': 'x', 'y': 'y', 'z': 'z',
    '0': '0', '1': '1', '2': '2', '3': '3', '4': '4', '5': '5', '6': '6',
    '7': '7', '8': '8', '9': '9',
    ' ': 'spc', '\n': 'ret', '\b': 'backspace', '\t': 'tab',
    '-': 'minus', '=': 'equal', '[': 'bracket_left', ']': 'bracket_right',
    ';': 'semicolon', "'": 'apostrophe', '`': 'grave_accent',
    '\\': 'backslash', ',': 'comma', '.': 'dot', '/': 'slash',
}


def hmp(sock, cmd, wait=0.4):
    sock.sendall(cmd.encode() + b'\n')
    time.sleep(wait)
    sock.settimeout(0.15)
    out = b''
    while True:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            out += chunk
        except socket.timeout:
            break
    return out.decode(errors='replace')


def type_text(sock, text, delay=0.5):
    for ch in text:
        if ch not in SENDKEY:
            print('!! cannot type %r' % ch, file=sys.stderr)
            continue
        hmp(sock, 'sendkey %s' % SENDKEY[ch], wait=0.05)
        time.sleep(delay)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', help='floppy image (Image)')
    ap.add_argument('--iso', help='bootable ISO')
    ap.add_argument('--hda', help='IDE disk image (MINIX fs)')
    ap.add_argument('--out', default='/tmp/qtest', help='output prefix')
    ap.add_argument('--hold', type=float, default=1.2,
                    help='seconds before typing keys')
    ap.add_argument('--keys', default='',
                    help='keystrokes to type (\\n = enter)')
    ap.add_argument('--tail', type=float, default=1.5,
                    help='seconds to wait after typing keys')
    ap.add_argument('--extra', default='', help='extra qemu args')
    ap.add_argument('--qemu', default='qemu-system-i386')
    args = ap.parse_args()

    mon = args.out + '.mon'
    serial = args.out + '.serial'
    for f in (mon, serial):
        if os.path.exists(f):
            os.unlink(f)

    cmd = [args.qemu, '-m', '4M', '-vga', 'std', '-display', 'none',
           '-serial', 'file:%s' % serial,
           '-monitor', 'unix:%s,server,nowait' % mon]
    if args.image:
        cmd += ['-drive', 'file=%s,format=raw,if=floppy,index=0' % args.image]
    elif args.iso:
        cmd += ['-cdrom', args.iso, '-boot', 'd']
    else:
        print('need --image or --iso', file=sys.stderr)
        sys.exit(2)
    if args.hda:
        cmd += ['-drive', 'file=%s,format=raw,if=ide,index=0' % args.hda]
    if args.extra:
        cmd += args.extra.split()

    print('qemu: %s' % ' '.join(cmd))
    proc = subprocess.Popen(cmd)

    sock = None
    try:
        for _ in range(100):
            if os.path.exists(mon):
                try:
                    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    sock.connect(mon)
                    break
                except OSError:
                    time.sleep(0.1)
            time.sleep(0.1)
        if sock is None:
            print('monitor socket never appeared', file=sys.stderr)
            sys.exit(1)

        time.sleep(args.hold)
        if args.keys:
            # accept literal "\n" (from plain single-quoted CLI args) as well
            # as an actual newline (bash $'...\n') — unify both
            keys = args.keys.replace('\\n', '\n')
            type_text(sock, keys)
            time.sleep(args.tail)
        hmp(sock, 'screendump %s.ppm' % args.out)
        time.sleep(0.5)
        with open(args.out + '.regs', 'w') as f:
            f.write(hmp(sock, 'info registers'))
        hmp(sock, 'quit', wait=0.3)
    finally:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        if sock:
            sock.close()

    # print the serial capture
    if os.path.exists(serial):
        with open(serial, 'rb') as f:
            data = f.read()
        sys.stdout.write(data.decode(errors='replace'))


if __name__ == '__main__':
    main()
