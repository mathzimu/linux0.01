#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <string.h>
#include <asm/system.h>

extern unsigned long _end;

unsigned long memory_end = 0;
unsigned long *mem_map = NULL;
int max_map_nr = 0;

/* The PTE for a linear address, and the PDE that points at its page
 * table.  Only PDE[0] exists in this kernel (boot/head.s maps 0..4MB
 * with a single page table), so every address must be inside the
 * identity map. */
#define PTE_PTR(a) ((unsigned long *)(PAGE_TABLE_0 + (((a) >> 12) << 2)))
#define PDE_PTR(a) ((unsigned long *)(PAGE_DIRECTORY + (((a) >> 22) << 2)))

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    int i;
    int map_size;

    memory_end = end_mem;
    max_map_nr = (end_mem - LOW_MEM) / PAGE_SIZE;
    map_size = max_map_nr * sizeof(unsigned long);

    mem_map = (unsigned long *)(end_mem - map_size);

    for (i = 0; i < max_map_nr; i++)
        mem_map[i] = 0;

    /* Reserve the page directory and page table 0 */
    mem_map[0] = USED;  /* PAGE_DIRECTORY */
    mem_map[1] = USED;  /* PAGE_TABLE_0 */

    /* Reserve the pages occupied by mem_map itself */
    {
        unsigned long map_start = (unsigned long)mem_map;
        unsigned long map_end = map_start + map_size;
        int first = MAP_NR(map_start & ~(PAGE_SIZE - 1));
        int last = MAP_NR(map_end - 1);
        int j;
        for (j = first; j <= last && j < max_map_nr; j++)
            mem_map[j] = USED;
    }

    /* Reserve kernel image pages (from LOW_MEM up to start_mem) */
    if (start_mem > LOW_MEM) {
        int kstart = 0;
        int kend = MAP_NR(start_mem - 1);
        int j;
        for (j = kstart; j <= kend && j < max_map_nr; j++)
            if (mem_map[j] == 0)
                mem_map[j] = USED;
    }

    /* Everything from USER_PROG_START up belongs to user space or to the
       kernel buffer cache: the fixed user regions (program image, heap,
       fork child stack, user stack), the cache, and the page allocator's
       own bitmap at the very top.  The allocator must never hand any of
       it out — without this, get_free_page() could reach a page inside
       the user stack, and mem_check() rejects that at boot. */
    {
        int first = MAP_NR(USER_PROG_START);
        int j;
        for (j = first; j < max_map_nr; j++)
            mem_map[j] = USED;
    }
}

unsigned long get_free_page(void)
{
    unsigned long addr;
    int i;

    for (i = 0; i < max_map_nr; i++) {
        if (mem_map[i] != 0) continue;
        mem_map[i] = 1;
        addr = LOW_MEM + i * PAGE_SIZE;

        /* The +0x100000 offset means the bitmap index equals the address
           minus LOW_MEM, so this comparison really is "is this page
           inside the region reserved for the user program image?".  A
           page handed out up there would be the image's physical page:
           the kernel would then scribble on a running program (or the
           program on the kernel) with nothing to notice it. */
        if (addr >= KERNEL_POOL_END)
            panic("get_free_page: page allocator reached the user program "
                  "image; widen the layout in include/linux/memmap.h");

        memset((char *)addr, 0, PAGE_SIZE);
        return addr;
    }

    return 0;
}

void free_page(unsigned long addr)
{
    int i;

    if (addr < LOW_MEM) return;
    if (addr >= memory_end) return;
    if (addr & (PAGE_SIZE - 1)) return;

    i = MAP_NR(addr);
    if (i >= max_map_nr) return;
    if (mem_map[i] <= 0) return;
    if (mem_map[i] >= USED) return;

    mem_map[i]--;
}

/* Ring3 access control (memory isolation).
   Page table 0 identity-maps the whole 0..4MB with U/S cleared
   (supervisor-only).  This routine ORs the U/S bit into the PTEs of
   the given range, marking exactly the user program / heap / stack
   pages as user-accessible; the kernel (including the buffer cache
   and task pages) stays supervisor-only.  Ring0 can still touch
   everything (U/S only constrains Ring3). */
void grant_user_pages(unsigned long from, unsigned long size)
{
    unsigned long end = (from + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    unsigned long a;

    for (a = from & ~(PAGE_SIZE - 1); a < end; a += PAGE_SIZE) {
        if (a >= IDENTITY_MAP_TOP)
            break;                    /* only PDE[0] is mapped */
        *PTE_PTR(a) |= 4;             /* _PAGE_USER */
    }
    write_cr3(read_cr3());            /* flush the TLB */
}

int free_page_tables(unsigned long from, unsigned long size)
{
    unsigned long *pg_dir;
    unsigned long nr;
    unsigned long dir_index;

    if (from & (IDENTITY_MAP_SIZE - 1))
        panic("free_page_tables: from must be identity-map aligned");

    size = (size + IDENTITY_MAP_SIZE - 1) / IDENTITY_MAP_SIZE;

    for (nr = 0; nr < size; nr++) {
        unsigned long *pg_table;
        dir_index = (from >> 22) + nr;
        if (dir_index >= 1024) break;
        pg_dir = PDE_PTR(dir_index << 22);
        if (*pg_dir & 1) {
            int j;
            pg_table = (unsigned long *)(0xFFFFF000 & *pg_dir);
            for (j = 0; j < 1024; j++) {
                if (pg_table[j] & 1)
                    free_page(pg_table[j] & 0xFFFFF000);
            }
            free_page((unsigned long)pg_table & 0xFFFFF000);
            *pg_dir = 0;
        }
    }

    write_cr3(read_cr3());
    return 0;
}

void do_no_page(unsigned long error_code, unsigned long eip, unsigned long address)
{
    unsigned long pde = 0, pte = 0;

    if (address < IDENTITY_MAP_TOP) {
        pde = *PDE_PTR(address);
        pte = *PTE_PTR(address);
    }

    printk("\nPAGE FAULT: addr=0x%x err=0x%x eip=0x%x pid=%d state=%d\n",
           address, error_code, eip, current->pid, current->state);
    printk("  cr3=0x%lx pde[0x%x]=0x%lx pte[0x%x]=0x%lx\n",
           read_cr3(), (unsigned)(address >> 22), pde,
           (unsigned)(address >> 12), pte);

    /* A bad pointer must not crash the whole kernel: terminate only the
       faulting task with SIGSEGV's default action (128 + 11 = 139).
       The task becomes a zombie; its parent's waitpid() reaps it.
       NOTE: sys_exit() switches away and never returns, so a fault
       raised while already inside the kernel (i.e. by the kernel
       itself) also ends the current task — but it cannot re-enter this
       handler, which a bare return would. */
    sys_exit(128 + 11);        /* never returns */
}
