#ifndef _UNISTD_H
#define _UNISTD_H

/* Minimal Linux 0.01 - user-side syscall wrappers.
 *
 * These macros expand to the classic `int $0x80` inline-asm sequence.
 * System call numbers are the same as include/linux/sched.h __NR_*.
 * Args are passed in ebx/ecx/edx; the return value comes back in eax.
 *
 * Note: this kernel has no errno variable; callers must check for
 * negative return values themselves.
 */

#define __NR_setup 0
#define __NR_exit 1
#define __NR_fork 2
#define __NR_read 3
#define __NR_write 4
#define __NR_open 5
#define __NR_close 6
#define __NR_getpid 7
#define __NR_pause 8
#define __NR_time 9
#define __NR_kill 10
#define __NR_sync 11
#define __NR_lseek 12
#define __NR_dup 13
#define __NR_dup2 14
#define __NR_getppid 15
#define __NR_mknod 16
#define __NR_mkdir 17

/* lseek origins */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _syscall0(type, name) \
type name(void) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall1(type, name, atype, a) \
type name(atype a) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name), "b"((long)(a)) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall2(type, name, atype, a, btype, b) \
type name(atype a, btype b) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name), "b"((long)(a)), "c"((long)(b)) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall3(type, name, atype, a, btype, b, ctype, c) \
type name(atype a, btype b, ctype c) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name), "b"((long)(a)), "c"((long)(b)), \
          "d"((long)(c)) \
        : "memory"); \
    return (type)__res; \
}

_syscall0(int, setup)
_syscall1(int, exit, int, status)
_syscall0(int, fork)
_syscall3(int, read, int, fd, char *, buf, unsigned long, count)
_syscall3(int, write, int, fd, const char *, buf, unsigned long, count)
_syscall2(int, open, const char *, filename, int, flag)
_syscall1(int, close, int, fd)
_syscall0(int, getpid)
_syscall0(int, pause)
_syscall1(int, time, unsigned long *, tloc)
_syscall2(int, kill, int, pid, int, sig)
_syscall0(int, sync)
_syscall3(long, lseek, unsigned int, fd, long, offset, int, origin)
_syscall1(int, dup, unsigned int, fd)
_syscall2(int, dup2, unsigned int, oldfd, unsigned int, newfd)
_syscall0(int, getppid)
_syscall2(int, mknod, const char *, filename, int, mode)
_syscall2(int, mkdir, const char *, dirname, int, mode)

#endif
