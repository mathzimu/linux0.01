#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/head.h>
#include <asm/system.h>

/* ====================================================================
 * Periodic write-back (M2-3).
 *
 * Until now a dirty buffer or inode only reached the disk when somebody
 * called sync() — so an interrupted kernel lost data, and every
 * regression scenario had to rebuild minix.img because QEMU's writeback
 * could leave whatever the previous run had flushed.
 *
 * The flush cannot happen in the timer interrupt itself: the buffer and
 * inode paths sleep (sleep_on / schedule) and allocate buffers, and
 * sleeping inside an interrupt handler would deadlock.  So do_timer()
 * only raises a flag and wakes this task; the flush itself runs as a
 * real task with its own stack.
 *
 * The slot is pid 1 and the task page is a static buffer inside the
 * kernel image, so the write-back thread exists before anything else can
 * fail, and it can never be confused with a user process (it holds the
 * last task slot, so user pids still start at 1).
 * ==================================================================== */

/* Kernel stack for the write-back task, aligned to a page. */
static unsigned long sync_task_stack[1024] __attribute__((aligned(4096)));

/* Task slot (and therefore pid) of the write-back task.  The last slot
   keeps user-visible pids starting at 1. */
#define SYNC_TASK_SLOT  (NR_TASKS - 1)

unsigned long sync_interval = 5 * HZ;     /* jiffies between flushes */
unsigned long next_sync = 5 * HZ;
volatile int sync_pending = 0;

static const unsigned long root_dev = 0x301;   /* the MINIX root disk */

/* How many write-backs have actually written something. */
unsigned long sync_flushes = 0;

static void sync_loop(void)
{
    for (;;) {
        int written;

        wait_for_sync();

        /* One flush at a time: while this is running the timer keeps
           setting the flag, and it is simply consumed on the next
           pass. */
        sync_pending = 0;
        written = sync_dev(root_dev);

        if (written > 0) {
            sync_flushes++;
            printk("sync: %d block(s) written back (t=%d)\n",
                   written, (int)(jiffies / HZ));
        }
    }
}

void event_sync(void)
{
    struct task_struct *t;

    next_sync = jiffies + sync_interval;
    sync_pending = 1;

    t = task[SYNC_TASK_SLOT];
    if (t && t->state != TASK_RUNNING)
        wake_up(&t);
}

void wait_for_sync(void)
{
    while (!sync_pending) {
        current->state = TASK_INTERRUPTIBLE;
        if (!sync_pending)
            schedule();
        current->state = TASK_RUNNING;
    }
}

/* Create the write-back task (called from sched_init, so `current` is
   still the init task and no user task exists yet).  It takes the LAST
   task slot rather than the first free one: pid == slot index in this
   kernel, so occupying slot 1 would push every user process to pid 2 and
   change the shell's user-visible numbering. */
void sync_init(void)
{
    struct task_struct *p = task[SYNC_TASK_SLOT];
    unsigned long *frame;

    if (p != NULL) {
        printk("sync_init: task slot %d is taken; periodic write-back off\n",
               SYNC_TASK_SLOT);
        return;
    }

    p = (struct task_struct *)sync_task_stack;
    p->state = TASK_RUNNING;
    p->counter = 10;
    p->priority = 10;
    p->signal = 0;
    p->exit_code = 0;
    p->pid = SYNC_TASK_SLOT;
    p->parent = 0;
    p->pgrp = 0;
    p->session = 0;
    p->leader = 0;

    /* Share the init task's working directory/root (they are held
       references; root is NULL = the filesystem root). */
    p->pwd = current->pwd;
    if (p->pwd)
        p->pwd->i_count++;
    p->root = current->root;
    if (p->root)
        p->root->i_count++;

    /* Enter sync_loop the same way the CPU enters a task after an
       interrupt return: build a fake iret frame at the top of the task's
       kernel stack. */
    frame = (unsigned long *)((char *)p + PAGE_SIZE);
    *--frame = 0x202;                     /* eflags: IF set            */
    *--frame = KERNEL_CS;                 /* cs                        */
    *--frame = (unsigned long)sync_loop;  /* eip                       */

    p->tss.back_link = 0;
    p->tss.esp0 = (unsigned long)p + PAGE_SIZE;
    p->tss.ss0 = KERNEL_DS;
    p->tss.cr3 = read_cr3();
    p->tss.eip = (unsigned long)sync_loop;
    p->tss.eflags = 0x202;
    p->tss.eax = 0;
    p->tss.ecx = 0;
    p->tss.edx = 0;
    p->tss.ebx = 0;
    p->tss.esp = (unsigned long)frame;
    p->tss.ebp = 0;
    p->tss.esi = 0;
    p->tss.edi = 0;
    p->tss.es = KERNEL_DS;
    p->tss.cs = KERNEL_CS;
    p->tss.ss = KERNEL_DS;
    p->tss.ds = KERNEL_DS;
    p->tss.fs = KERNEL_DS;
    p->tss.gs = KERNEL_DS;

    p->ldt[0].a = 0x0000FFFF;
    p->ldt[0].b = 0x00CFFA00;
    p->ldt[1].a = 0x0000FFFF;
    p->ldt[1].b = 0x00CFF200;

    {
        int tss_entry = 8 + SYNC_TASK_SLOT * 2;
        int ldt_entry = tss_entry + 1;
        struct desc_struct *d;

        d = (struct desc_struct *)(&_gdt) + tss_entry;
        set_tss_desc(d, &p->tss);
        d = (struct desc_struct *)(&_gdt) + ldt_entry;
        set_ldt_desc(d, &p->ldt);
        p->tss.ldt = ldt_entry * 8;
    }

    task[SYNC_TASK_SLOT] = p;
    next_sync = jiffies + sync_interval;
}
