#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/head.h>
#include <signal.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <string.h>

extern long syscall_esp;
extern int syscall_cpl;
extern void ret_from_sys_call(void);

/* user stack area (matches init/shell.c run_user_program) */
#define USER_STACK_TOP      0x3FF000UL
#define CHILD_USER_STACK_TOP 0x3E0000UL

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

    /* remember the parent (task[] index) for sys_getppid */
    for (i = 0; i < NR_TASKS; i++)
        if (task[i] == current)
            break;
    p->parent = i;

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
     * At `int $0x80` entry the CPU pushes the exception frame onto the
     * kernel stack (via TSS.esp0 for a ring3 caller) and head.s records
     * `syscall_esp` = that esp, then pushes
     *   ds es fs gs eax ebp edi esi edx ecx ebx   (11 longs, downward)
     * so the full frame, low -> high, is:
     *   ebx ecx edx esi edi ebp eax gs fs es ds | eip cs eflags [esp ss]
     * (slot -11) ...                        (slot -1)  (slot 0..+2 [+3 +4])
     * The trailing esp/ss are present only for a ring3 caller
     * (syscall_cpl == 3), so the frame is 14 longs (ring0) or 16
     * (ring3); ret_from_sys_call pops ebx..ebp, eax, gs..ds (11) and
     * iret pops 3 or 5 longs accordingly.
     *
     * - parent_sp   = syscall_esp + (12 ring0 / 20 ring3) : first free
     *   slot ABOVE the int-frame; everything above it is the caller's
     *   live C frames.  We copy [parent_sp, esp0) wholesale so the
     *   child "returns into" a faithful copy of those frames.
     * - child_frame = child_sp - words*4 : the syscall-return frame
     *   the child consumes at ret_from_sys_call, copied from
     *   parent_frame = syscall_esp - 11*4 (the ebx slot).
     * ------------------------------------------------------------------ */
    parent_top = current->tss.esp0;              /* parent's kernel stack top */
    child_top = (long)p + PAGE_SIZE;             /* child's kernel stack top */
    if (syscall_cpl == 3) {
        /* ring3 caller: 16-word frame, and copy the user stack so the
           child resumes with its own copy (iret pops user esp/ss) */
        int words = 16;
        long user_esp, user_size, child_user_esp;

        parent_sp = syscall_esp + 20;            /* above ss */
        size = parent_top - parent_sp;
        child_sp = child_top - size;
        memcpy((void *)child_sp, (void *)parent_sp, size);

        child_frame  = (long *)(child_sp - words * sizeof(long));
        parent_frame = (long *)(syscall_esp - 11 * sizeof(long));
        for (i = 0; i < words; i++)
            child_frame[i] = parent_frame[i];

        /* user stack: [user_esp, 0x3FF000) -> child area below 0x3E0000 */
        user_esp = *(long *)(syscall_esp + 12);
        user_size = USER_STACK_TOP - user_esp;
        child_user_esp = CHILD_USER_STACK_TOP - user_size;
        if (user_size < 0x10000)
            memcpy((void *)child_user_esp, (void *)user_esp, user_size);
        child_frame[14] = child_user_esp;        /* esp slot */
        /* child_frame[15] (ss) stays USER_DS from the parent copy */
    } else {
        /* ring0 caller: 14-word frame (spawn / kernel-side fork) */
        parent_sp  = syscall_esp + 12;
        size = parent_top - parent_sp;
        child_sp  = child_top - size;
        memcpy((void *)child_sp, (void *)parent_sp, size);
        child_frame  = (long *)(child_sp - 14 * sizeof(long));
        parent_frame = (long *)(syscall_esp - 11 * sizeof(long));
        for (i = 0; i < 14; i++)
            child_frame[i] = parent_frame[i];
    }

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

    /* Become a zombie: keep the task[] slot AND the task page so the
       parent's waitpid() can reap us (read exit_code, then free the
       page).  Freeing here would be a use-after-free — the exiting
       task is still running on this page and the CPU writes its state
       back into this TSS on the next switch. */
    current->exit_code = ret;
    current->state = TASK_ZOMBIE;

    /* notify the parent (SIGCHLD bit + wake if it is waiting in
       waitpid / sys_pause).  A parent that set signal(SIGCHLD,
       SIG_IGN) does not get notified: its exiting children are
       auto-reaped by schedule() (see there), never becoming
       reaped-by-waitpid zombies. */
    {
        struct task_struct *parent = task[current->parent];
        if (parent && !(parent->sig_ignore_mask & (1 << 17))) {
            parent->signal |= (1 << 17);   /* SIGCHLD */
            if (parent->state == TASK_INTERRUPTIBLE)
                parent->state = TASK_RUNNING;
        }
    }

    /* schedule() never selects a zombie, so this loop runs until the
       parent reaps us or the machine idles. */
    for (;;)
        schedule();
    cli();
    for (;;) __asm__ volatile("hlt");
}

