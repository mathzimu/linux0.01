#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/head.h>
#include <asm/system.h>

extern unsigned long _end;
extern void ltr(unsigned short sel);

int jiffies = 0;
struct task_struct *current = NULL;
struct task_struct *task[NR_TASKS] = {NULL,};

static struct task_struct init_task = {
    0,            /* state */
    15,           /* counter */
    15,           /* priority */
    0,            /* signal */
    {0},          /* tss */
    {NULL,},      /* filp */
    0,            /* uid */
    0,            /* pid */
    0,            /* pgrp */
    0,            /* session */
    0,            /* leader */
    0,0,0,0,      /* time */
    0,0,0,0,      /* code/data */
    0,0,          /* brk, stack */
    {{0,0},{0,0},{0,0}} /* ldt */
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

    init_task.tss.ss0 = KERNEL_DS;
    init_task.tss.esp0 = (unsigned long)&_end + 0x2000;

    /* Task 0 TSS at GDT entry 8, selector = 8*8 = 64 */
    p = (struct desc_struct *)(&_gdt) + 8;
    p->a = ((unsigned long)&init_task.tss) & 0xFFFF;
    p->a |= ((((unsigned long)&init_task.tss) >> 16) << 16) & 0xFF000000;
    p->a |= 0x89000000;
    p->b = ((unsigned long)&init_task.tss) & 0xFF000000;
    p->b |= 0x00408900;
    p->b |= 0x00000040;

    /* Task 0 LDT at GDT entry 9, selector = 9*8 = 72 */
    init_task.ldt[0].a = 0x0000FFFF;
    init_task.ldt[0].b = 0x00CFFA00;
    init_task.ldt[1].a = 0x0000FFFF;
    init_task.ldt[1].b = 0x00CFF200;

    p = (struct desc_struct *)(&_gdt) + 9;
    p->a = ((unsigned long)&init_task.ldt) & 0xFFFF;
    p->a |= ((((unsigned long)&init_task.ldt) >> 16) << 16) & 0xFF000000;
    p->a |= 0x82000000;
    p->b = ((unsigned long)&init_task.ldt) & 0xFF000000;
    p->b |= 0x00408200;
    p->b |= 0x00000040;

    init_task.tss.ldt = 72;
    ltr(64);

    __asm__ volatile(
        "movl $0x3f, %%eax\n\t"
        "movl $0x43, %%edx\n\t"
        "outl %%eax, %%edx\n\t"
        "movl $1193180, %%eax\n\t"
        "movl $0x40, %%edx\n\t"
        "outl %%eax, %%edx\n\t"
        : : : "eax", "edx"
    );
}

void schedule(void)
{
    int next, c;
    struct task_struct **p;

    while (1) {
        c = -1;
        next = 0;
        p = &task[NR_TASKS];

        while (--p >= &task[0]) {
            if (*p == NULL) continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {
                c = (*p)->counter;
                next = (int)(p - task);
            }
        }

        if (c) break;

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
            current = task[next];
            switch_to(next);
        }
    }
}

void do_timer(void)
{
    jiffies++;

    if (current->counter > 0) {
        current->counter--;
    }

    if (current->counter > 0) return;

    schedule();
}
