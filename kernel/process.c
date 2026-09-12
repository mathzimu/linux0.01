#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/fs.h>
#include <linux/head.h>
#include <signal.h>
#include <static-assert.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <string.h>

extern long syscall_esp;
extern int syscall_cpl;
extern void ret_from_sys_call(void);

/* USER_STACK_TOP and CHILD_USER_STACK_TOP both come from
 * include/memlayout.h alongside every other user-visible address. */
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

    /* pwd and root are shared references: give the child its own hold
       so the parent's iput() on exit cannot free them under us */
    p->pwd = current->pwd;
    if (p->pwd)
        p->pwd->i_count++;
    p->root = current->root;
    if (p->root)
        p->root->i_count++;

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

        /* user stack: [user_esp, USER_STACK_TOP) -> child copy placed
           just below CHILD_USER_STACK_TOP.  The child region is sized so
           the copy can never reach the heap below or the buffer cache
           above; a parent with more live stack than that is refused
           (fork returns -ENOMEM) rather than silently corrupting the
           filesystem cache.  Proper copy-on-write would remove the
           limit entirely — see docs/M3. */
        user_esp = *(long *)(syscall_esp + 12);
        user_size = USER_STACK_TOP - user_esp;
        if (user_size <= 0 ||
            user_size > (long)(CHILD_USER_STACK_TOP - CHILD_USER_STACK_END)) {
            printk("fork: user stack copy of %ld bytes does not fit the "
                   "child stack region (%ld bytes)\n",
                   user_size, (long)(CHILD_USER_STACK_TOP - CHILD_USER_STACK_END));
            goto fail;
        }
        child_user_esp = CHILD_USER_STACK_TOP - user_size;
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

