#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/head.h>
#include <linux/fs.h>
#include <signal.h>
#include <asm/system.h>

extern unsigned long _end;
extern void ltr(unsigned short sel);

int jiffies = 0;
struct task_struct *current = NULL;
struct task_struct *task[NR_TASKS] = {NULL,};

/* The idle task (pid 0) that becomes the shell.  Only the two scheduling
   fields are non-zero: everything else — the signal handlers, pwd/root,
   the exe_* image table, the TSS, the LDT, filp[] — is zero, and C99
   designated initializers give us that for free.
 *
 * It used to be a positional list with a comment per field, and it had
 * silently drifted out of step with struct task_struct: M3 added pg_dir
 * and the code/data/brk group, B3 added the image table, and nobody moved
 * the list along.  The values still came out zero by luck, but gcc warned
 * ("braces around scalar initializer") and the comments pointed at the
 * wrong members.  Naming the fields we mean makes that impossible. */
static struct task_struct init_task = {
    .counter = 15,
    .priority = 15,
};

void sched_init(void)
{
    int i;
    struct desc_struct *p;

    p = (struct desc_struct *)(&_gdt);
    p += 8;

    for (i = 0; i < NR_TASKS; i++) {
        task[i] = NULL;
    }

    current = &init_task;
    task[0] = &init_task;

    /* sys_setup() (called before sched_init in main) has mounted the
       MINIX fs, so the root inode is resolvable.  The init task (and
       via fork, everyone else) starts with pwd = root. */
    init_task.pwd = iget(0x301, 1);

    /* fds 0/1/2 are the console for every task (see drivers/tty_io.c).
       They are ordinary descriptor-table entries, which is what lets a
       shell dup2() a file onto stdout and get real redirection. */
    init_task.filp[0] = init_task.filp[1] = init_task.filp[2] = &tty_file;
    tty_file.f_count = 1;

    init_task.tss.ss0 = KERNEL_DS;
    init_task.tss.esp0 = (unsigned long)&_end + 0x1000;
    /* CRITICAL: the CPU loads CR3 from tss.cr3 on every task switch.
       Left at 0, switching back to the init task would zero CR3 and
       crash on the next memory access.  M3: the init task is a kernel
       task, so it keeps the kernel directory (identity map, no user
       window) for its whole life. */
    init_task.pg_dir = 0;
    init_task.tss.cr3 = kernel_pg_dir;

    init_task.ldt[0].a = 0x0000FFFF;
    init_task.ldt[0].b = 0x00CFFA00;
    init_task.ldt[1].a = 0x0000FFFF;
    init_task.ldt[1].b = 0x00CFF200;

    p = (struct desc_struct *)(&_gdt) + 8;
    set_tss_desc(p, &init_task.tss);
    p = (struct desc_struct *)(&_gdt) + 9;
    set_ldt_desc(p, &init_task.ldt);

    init_task.tss.ldt = 72;
    ltr(64);

    __asm__ volatile(
        "movb $0x36, %%al\n\t"
        "outb %%al, $0x43\n\t"
        "movb $0x9b, %%al\n\t"
        "outb %%al, $0x40\n\t"
        "movb $0x2e, %%al\n\t"
        "outb %%al, $0x40\n\t"
        : : : "al"
    );

    /* Start the periodic write-back task (pid 1).  It gets its own task
       page inside the kernel image, so this must happen before any user
       process can fork. */
    sync_init();
}

void schedule(void)
{
    int next, c, i;
    struct task_struct **p;

    /* Auto-reap zombies nobody will wait for: children whose parent
       set signal(SIGCHLD, SIG_IGN), and orphans whose parent task is
       already gone (no adoption in this kernel).  We must skip
       `current`: an exiting task calls schedule() from sys_exit and
       its page must stay valid until the switch actually happens. */
    for (i = 0; i < NR_TASKS; i++) {
        struct task_struct *z = task[i];
        struct task_struct *par;
        if (!z || z == current || z->state != TASK_ZOMBIE)
            continue;
        par = task[z->parent];
        if (par && !(par->handlers[SIGCHLD] == SIG_IGN))
            continue;                 /* parent will waitpid() for it */
        task[i] = NULL;
        free_page((unsigned long)z);
    }

    while (1) {
        c = -1;
        next = -1;
        p = &task[NR_TASKS - 1];

        while (p >= &task[0]) {
            if (*p == NULL) { p--; continue; }
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {
                c = (*p)->counter;
                next = (int)(p - task);
            }
            p--;
        }

        if (c > 0) break;

        if (c < 0) {
            unsigned long eflags;
            for (p = &task[NR_TASKS - 1]; p >= &task[0]; p--) {
                if (*p == NULL) continue;
                (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
            }
            __asm__ volatile("pushfl; popl %0" : "=r"(eflags));
            if (!(eflags & 0x200))
                __asm__ volatile("sti");
            __asm__ volatile("hlt");
            return;
        }

        for (p = &task[NR_TASKS - 1]; p >= &task[0]; p--) {
            if (*p == NULL) continue;
            (*p)->counter = ((*p)->counter >> 1) + (*p)->priority;
        }
    }

    {
        int current_idx;
        for (current_idx = 0; current_idx < NR_TASKS; current_idx++)
            if (task[current_idx] == current) break;
        if (next != current_idx) {
            /* NOTE: do NOT assign current = task[next] here.
               switch_to() swaps it in via "xchgl %%ecx, current"
               and its leading "cmpl %%ecx, current; je" guard would
               see them equal and skip the ljmp entirely. */
            switch_to(next);
        }
    }
}

void do_timer(void)
{
    int i;

    jiffies++;

    /* alarm(2): deliver SIGALRM when the deadline passes.  Every task is
       checked, not just `current`: a task that called alarm() and then
       blocked (the classic alarm(); pause(); idiom) is not current any
       more, and checking only `current` would never deliver its alarm.
       An interruptible task with a signal pending is made runnable, which
       is what lets pause() return. */
    for (i = 0; i < NR_TASKS; i++) {
        struct task_struct *p = task[i];
        if (!p || !p->alarm)
            continue;
        if ((unsigned long)jiffies < p->alarm)
            continue;
        p->signal |= (1 << SIGALRM);
        p->alarm = 0;
        if (p->state == TASK_INTERRUPTIBLE)
            p->state = TASK_RUNNING;
    }

    /* Deadline sleepers (sleep/select): this is the kernel's only "wake me
       later" mechanism.  alarm() could not be reused — it wakes a task by
       delivering SIGALRM, and a timeout is not a signal.  A task whose
       deadline passed is made runnable; it re-checks its own condition. */
    for (i = 0; i < NR_TASKS; i++) {
        struct task_struct *p = task[i];
        if (!p || !sleep_deadline[i])
            continue;
        if ((long)(jiffies - (long)sleep_deadline[i]) < 0)
            continue;
        sleep_deadline[i] = 0;
        if (p->state == TASK_INTERRUPTIBLE || p->state == TASK_UNINTERRUPTIBLE)
            p->state = TASK_RUNNING;
    }

    /* Periodic write-back: only raise the flag and wake the sync task
       here.  Flushing from the timer interrupt would sleep in the
       buffer/inode paths (sleep_on, buffer allocation) and deadlock. */
    if ((long)(jiffies - (long)next_sync) >= 0)
        event_sync();

    if (current->counter > 0) {
        current->counter--;
    }

    if (current->counter > 0) return;

    schedule();
}
