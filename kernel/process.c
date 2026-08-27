#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/head.h>
#include <asm/system.h>
#include <string.h>

extern long syscall_esp;
extern void ret_from_sys_call(void);

int sys_fork(void)
{
    struct task_struct *p;
    int i, pid;
    int nr;
    long *parent_frame;
    long parent_top, parent_sp, size;
    long child_top, child_sp;
    long *child_frame;

    p = (struct task_struct *)get_free_page();
    if (!p)
        return -1;

    nr = -1;
    pid = 0;
    for (i = 0; i < NR_TASKS; i++) {
        if (task[i]) continue;
        task[i] = p;
        nr = i;
        pid = i;               /* pid == task[] slot index (init = 0) */
        break;
    }

    if (pid == 0) {
        free_page((unsigned long)p);
        return -1;
    }

    *p = *current;

    for (i = 0; i < NR_OPEN; i++) {
        if (p->filp[i])
            p->filp[i]->f_count++;
    }

    p->pid = pid;
    p->counter = p->priority;
    p->state = TASK_RUNNING;

    /* ------------------------------------------------------------------
     * Child kernel-stack construction.  The layout below is a hard
     * contract with boot/head.s `system_call`; do not change one side
     * without the other.
     *
     * At `int $0x80` entry (ring0), the CPU pushes eip/cs/eflags and
     * head.s records `syscall_esp` = that esp, then pushes
     *   ds es fs gs eax ebp edi esi edx ecx ebx   (11 longs, downward)
     * so the full frame, low -> high, is:
     *   ebx ecx edx esi edi ebp eax gs fs es ds | eip cs eflags
     * (slot -11) ...                        (slot -1)  (slot 0..+2)
     * ret_from_sys_call pops ebx..ebp, eax, gs..ds (11) then iret
     * pops eip/cs/eflags (3) = 14 longs total.
     *
     * - parent_sp   = syscall_esp + 12 : first free slot ABOVE the
     *   int-frame; everything above it is the caller's live C frames
     *   (cmd_*, shell_main, ...).  We copy [parent_sp, esp0) wholesale
     *   so the child "returns into" a faithful copy of those frames.
     * - child_frame = child_sp - 14*4 : the 14-long syscall-return
     *   frame the child will consume at ret_from_sys_call, copied from
     *   parent_frame = syscall_esp - 11*4 (the ebx slot).
     * ------------------------------------------------------------------ */
    parent_top = current->tss.esp0;              /* parent's kernel stack top */
    parent_sp  = syscall_esp + 12;               /* parent's esp on fork return */
    size = parent_top - parent_sp;

    child_top = (long)p + PAGE_SIZE;
    child_sp  = child_top - size;

    /* copy the parent's live stack frames (locals, return addresses) */
    memcpy((void *)child_sp, (void *)parent_sp, size);

    /* build the syscall-return frame just below the copied stack */
    child_frame  = (long *)(child_sp - 14 * sizeof(long));
    parent_frame = (long *)(syscall_esp - 11 * sizeof(long)); /* ebx slot */
    for (i = 0; i < 14; i++)
        child_frame[i] = parent_frame[i];

    p->tss.back_link = 0;
    p->tss.esp0 = (long)p + PAGE_SIZE;
    p->tss.ss0 = KERNEL_DS;
    p->tss.cr3 = read_cr3();
    p->tss.eip = (long)ret_from_sys_call;
    p->tss.eflags = 0x202;
    p->tss.eax = 0;                              /* fork() returns 0 in child */
    p->tss.ebx = 0;
    p->tss.ecx = 0;
    p->tss.edx = 0;
    p->tss.esi = 0;
    p->tss.edi = 0;
    p->tss.ebp = 0;
    p->tss.esp = (long)child_frame;
    p->tss.cs = KERNEL_CS;
    p->tss.ss = KERNEL_DS;
    p->tss.ds = KERNEL_DS;
    p->tss.es = KERNEL_DS;
    p->tss.fs = KERNEL_DS;
    p->tss.gs = KERNEL_DS;

    {
        int tss_entry = 8 + nr * 2;
        int ldt_entry = tss_entry + 1;
        struct desc_struct *p_desc;

        p_desc = (struct desc_struct *)(&_gdt) + tss_entry;
        set_tss_desc(p_desc, &p->tss);

        p_desc = (struct desc_struct *)(&_gdt) + ldt_entry;
        set_ldt_desc(p_desc, &p->ldt);

        p->tss.ldt = ldt_entry * 8;
    }

    return pid;
}

int sys_exit(int ret)
{
    int i;

    /* Close every file this task has open, releasing inode refs. */
    for (i = 0; i < NR_OPEN; i++) {
        struct file *f = current->filp[i];
        if (f) {
            f->f_count--;
            if (f->f_count == 0)
                iput(f->f_inode);
            current->filp[i] = NULL;
        }
    }

    /* The init task (task[0]) must never leave task[]: with an empty
       task[] the scheduler's idle path (c<0) re-enters itself on every
       timer tick and overflows the stack, corrupting task[] and
       crashing (observed as a #GP on a garbage TSS selector).  So the
       init task's "exit" turns it into a permanent idle loop. */
    if (current == task[0]) {
        current->state = TASK_RUNNING;
        for (;;)
            schedule();
    }

    for (i = 0; i < NR_TASKS; i++) {
        if (task[i] == current) {
            task[i] = NULL;
            break;
        }
    }

    current->state = TASK_UNINTERRUPTIBLE;
    /* Do NOT free_page(current) here: the exiting task is still running
       on this page (its TSS + kernel stack), and the CPU will write the
       task state back into this TSS when it switches away.  Freeing it
       lets a later fork() reuse the page and corrupt that write-back
       (observed as CR3->0 and a triple fault).  With no wait/zombie
       reaper, we leak one 4KB page per exit instead — acceptable for
       a teaching kernel. */
    /* We are no longer in task[]; schedule() will never switch back to
       us, so loop on it until the machine switches away for good.
       (Must NOT cli+hlt here: that would starve every other task.) */
    for (;;)
        schedule();
    cli();
    for (;;) __asm__ volatile("hlt");
}

int sys_getpid(void)
{
    return current->pid;
}

int sys_pause(void)
{
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}

/* Minimal signal delivery: runs at every syscall return when the
   current task has pending signals (see ret_from_sys_call in head.s).
   Only default actions are implemented (SIG_DFL):
     - SIGINT/SIGQUIT/SIGKILL (2/3/9): terminate with 128+sig
     - everything else: ignored
   Custom handlers (sigaction-style) and SIGCHLD-to-parent are not
   implemented.  This is enough to make sys_pause() wakeable via
   sys_kill(). */
void do_signal(void)
{
    int sig;

    if (!current->signal)
        return;

    for (sig = 1; sig < 32; sig++) {
        if (!(current->signal & (1 << sig)))
            continue;
        current->signal &= ~(1 << sig);
        switch (sig) {
        case 2:   /* SIGINT */
        case 3:   /* SIGQUIT */
        case 9:   /* SIGKILL */
            sys_exit(128 + sig);   /* never returns */
        default:
            break;                 /* default action: ignore */
        }
    }
}
