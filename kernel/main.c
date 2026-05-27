#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <asm/system.h>

extern unsigned long _end;
extern void shell_main(void);

void move_to_user_mode(void);

void main(void)
{
    unsigned long memory_start, memory_end;

    memory_end = (unsigned long) *((unsigned short *)0x100002);
    if (memory_end == 0)
        memory_end = 0x400000; /* 4MB default */

    memory_end &= 0xFFFFFF00;
    memory_start = (unsigned long) &_end;
    memory_start += 0x1000;

    mem_init(memory_start, memory_end);
    buffer_init(memory_end - 0x100000);

    tty_init();

    sched_init();

    sti();

    move_to_user_mode();

    shell_main();
}

void move_to_user_mode(void)
{
    __asm__ volatile(
        "movl %%esp, %%eax\n\t"
        "pushl $0x23\n\t"
        "pushl %%eax\n\t"
        "pushfl\n\t"
        "pushl $0x1B\n\t"
        "pushl $1f\n\t"
        "iret\n"
        "1:\n\t"
        "movl $0x23, %%eax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        :
        : "eax");
}
