#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64
#define HZ 100

#define TASK_RUNNING 0
#define TASK_INTERRUPTIBLE 1
#define TASK_UNINTERRUPTIBLE 2
#define TASK_ZOMBIE 3
#define TASK_STOPPED 4

#define PAGE_SIZE 4096

/* System call numbers — identical to 1991 Linux 0.01 (kernel/sys_call_table) */
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

#define NR_OPEN 64

#ifndef __ASSEMBLER__

#include <asm/system.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>

struct tss_struct {
    long back_link;
    long esp0;
    long ss0;
    long esp1;
    long ss1;
    long esp2;
    long ss2;
    long cr3;
    long eip;
    long eflags;
    long eax, ecx, edx, ebx;
    long esp, ebp, esi, edi;
    long es, cs, ss, ds, fs, gs;
    long ldt;
    long trace_bitmap;
};

struct m_inode;              /* forward decl: pwd points into the FS */

struct task_struct {
    long state;
    long counter;
    long priority;
    long signal;
    long exit_code;          /* set on exit; reaped by waitpid() */
    unsigned long sig_ignore_mask; /* signals set to SIG_IGN via signal() */
    struct m_inode *pwd;     /* current working directory (held ref) */
    struct m_inode *root;    /* chroot() root (held ref; NULL = fs root) */
    unsigned short uid, euid, suid;
    unsigned short gid, egid, sgid;
    unsigned long alarm;     /* jiffies when SIGALRM fires (0 = none) */
    unsigned short umask;
    struct tss_struct tss;
    struct file *filp[NR_OPEN];
    unsigned long pid;
    unsigned long parent;      /* task[] index of the parent */
    unsigned long pgrp;
    unsigned long session;
    unsigned long leader;
    unsigned long utime, stime, cutime, cstime;
    unsigned long start_code, end_code, start_data, end_data;
    unsigned long brk, start_stack;
    struct desc_struct ldt[3];
};

struct file {
    unsigned short f_mode;
    unsigned short f_flags;
    unsigned short f_count;
    struct m_inode *f_inode;
    unsigned long f_pos;
};

extern struct task_struct *task[];
extern struct task_struct *current;
extern int jiffies;

void sched_init(void);
void schedule(void);
int sys_fork(void);
int sys_pause(void);
int sys_exit(int ret);
int sys_getpid(void);
int sys_setup(void);
int sys_time(unsigned long *tloc);
long sys_write(unsigned int fd, const char *buf, unsigned long count);
long sys_read(unsigned int fd, char *buf, unsigned long count);
int sys_open(const char *filename, int flag, int mode);
int sys_creat(const char *pathname, int mode);
int sys_close(unsigned int fd);
int sys_kill(int pid, int sig);
int sys_sync(void);
int sys_lseek(unsigned int fd, long offset, int origin);
int sys_dup(unsigned int fildes);
int sys_dup2(unsigned int oldfd, unsigned int newfd);
int sys_getppid(void);
int sys_mknod(const char *filename, int mode);
int sys_mkdir(const char *dirname, int mode);
int sys_unlink(const char *filename);
int sys_rmdir(const char *dirname);
int sys_waitpid(int pid, unsigned long *stat_addr, int options);
int sys_execve(const char *filename, char **argv, char **envp);
int sys_signal(int sig, unsigned long handler);
int sys_chdir(const char *filename);
int sys_chmod(const char *filename, int mode);
int sys_chown(const char *filename, int uid, int gid);
int sys_stat(const char *filename, struct stat *statbuf);
int sys_fstat(unsigned int fd, struct stat *statbuf);
int sys_access(const char *filename, int mode);
int sys_umask(int mask);
int sys_uname(struct utsname *utsbuf);
int sys_stime(unsigned long *tptr);
int sys_utime(const char *filename, unsigned long *times);
int sys_setuid(int uid);
int sys_getuid(void);
int sys_setgid(int gid);
int sys_getgid(void);
int sys_geteuid(void);
int sys_getegid(void);
int sys_alarm(long seconds);
int sys_nice(long increment);
int sys_times(struct tms *tbuf);
int sys_setpgid(int pid, int pgid);
int sys_getpgrp(void);
int sys_setsid(void);
int sys_chroot(const char *filename);
int sys_link(const char *oldname, const char *newname);
int sys_rename(const char *oldname, const char *newname);
int sys_pipe(unsigned long *fildes);
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);
int sys_brk(unsigned long end_data_seg);

void do_timer(void);
void do_signal(void);

#endif
#endif
