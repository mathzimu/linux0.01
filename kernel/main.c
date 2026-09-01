#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <asm/system.h>

extern unsigned long _end;
extern void shell_main(void);
extern int sys_setup(void);

void main(void)
{
    unsigned long memory_start, memory_end;
    unsigned short ext_kb;

    /* int 15/88 stored extended memory (KB above 1MB) at physical 0x10002 */
    ext_kb = *((unsigned short *)0x10002);
    if (ext_kb == 0)
        memory_end = 0x400000; /* 4MB default */
    else
        memory_end = (1 << 20) + ((unsigned long)ext_kb << 10);

    if (memory_end > 0x400000)   /* only the first 4MB is mapped */
        memory_end = 0x400000;

    memory_end &= 0xFFFFF000;

    if (memory_end < 0x300000)
        memory_end = 0x400000;

    memory_start = (unsigned long) &_end;
    memory_start += 0x1000;

    mem_init(memory_start, memory_end);
    buffer_init(memory_end - 0x100000);

    /* Memory isolation: everything is supervisor-only by default.
       Grant user-mode access to the fixed user regions — heap
       [0x310000, 0x3FE000) and stack [0x3FE000, 0x400000).  The
       program image at 0x200000 is granted at exec time. */
    grant_user_pages(0x310000, 0x400000 - 0x310000);

    tty_init();

    if (sys_setup() < 0)
        printk("Warning: no root filesystem found\n");

    sched_init();

    sti();

    shell_main();
}