int sys_getpid(void)
{
    return current->pid;
}

int sys_getppid(void)
{
    struct task_struct *p = task[current->parent];
    return p ? p->pid : 0;
}

/* Set a signal disposition.  This teaching kernel supports only the
   two built-in dispositions:
     signal(sig, SIG_IGN)  -> the signal is ignored
     signal(sig, SIG_DFL)  -> default behaviour (terminate for
                              SIGINT/SIGQUIT/SIGKILL, ignore otherwise)
   Custom handlers are not implemented: any other handler value returns
   -1.  SIGKILL is not ignorable (POSIX).  Dispositions are inherited
   by fork() (*p = *current copies sig_ignore_mask). */
int sys_signal(int sig, unsigned long handler)
{
    if (sig < 1 || sig >= 32)
        return -1;
    if (sig == SIGKILL && handler == SIG_IGN)
        return -1;
    if (handler != SIG_DFL && handler != SIG_IGN)
        return -1;

    if (handler == SIG_IGN)
        current->sig_ignore_mask |= (1 << sig);
    else
        current->sig_ignore_mask &= ~(1 << sig);
    return 0;
}

/* Wait for a child to become a zombie and reap it: hand out the exit
   code via *stat_addr and free the child's task page.
   pid > 0  : wait for that specific child (pid)
   pid <= 0 : wait for any child
   options & 1 (WNOHANG): return 0 immediately if no zombie yet.
   Returns the child pid, 0 (WNOHANG), or -1 (no such child).
   POSIX: once the parent has set signal(SIGCHLD, SIG_IGN) there are
   no waitable children — return -1 (ECHILD) immediately. */
int sys_waitpid(int pid, unsigned long *stat_addr, int options)
{
    int i, current_idx;
    struct task_struct *p;
    int found_any;

    if (current->sig_ignore_mask & (1 << 17))
        return -1;

    for (current_idx = 0; current_idx < NR_TASKS; current_idx++)
        if (task[current_idx] == current)
            break;

    while (1) {
        found_any = 0;
        for (i = 1; i < NR_TASKS; i++) {       /* children are never slot 0 */
            p = task[i];
            if (!p)
                continue;
            if (p->parent != current_idx)
                continue;
            found_any = 1;
            if (pid > 0 && p->pid != (unsigned long)pid)
                continue;

            if (p->state == TASK_ZOMBIE) {
                /* reap: hand out the exit code, free the task page */
                if (stat_addr)
                    put_fs_long(p->exit_code, stat_addr);
                task[i] = NULL;
                free_page((unsigned long)p);
                return p->pid;
            }
        }

        if (!found_any)
            return -1;                          /* no such child */
        if (options & 1)                        /* WNOHANG */
            return 0;

        /* sleep until a child exits; sys_exit's SIGCHLD wakes us */
        current->state = TASK_INTERRUPTIBLE;
        schedule();
        current->state = TASK_RUNNING;
    }
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
