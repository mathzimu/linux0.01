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

#define NR_OPEN 64

#ifndef __ASSEMBLER__

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

void do_timer(void);

#endif
#endif
