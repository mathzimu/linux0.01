#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/head.h>
#include <asm/system.h>

int sys_fork(void)
{
    struct task_struct *p;
    int i, pid;

    p = (struct task_struct *)get_free_page();
    if (!p)
        return -1;

    pid = 0;
    for (i = 0; i < NR_TASKS; i++) {
        if (task[i]) continue;
        task[i] = p;
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
    p->tss.back_link = 0;
    p->tss.esp0 = (unsigned long)p + PAGE_SIZE;
    p->tss.ss0 = KERNEL_DS;
    p->tss.eax = 0;
    p->tss.ebx = current->tss.ebx;
    p->tss.ecx = current->tss.ecx;
    p->tss.edx = current->tss.edx;
    p->tss.esi = current->tss.esi;
    p->tss.edi = current->tss.edi;
    p->tss.ebp = current->tss.ebp;
    p->tss.esp = current->tss.esp;
    p->tss.eip = current->tss.eip;
    p->tss.eflags = current->tss.eflags;
    p->tss.cs = current->tss.cs;
    p->tss.ss = current->tss.ss;
    p->tss.ds = current->tss.ds;
    p->tss.es = current->tss.es;
    p->tss.fs = current->tss.fs;
    p->tss.gs = current->tss.gs;

    i = (int)p;
    i >>= 4;
    p->tss.cr3 = read_cr3();

    {
        int nr = (int)(p - task[0]);
        int tss_entry = 8 + nr * 2;
        int ldt_entry = tss_entry + 1;
        struct desc_struct *p_desc = (struct desc_struct *)(&_gdt) + tss_entry;

        p_desc->a = ((unsigned long)&p->tss) & 0xFFFF;
        p_desc->a |= ((((unsigned long)&p->tss) >> 16) << 16) & 0xFF000000;
        p_desc->a |= 0x89000000;
        p_desc->b = ((unsigned long)&p->tss) & 0xFF000000;
        p_desc->b |= 0x00408900;
        p_desc->b |= 0x00000040;

        p_desc = (struct desc_struct *)(&_gdt) + ldt_entry;
        p_desc->a = ((unsigned long)&p->ldt) & 0xFFFF;
        p_desc->a |= ((((unsigned long)&p->ldt) >> 16) << 16) & 0xFF000000;
        p_desc->a |= 0x82000000;
        p_desc->b = ((unsigned long)&p->ldt) & 0xFF000000;
        p_desc->b |= 0x00408200;
        p_desc->b |= 0x00000040;

        p->tss.ldt = ldt_entry * 8;
    }

    p->ldt[0].a = 0x0000FFFF;
    p->ldt[0].b = 0x00CFFA00;
    p->ldt[1].a = 0x0000FFFF;
    p->ldt[1].b = 0x00CFF200;

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

    free_page((unsigned long)current);
    schedule();

    return ret;
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
