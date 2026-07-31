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
    init_task.tss.esp0 = (unsigned long)&_end + 0x1000;

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
}

void schedule(void)
{
    int next, c;
    struct task_struct **p;

    while (1) {
        c = -1;
        next = -1;
        p = &task[NR_TASKS];

        while (--p >= &task[0]) {
            if (*p == NULL) continue;
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c) {
                c = (*p)->counter;
                next = (int)(p - task);
            }
        }

        if (c > 0) break;

        if (c < 0) {
            unsigned long eflags;
            __asm__ volatile("pushfl; popl %0" : "=r"(eflags));
            if (eflags & 0x200)
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
