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

/* Program image regions, as they came out of the ELF file (B3).
 *
 * Until B3 execve() copied every LOAD segment into fresh pages up front.
 * Now it only records where the bytes live: an image page is read in on
 * its first fault, which also means a page can be *thrown away* when
 * memory gets tight and read back later — the file is the backing store,
 * so no swap area is needed for the program text.
 *
 *   va       page-aligned start of the segment in the user window
 *   file_off file offset of the byte at `va`
 *   filesz   bytes of the segment that exist in the file
 *   memsz    bytes the segment occupies (the tail is zero-filled = BSS)
 */
#define NR_EXE_REGIONS 4

struct exe_region {
    unsigned long va;
    unsigned long file_off;
    unsigned long filesz;
    unsigned long memsz;
    unsigned long flags;       /* ELF p_flags: PF_X 1, PF_W 2, PF_R 4.
                                  Only a non-writable region's pages can be
                                  thrown away — a data page may have been
                                  modified since it was read. */
};

struct task_struct {
    long state;
    long counter;
    long priority;
    long signal;
    long exit_code;          /* set on exit; reaped by waitpid() */
    /* Per-signal disposition: SIG_DFL (0), SIG_IGN (1) or a Ring3 handler
     * address.  Indexed by signal number (0..31); index 0 is unused.
     * SIGCHLD's entry carries the "ignore" semantics that the old
     * sig_ignore_mask used to.  Inherited across fork by "*p = *current". */
    unsigned long handlers[32];
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
    /* M3: this task's address space — the physical address of its page
     * directory (its user window is private; the kernel identity map is
     * shared).  0 means "kernel only", which is what the init task and
     * the write-back task use.  tss.cr3 holds the same value because the
     * CPU reloads CR3 from the TSS on every task switch. */
    unsigned long pg_dir;
    unsigned long start_code, end_code, start_data, end_data;
    unsigned long brk, start_stack;
    /* The executable this image came from (held reference) plus the
     * region table above: together they are the backing store that lets
     * an image page be evicted and paged back in. */
    struct m_inode *exe_inode;
    struct exe_region exe_regions[NR_EXE_REGIONS];
    int nr_exe_regions;
    struct desc_struct ldt[3];
};

/* struct file lives in include/linux/fs.h (pulled in through
 * include/linux/memmap.h), together with the file_table[] that needs the
 * complete type. */

extern struct task_struct *task[];
extern struct task_struct *current;
extern int jiffies;

/* Per-task signal mask (B5), indexed by pid — which is the task[] slot.
 * This is deliberately *not* a field of struct task_struct.  The struct
 * sits at the bottom of the task's 4 KB page and the child's kernel
 * stack is the room left above it, so sys_fork copies the parent's live
 * stack into `PAGE_SIZE - sizeof(struct task_struct)` bytes; three
 * earlier attempts at signal masks grew the struct and each one ended in
 * a silent double fault (see the guard in sys_fork).  Whether a signal
 * is blocked does not belong to the saved CPU context anyway. */
extern unsigned long sig_blocked[NR_TASKS];

/* Periodic write-back task (kernel/sync.c): pid 1, its task page is a
 * static buffer inside the kernel image so it cannot be reaped or
 * confused with a user process. */
void sync_init(void);
void event_sync(void);
void wait_for_sync(void);
extern volatile int sync_pending;
extern unsigned long sync_interval;
extern unsigned long next_sync;

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
int sys_sigprocmask(int how, unsigned long *set, unsigned long *oldset);
int sys_sigsuspend(unsigned long *mask);
int sys_sigaction(int sig, unsigned long *act, unsigned long *oldact);
/* Called from sys_sigreturn (kernel/asm.s) to put the caller's signal
   mask back after a handler has run with its own signal blocked. */
void sigreturn_restore_mask(void);
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
/* kf is the caller's syscall frame base (the saved ebx slot); see
 * ret_from_sys_call in boot/head.s and the UFRAME_* offsets in
 * kernel/process.c. */
void do_signal(unsigned char *kf);
/* Same delivery, but from the timer interrupt's frame: this is what lets
 * a task that never makes another system call still be killed (or get its
 * alarm).  Called from timer_interrupt in boot/head.s. */
void do_signal_from_intr(unsigned long *iframe);

#endif
#endif
