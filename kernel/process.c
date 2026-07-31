#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
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
        pid = i + 1;
        break;
    }

    if (pid == 0) {
        free_page((unsigned long)p);
        return -1;
    }

    *p = *current;

    p->pid = pid;
    p->counter = p->priority;
    p->state = TASK_RUNNING;

    /* Build the child's kernel stack: copy the parent's live stack and set
       up a syscall-return frame, so the child resumes right after int 0x80
       with eax == 0 (fork returns 0 in the child, pid in the parent). */
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

    for (i = 0; i < NR_TASKS; i++) {
        if (task[i] == current) {
            task[i] = NULL;
            break;
        }
    }

    current->state = TASK_UNINTERRUPTIBLE;
    free_page((unsigned long)current);
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
