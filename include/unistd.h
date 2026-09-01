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
#define __NR_waitpid 7
#define __NR_creat 8
#define __NR_link 9
#define __NR_unlink 10
#define __NR_execve 11
#define __NR_chdir 12
#define __NR_time 13
#define __NR_mknod 14
#define __NR_chmod 15
#define __NR_chown 16
#define __NR_break 17
#define __NR_stat 18
#define __NR_lseek 19
#define __NR_getpid 20
#define __NR_mount 21
#define __NR_umount 22
#define __NR_setuid 23
#define __NR_getuid 24
#define __NR_stime 25
#define __NR_ptrace 26
#define __NR_alarm 27
#define __NR_fstat 28
#define __NR_pause 29
#define __NR_utime 30
#define __NR_stty 31
#define __NR_gtty 32
#define __NR_access 33
#define __NR_nice 34
#define __NR_ftime 35
#define __NR_sync 36
#define __NR_kill 37
#define __NR_rename 38
#define __NR_mkdir 39
#define __NR_rmdir 40
#define __NR_dup 41
#define __NR_pipe 42
#define __NR_times 43
#define __NR_prof 44
#define __NR_brk 45
#define __NR_setgid 46
#define __NR_getgid 47
#define __NR_signal 48
#define __NR_geteuid 49
#define __NR_getegid 50
#define __NR_acct 51
#define __NR_phys 52
#define __NR_lock 53
#define __NR_ioctl 54
#define __NR_fcntl 55
#define __NR_mpx 56
#define __NR_setpgid 57
#define __NR_ulimit 58
#define __NR_uname 59
#define __NR_umask 60
#define __NR_chroot 61
#define __NR_ustat 62
#define __NR_dup2 63
#define __NR_getppid 64
#define __NR_getpgrp 65
#define __NR_setsid 66

/* open flags (Linux 0.01) */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000

/* waitpid options */
#define WNOHANG 1

/* lseek origins */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _syscall0(type, name) \
static inline type name(void) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall1(type, name, atype, a) \
static inline type name(atype a) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name), "b"((long)(a)) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall2(type, name, atype, a, btype, b) \
static inline type name(atype a, btype b) \
{ \
    long __res; \
    __asm__ volatile("int $0x80" \
        : "=a"(__res) \
        : "0"(__NR_##name), "b"((long)(a)), "c"((long)(b)) \
        : "memory"); \
    return (type)__res; \
}

#define _syscall3(type, name, atype, a, btype, b, ctype, c) \
static inline type name(atype a, btype b, ctype c) \
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
_syscall3(int, open, const char *, filename, int, flag, int, mode)
_syscall1(int, close, int, fd)
_syscall3(int, waitpid, int, pid, unsigned long *, stat_addr, int, options)
_syscall2(int, creat, const char *, pathname, int, mode)
_syscall2(int, link, const char *, oldname, const char *, newname)
_syscall1(int, unlink, const char *, filename)
_syscall3(int, execve, const char *, filename, char **, argv, char **, envp)
_syscall1(int, chdir, const char *, path)
_syscall1(int, time, unsigned long *, tloc)
_syscall2(int, mknod, const char *, filename, int, mode)
_syscall2(int, chmod, const char *, filename, int, mode)
_syscall3(int, chown, const char *, filename, int, uid, int, gid)
_syscall2(int, stat, const char *, filename, unsigned long *, statbuf)
_syscall3(long, lseek, unsigned int, fd, long, offset, int, origin)
_syscall0(int, getpid)
_syscall1(int, setuid, int, uid)
_syscall0(int, getuid)
_syscall1(int, stime, unsigned long *, tptr)
_syscall1(int, alarm, long, seconds)
_syscall2(int, fstat, unsigned int, fd, unsigned long *, statbuf)
_syscall0(int, pause)
_syscall2(int, utime, const char *, filename, unsigned long *, times)
_syscall2(int, access, const char *, filename, int, mode)
_syscall1(int, nice, long, increment)
_syscall0(int, sync)
_syscall2(int, kill, int, pid, int, sig)
_syscall2(int, rename, const char *, oldname, const char *, newname)
_syscall2(int, mkdir, const char *, dirname, int, mode)
_syscall1(int, rmdir, const char *, dirname)
_syscall1(int, dup, unsigned int, fd)
_syscall1(int, pipe, unsigned long *, fildes)
_syscall1(int, times, unsigned long *, tbuf)
_syscall1(int, setgid, int, gid)
_syscall0(int, getgid)
_syscall2(int, signal, int, sig, unsigned long, handler)
_syscall0(int, geteuid)
_syscall0(int, getegid)
_syscall3(int, fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg)
_syscall2(int, setpgid, int, pid, int, pgid)
_syscall1(int, uname, unsigned long *, utsbuf)
_syscall1(int, umask, int, mask)
_syscall1(int, chroot, const char *, filename)
_syscall2(int, dup2, unsigned int, oldfd, unsigned int, newfd)
_syscall0(int, getppid)
_syscall0(int, getpgrp)
_syscall0(int, setsid)

#endif