fail:
    /* Undo everything the child setup grabbed before giving up: the task
       slot, the pwd/root holds (p was filled by "the child's pwd = the
       parent's", so they must be dropped) and the task page. */
    if (p->pwd)
        p->pwd->i_count--;
    if (p->root)
        p->root->i_count--;
    task[nr] = NULL;
    free_page((unsigned long)p);
    return -1;
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

    /* release our pwd and root references */
    if (current->pwd) {
        iput(current->pwd);
        current->pwd = NULL;
    }
    if (current->root) {
        iput(current->root);
        current->root = NULL;
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
        if (parent && !(parent->handlers[SIGCHLD] == SIG_IGN)) {
            parent->signal |= (1 << SIGCHLD);
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

/* Set a signal disposition.  Three kinds of value are accepted, exactly
   as 0.01's sys_signal did (it compared against the *old* handler):
     SIG_IGN -> ignore the signal
     SIG_DFL -> default action (terminate for SIGINT/SIGQUIT/SIGKILL/
                SIGSEGV/SIGPIPE/SIGALRM, ignore otherwise)
     anything else -> a Ring3 function pointer (checked to point into the
                user program image, so the kernel cannot be tricked into
                iret-ing to a kernel address or into the signal frame)
   SIGKILL is not ignorable (POSIX).  Dispositions are inherited by
   fork() ("*p = *current" copies the handlers[] array).  The previous
   disposition is returned. */
int sys_signal(int sig, unsigned long handler)
{
    unsigned long old;

    if (sig < 1 || sig >= 32)
        return -1;
    if (sig == SIGKILL)
        return -1;
    if (handler != SIG_DFL && handler != SIG_IGN &&
        (handler < USER_PROG_START || handler >= USER_PROG_END))
        return -1;

    old = current->handlers[sig];
    current->handlers[sig] = handler;
    return (int)old;
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

    if (current->handlers[SIGCHLD] == SIG_IGN)
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

/* ------------------------------------------------------------------
 * Signal delivery with custom handlers (Linux 0.01 kernel/signal.c
 * semantics, adapted to this kernel's system_call frame).
 *
 * Runs at every syscall return (ret_from_sys_call in boot/head.s calls
 * it) when the current task has pending signals.  Only a Ring3 caller
 * can be given a handler, because a handler is entered by rewriting the
 * iret frame: for a Ring0 caller there is no user context to return to.
 *
 * The handler runs on the user stack.  The kernel writes a small frame
 * there (a copy of the interrupted context, for the program to inspect
 * and for change-detection) and points the iret at the handler; when
 * the handler returns it lands on a stub at USER_SIGRETURN_ENTRY, which
 * issues the sigreturn syscall.  That syscall reloads the context from
 * the kernel-side struct sig_context below — and that copy is the
 * authoritative one, because the failed/abandoned handler's stack is
 * not trustworthy.
 *
 * User-stack frame layout (see include/memlayout.h; the top word is
 * deliberately the handler's argument, so its `ret` pops the stub
 * address and leaves the signal number in the argument slot):
 *
 *   0x3FF000  signal number          <- handler's argument (esp)
 *   0x3FEFFC  USER_SIGRETURN_ENTRY   <- the handler's return address
 *   0x3FEFF8  handler address
 *   0x3FEFF4  magic (frame validity)
 *   0x3FEFF0  ... saved context, 80 bytes ...
 *   0x3FEFA0
 * ------------------------------------------------------------------ */

/* Kernel-side saved context.  The order is a contract with the unwinder
 * in kernel/asm.s (sys_sigreturn reads these offsets literally) and
 * with include/memlayout.h; change all three together. */
struct sig_context {
    unsigned long magic;     /*  0 */
    unsigned long retaddr;   /*  4 */
    unsigned long handler;   /*  8 */
    unsigned long signo;     /* 12 */
    unsigned long eip;       /* 16 */
    unsigned long cs;        /* 20 */
    unsigned long eflags;    /* 24 */
    unsigned long esp;       /* 28 */
    unsigned long ss;        /* 32 */
    unsigned long gs;        /* 36 */
    unsigned long fs;        /* 40 */
    unsigned long es;        /* 44 */
    unsigned long ds;        /* 48 */
    unsigned long eax;       /* 52 */
};

/* Filled by deliver_signal(), consumed by sys_sigreturn (kernel/asm.s).
 * One slot is enough: only the current task is ever in a syscall. */
struct sig_context sigreturn_frame;

/* kernel/asm.s reserves exactly 56 bytes for sigreturn_frame and reads
 * it with literal offsets; a field added here without updating it would
 * silently corrupt the resumed context. */
STATIC_ASSERT(sizeof(struct sig_context) == 56, sig_context_is_56_bytes);

/* Snapshot of the interrupted context, mirrored for the handler to look
 * at.  The block is written just BELOW the interrupted user esp (see
 * deliver_signal):
 *     (base+4) signal number  <- the handler's argument
 *     (base+0) USER_SIGRETURN_ENTRY   <- the handler's return address
 *     (base+8) this snapshot, 80 bytes
 * The return address is at the LOWEST address of the header because the
 * handler's own frames grow down into the snapshot area, never over it;
 * the kernel-side struct sig_context above stays authoritative. */
struct user_regs {
    unsigned long eip, cs, eflags, esp, ss;
    unsigned long gs, fs, es, ds, eax;
    unsigned long ebx, ecx, edx, esi, edi, ebp;
};

/* Offsets into the system_call frame at ret_from_sys_call (see
 * boot/head.s: 5 pushes + 7 saved words below the iret frame).  The
 * fork() frame builders in this file use the same numbers. */
#define UFRAME_ARG0   0    /* ebx */
#define UFRAME_ARG1   4    /* ecx */
#define UFRAME_ARG2   8    /* edx */
#define UFRAME_ARG3   12   /* esi */
#define UFRAME_ARG4   16   /* edi */
#define UFRAME_ARG5   20   /* ebp */
#define UFRAME_EAX    24   /* saved eax (syscall return value) */
#define UFRAME_GS     28
#define UFRAME_FS     32
#define UFRAME_ES     36
#define UFRAME_DS     40
#define UFRAME_EIP    44
#define UFRAME_CS     48
#define UFRAME_EFLAGS 52
#define UFRAME_ESP    56
#define UFRAME_SS     60

/* The Ring3 stub the handler returns into.  It cannot live in the
 * kernel, and user code cannot be loaded at a fixed address without
 * another build step, so the kernel writes these twelve bytes into the
 * one page above the user stack:
 *
 *     movl $USER_SIGRETURN_SYSCALL, %eax    (B8 43 00 00 00)
 *     int  $0x80                            (CD 80)
 *     movl USER_SIGRETURN_STUB_ARG, %eax    (A1 14 F0 3F 00)
 *     jmp  *%eax                            (FF E0)
 *
 * where USER_SIGRETURN_STUB_ARG holds the handler the kernel chose.
 * That page is deliberately NOT user-readable (mm/memcheck.c): the
 * address beside the stub is a kernel-chosen value the user must not be
 * able to read or to change. */
static const unsigned char sigreturn_stub[] = {
    0xB8, USER_SIGRETURN_SYSCALL & 0xFF, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xA1, (USER_SIGRETURN_STUB_ARG) & 0xFF,
    ((USER_SIGRETURN_STUB_ARG) >> 8) & 0xFF,
    ((USER_SIGRETURN_STUB_ARG) >> 16) & 0xFF,
    ((USER_SIGRETURN_STUB_ARG) >> 24) & 0xFF,
    0xFF, 0xE0,
    0xEB, 0xFE                    /* jmp .-2 : never fall off the end */
};

STATIC_ASSERT(sizeof(sigreturn_stub) <= 20, sigreturn_stub_fits);

static void install_sigreturn_stub(void)
{
    int i;

    /* Idempotent, so it is simply rewritten on every delivery. */
    for (i = 0; i < (int)sizeof(sigreturn_stub); i++)
        put_fs_byte(sigreturn_stub[i], (char *)(USER_SIGRETURN_ENTRY + i));
}

static void deliver_signal(int sig, unsigned long handler)
{
    extern long syscall_esp;
    unsigned char *kf = (unsigned char *)syscall_esp;
    unsigned long user_esp = *(unsigned long *)(kf + UFRAME_ESP);
    unsigned long frame = user_esp - 4 - SIGFRAME_BYTES;
    struct user_regs *uf;

    /* The signal block is written below the interrupted esp.  Keep it
       inside the user area: a wild esp must not make the kernel write
       (through the identity map) into kernel memory. */
    if (frame < CHILD_USER_STACK_END || user_esp > USER_STACK_END) {
        printk("signal: refusing to build a frame for esp=0x%lx\n", user_esp);
        return;
    }
    uf = (struct user_regs *)(frame + 8);

    /* The context the interrupted syscall was about to return to.  The
       syscall is still on the kernel stack, so syscall_esp is valid. */
    sigreturn_frame.magic = SIGFRAME_MAGIC;
    sigreturn_frame.retaddr = USER_SIGRETURN_ENTRY;
    sigreturn_frame.handler = handler;
    sigreturn_frame.signo = (unsigned long)sig;
    sigreturn_frame.eip = *(unsigned long *)(kf + UFRAME_EIP);
    sigreturn_frame.cs = *(unsigned long *)(kf + UFRAME_CS);
    sigreturn_frame.eflags = *(unsigned long *)(kf + UFRAME_EFLAGS);
    sigreturn_frame.esp = *(unsigned long *)(kf + UFRAME_ESP);
    sigreturn_frame.ss = *(unsigned long *)(kf + UFRAME_SS);
    sigreturn_frame.gs = *(unsigned long *)(kf + UFRAME_GS);
    sigreturn_frame.fs = *(unsigned long *)(kf + UFRAME_FS);
    sigreturn_frame.es = *(unsigned long *)(kf + UFRAME_ES);
    sigreturn_frame.ds = *(unsigned long *)(kf + UFRAME_DS);
    sigreturn_frame.eax = *(unsigned long *)(kf + UFRAME_EAX);

    /* Snapshot for the handler to look at (the kernel copy above is the
       authoritative one). */
    uf->eip = sigreturn_frame.eip;
    uf->cs = sigreturn_frame.cs;
    uf->eflags = sigreturn_frame.eflags;
    uf->esp = sigreturn_frame.esp;
    uf->ss = sigreturn_frame.ss;
    uf->gs = sigreturn_frame.gs;
    uf->fs = sigreturn_frame.fs;
    uf->es = sigreturn_frame.es;
    uf->ds = sigreturn_frame.ds;
    uf->eax = sigreturn_frame.eax;
    uf->ebx = *(unsigned long *)(kf + UFRAME_ARG0);
    uf->ecx = *(unsigned long *)(kf + UFRAME_ARG1);
    uf->edx = *(unsigned long *)(kf + UFRAME_ARG2);
    uf->esi = *(unsigned long *)(kf + UFRAME_ARG3);
    uf->edi = *(unsigned long *)(kf + UFRAME_ARG4);
    uf->ebp = *(unsigned long *)(kf + UFRAME_ARG5);

    /* Header: [retaddr][signo] with retaddr lowest. */
    put_fs_long(USER_SIGRETURN_ENTRY, (unsigned long *)frame);
    put_fs_long((unsigned long)sig, (unsigned long *)(frame + 4));

    install_sigreturn_stub();
    put_fs_long(handler, (unsigned long *)USER_SIGRETURN_STUB_ARG);

    /* Enter the handler.  The handler's argument is at [esp+4] (where a
       normal call would leave its first argument) and its return
       address at [esp], so a plain "ret" lands on the stub. */
    *(unsigned long *)(kf + UFRAME_EIP) = handler;
    *(unsigned long *)(kf + UFRAME_ESP) = frame;
}

/* Signal delivery: called at every syscall return when the current task
   has pending signals. */
void do_signal(void)
{
    extern int syscall_cpl;
    int sig;

    if (!current->signal)
        return;

    for (sig = 1; sig < 32; sig++) {
        unsigned long handler;

        if (!(current->signal & (1 << sig)))
            continue;
        current->signal &= ~(1 << sig);

        /* An ignored signal never reaches the task. */
        if (current->handlers[sig] == SIG_IGN)
            continue;

        handler = current->handlers[sig];
        if (handler != SIG_DFL) {
            if (syscall_cpl == 3) {
                /* `signal()` semantics: a handler is reset to SIG_DFL
                   before it runs, except for SIGCHLD (0.01 did the
                   same).  Interrupted syscalls are not restarted; the
                   handler resumes at the next instruction. */
                if (sig != SIGCHLD)
                    current->handlers[sig] = SIG_DFL;
                deliver_signal(sig, handler);
                return;
            }
            /* Ring0 caller: no user context to rewrite, fall through to
               the default action. */
        }

        switch (sig) {
        case SIGINT:
        case SIGQUIT:
        case SIGKILL:
        case SIGSEGV:
        case SIGPIPE:
        case SIGALRM:
            sys_exit(128 + sig);   /* never returns */
        default:
            break;                 /* default action: ignore */
        }
    }
}
