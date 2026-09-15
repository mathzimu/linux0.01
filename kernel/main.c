#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/hdreg.h>
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
        phys_mem_end = PHYS_MEM_TOP;
    else
        phys_mem_end = (1 << 20) + ((unsigned long)ext_kb << 10);

    /* boot/head.s identity-maps KERNEL_IDENTITY_TOP bytes (16MB, four
       page tables), so usable RAM cannot extend past it.  mem_check()
       panics if this leaves less than the layout needs. */
    if (phys_mem_end > KERNEL_IDENTITY_TOP)
        phys_mem_end = KERNEL_IDENTITY_TOP;

    phys_mem_end &= 0xFFFFF000;

    if (phys_mem_end < MEMORY_END_MINIMUM)
        phys_mem_end = MEMORY_END_MINIMUM;

    phys_mem_start = (unsigned long)&_end;
    phys_mem_start += 0x1000;

    mem_init(phys_mem_start, phys_mem_end);

    /* Anchor the buffer cache explicitly: buffer_init() takes the address
       the cache ends at.  It used to be called with memory_end-1MB, which
       put the cache at [0x27C000, 0x300000) - inside the user program
       image region. */
    buffer_init((long)BUFFER_CACHE_TOP);

    /* Everything that carves up RAM has now had its say: allocator,
       kernel heap, kernel page tables, buffer cache.  Verify the map
       before anything can write through it. */
    mem_check();

    /* M3: nothing to grant any more.  Ring3 access is a property of the
       page-table entries a process's own address space is built from
       (alloc_user_page sets PTE_USER), so the kernel no longer pokes the
       U/S bit of a global page table at boot. */

    tty_init();

    /* sys_setup() below reads the superblock through the block layer, and
       that path can call schedule() - so the task table has to be real
       before it runs, not after.  Leaving it for sched_init() (below)
       meant schedule() walked a task[] slot full of BSS/0xffffffff during
       a boot-time disk read, faulted and reset the machine; a missing
       filesystem made that fatal instead of merely unhelpful.  The rest
       of the scheduler (root inode for pwd, GDT/TSS/LDT, timer) still
       happens in sched_init(), once the filesystem has been mounted. */
    sched_init_early();

    /* Let the disk's own interrupt wake the task waiting for it.  This
       only unmasks IRQ14.  Boot-time disk I/O polls rather than sleeps:
       hd_lock() turns interrupts on, so the driver cannot use "are
       interrupts on?" to decide, and until sched_init() runs there is no
       timer tick to time a sleeper out (see drivers/hd.c). */
    hd_init();

    if (sys_setup() < 0)
        printk("Warning: no root filesystem found\n");

    sched_init();

    sti();

    shell_main();
}
