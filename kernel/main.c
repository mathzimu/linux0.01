#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <asm/system.h>

extern unsigned long _end;
extern void shell_main(void);
extern int sys_setup(void);

/* mm/memcheck.c — refuses to boot on an inconsistent memory map. */
extern void mem_check(void);

void main(void)
{
    unsigned long phys_mem_start;
    unsigned long phys_mem_end;
    unsigned short ext_kb;

    /* setup.s stored the INT 15h result — extended memory in KB above
       1MB — in the first boot parameter block. */
    ext_kb = *((unsigned short *)BOOT_PARAM_ADDR);
    if (ext_kb == 0)
        phys_mem_end = PHYS_MEM_TOP;                 /* 4MB assumption */
    else
        phys_mem_end = (1 << 20) + ((unsigned long)ext_kb << 10);

    /* Only the first 4MB is mapped (boot/head.s fills PDE[0] alone), so
       usable RAM cannot extend past the identity map.  mem_check()
       panics if this leaves less than the layout needs. */
    if (phys_mem_end > IDENTITY_MAP_TOP)
        phys_mem_end = IDENTITY_MAP_TOP;

    phys_mem_end &= 0xFFFFF000;

    if (phys_mem_end < USER_STACK_END)
        phys_mem_end = IDENTITY_MAP_TOP;

    phys_mem_start = (unsigned long)&_end;
    phys_mem_start += 0x1000;

    mem_init(phys_mem_start, phys_mem_end);

    /* Anchor the buffer cache explicitly: buffer_init() takes the address
       the cache ends at.  It used to be called with memory_end-1MB, which
       put the cache at [0x27C000, 0x300000) - inside the user program
       image region. */
    buffer_init((long)BUFFER_CACHE_TOP);

    /* Everything that carves up the 0..4MB identity map has now had its
       say: allocator, kernel heap, buffer cache and the fixed user
       regions.  Verify the map before anything can write through it. */
    mem_check();

    /* Memory isolation: everything is supervisor-only by default.  Grant
       user-mode access to the fixed user regions — the heap
       [USER_HEAP_START, USER_HEAP_END) and the stack
       [USER_STACK_FLOOR, USER_TAIL_TOP).  The stack pages are where the
       kernel later drops the argc/argv block and the sigreturn stub; the
       very top of RAM stays supervisor-only because it holds the page
       allocator's bitmap.  The program image at USER_PROG_START is
       granted at exec time (kernel/sys.c). */
    grant_user_pages(USER_HEAP_START, USER_HEAP_END - USER_HEAP_START);
    grant_user_pages(USER_STACK_FLOOR, USER_TAIL_TOP - USER_STACK_FLOOR);

    tty_init();

    if (sys_setup() < 0)
        printk("Warning: no root filesystem found\n");

    sched_init();

    sti();

    shell_main();
}
