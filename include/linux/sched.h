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

#define NR_OPEN 64

#ifndef __ASSEMBLER__

#include <asm/system.h>

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

struct task_struct {
    long state;
    long counter;
    long priority;
    long signal;
    struct tss_struct tss;
    struct file *filp[NR_OPEN];
    unsigned short uid;
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
int sys_open(const char *filename, int flag);
int sys_close(unsigned int fd);
int sys_kill(int pid, int sig);
int sys_sync(void);
int sys_lseek(unsigned int fd, long offset, int origin);
int sys_dup(unsigned int fildes);
int sys_dup2(unsigned int oldfd, unsigned int newfd);
int sys_getppid(void);
int sys_mknod(const char *filename, int mode);
int sys_mkdir(const char *dirname, int mode);

void do_timer(void);
void do_signal(void);

#endif
#endif
