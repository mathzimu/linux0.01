#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memmap.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/hdreg.h>
#include <signal.h>
#include <string.h>
#include <asm/system.h>

extern unsigned long _end;

unsigned long memory_end = 0;
unsigned long *mem_map = NULL;
int max_map_nr = 0;

/* The page directory boot/head.s built: identity 0..16MB, supervisor
 * only, no user window.  Every process gets its own directory (with the
 * same PDE[0..KERNEL_PT_COUNT-1] entries) and the kernel-only tasks
 * (task[0], the write-back task) keep using this one. */
unsigned long kernel_pg_dir = PAGE_DIRECTORY;

/* --- addressing helpers -------------------------------------------
 * The kernel is identity mapped, so a page-table page or a user data
 * page is reachable at its own physical address whatever CR3 holds.
 * That is what makes the functions below independent of the current
 * process: they take an explicit page directory. */
#define PT_MASK      0xFFFFF000
#define PT_FLAGS     0x00000FFF

static unsigned long *pde_slot(unsigned long pgdir, unsigned long va)
{
    return (unsigned long *)pgdir + (va >> 22);
}

/* The page table entry for a linear address, or NULL when the page
 * directory entry that should point at its table is empty. */
static unsigned long *pte_slot(unsigned long pgdir, unsigned long va)
{
    unsigned long pde = *pde_slot(pgdir, va);

    if (!(pde & PTE_PRESENT))
        return NULL;
    return (unsigned long *)(pde & PT_MASK) + ((va >> 12) & 0x3FF);
}

/* The single page table a user address space has (the user window is one
 * PDE).  NULL if it is not there. */
static unsigned long *user_pt(unsigned long pgdir)
{
    unsigned long pde;

    if (!pgdir)
        return NULL;
    pde = *pde_slot(pgdir, USER_BASE);
    if (!(pde & PTE_PRESENT))
        return NULL;
    return (unsigned long *)(pde & PT_MASK);
}

/* Is [addr, addr+len) inside one of the regions a user process is
 * allowed to touch?  Everything else in the window (the guard hole
 * between heap and stack floor, and anything outside the window at all)
 * is a fatal access. */
int user_addr_ok(unsigned long addr, unsigned long len)
{
    unsigned long end = addr + len;

    if (end < addr)                       /* wrap */
        return 0;
    if (addr >= USER_PROG_START && end <= USER_PROG_END)   return 1;
    if (addr >= USER_HEAP_START && end <= USER_HEAP_END)   return 1;
    if (addr >= USER_STACK_FLOOR && end <= USER_STACK_TOP) return 1;
    if (addr >= USER_STACK_TOP && end <= USER_TAIL_TOP)    return 1;
    return 0;
}

/* --- physical page allocator --------------------------------------- */

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    int i;
    int map_size;
    int ktables;

    memory_end = end_mem;
    max_map_nr = (end_mem - LOW_MEM) / PAGE_SIZE;
    map_size = max_map_nr * sizeof(unsigned long);

    mem_map = (unsigned long *)(end_mem - map_size);

    for (i = 0; i < max_map_nr; i++)
        mem_map[i] = 0;

    /* Everything below LOW_MEM (boot code, kernel image, VGA, BIOS) is
       simply not in the map: index 0 is LOW_MEM itself. */

    /* The page directory and the kernel identity page tables live just
       above 1MB and are shared by every address space, so they must
       never be handed out.  start_mem (the linker's _end, rounded up)
       is well below them, so reserve them explicitly. */
    ktables = (int)((KERNEL_TABLES_END - LOW_MEM + PAGE_SIZE - 1) / PAGE_SIZE);
    for (i = 0; i < ktables && i < max_map_nr; i++)
        mem_map[i] = USED;

    /* Reserve the pages of mem_map itself (the top of RAM) */
    {
        unsigned long map_start = (unsigned long)mem_map;
        unsigned long map_end = map_start + map_size;
        int first = MAP_NR(map_start & ~(PAGE_SIZE - 1));
        int last = MAP_NR(map_end - 1);
        int j;
        for (j = first; j <= last && j < max_map_nr; j++)
            mem_map[j] = USED;
    }

    /* The kernel image: from LOW_MEM up to start_mem.  (It actually
       lives below LOW_MEM, so this is normally empty; it is kept as a
       guard for the day somebody moves the load address.) */
    if (start_mem > LOW_MEM + (unsigned long)ktables * PAGE_SIZE) {
        int kstart = (int)((LOW_MEM + (unsigned long)ktables * PAGE_SIZE - LOW_MEM)
                           / PAGE_SIZE);
        int kend = MAP_NR(start_mem - 1);
        int j;
        for (j = kstart; j <= kend && j < max_map_nr; j++)
            if (mem_map[j] == 0)
                mem_map[j] = USED;
    }

    /* Everything else is free and shared between kernel objects (task
       pages, pipes) and user pages — that is the point of M3.  The
       buffer cache marks its own pages USED in buffer_init(), which runs
       right after this function. */
}

unsigned long get_free_page(void)
{
    unsigned long addr;
    int i;

    for (i = 0; i < max_map_nr; i++) {
        if (mem_map[i] != 0)
            continue;
        mem_map[i] = 1;
        addr = LOW_MEM + (unsigned long)i * PAGE_SIZE;
        memset((char *)addr, 0, PAGE_SIZE);
        return addr;
    }

    /* Out of memory.  Since M3 the pool is not fenced off from user
       memory any more, so this is a normal condition rather than the
       "allocator walked into the user image" panic it used to be. */
    return 0;
}

/* Drop one reference to a page.  mem_map[] is a reference count: 0 is
 * free, 1..USED-1 is "in use by that many address spaces", USED is a
 * permanent reservation.  Copy-on-write is what makes the middle range
 * meaningful. */
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

/* --- per-process address spaces ------------------------------------ */

/* A fresh address space: one page directory plus one page table for the
 * user window, with the shared kernel identity entries filled in and no
 * user pages mapped yet.  Returns 0 when out of memory. */
unsigned long alloc_user_pgdir(void)
{
    unsigned long pgdir = get_free_page();
    unsigned long pt;
    int i;

    if (!pgdir)
        return 0;

    pt = get_free_page();
    if (!pt) {
        free_page(pgdir);
        return 0;
    }

    for (i = 0; i < KERNEL_PT_COUNT; i++)
        ((unsigned long *)pgdir)[i] =
            (PAGE_TABLE_0 + ((unsigned long)i << 12)) |
            PTE_PRESENT | PTE_RW;                    /* supervisor-only */
    ((unsigned long *)pgdir)[USER_PDE_INDEX] =
        pt | PTE_PRESENT | PTE_RW | PTE_USER;

    return pgdir;
}

/* Map one 4KB page of pgdir's user window at `va` and return the
 * physical page, or 0 when there is no memory left.  The page is zeroed
 * (get_free_page does that) and belongs to the caller. */
unsigned long alloc_user_page(unsigned long pgdir, unsigned long va)
{
    unsigned long pa = get_free_page();
    unsigned long *pte;

    if (!pa)
        return 0;

    pte = pte_slot(pgdir, va);
    if (!pte) {
        free_page(pa);
        return 0;
    }

    *pte = pa | PTE_PRESENT | PTE_RW | PTE_USER;
    return pa;
}

void map_user_page(unsigned long pgdir, unsigned long va, unsigned long pa,
                   unsigned long flags)
{
    unsigned long *pte = pte_slot(pgdir, va);

    if (pte)
        *pte = (pa & PT_MASK) | flags;
}

/* fork(): give the child its own address space and let both share every
 * page read-only.  The first write to a shared page faults, and
 * un_wp_page() gives the writer a private copy then.
 *
 * This is what finally makes fork() mean what POSIX says it means: the
 * child's writes to heap, stack or data are invisible to the parent.
 * Before M3 the parent's stack was copied but code and heap were simply
 * shared — and a child that execve()d wrote the new image straight over
 * the parent's code. */
int copy_page_tables(unsigned long from_pgdir, unsigned long to_pgdir)
{
    unsigned long *spt = user_pt(from_pgdir);
    unsigned long *dpt = user_pt(to_pgdir);
    int i;
    int shared = 0;

    if (!spt || !dpt)
        return -1;

    for (i = 0; i < 1024; i++) {
        unsigned long pte = spt[i];
        unsigned long pa;

        if (!(pte & PTE_PRESENT))
            continue;
        pa = pte & PT_MASK;

        /* One more user of this physical page, and neither side may
           write it until it has a copy of its own. */
        if (mem_map[MAP_NR(pa)] < USED)
            mem_map[MAP_NR(pa)]++;

        spt[i] = (pte & ~PTE_RW) | PTE_COW;
        dpt[i] = (pte & ~PTE_RW) | PTE_COW;
        shared++;
    }

    /* The parent's entries just became read-only: flush the TLB. */
    write_cr3(read_cr3());
    return shared;
}

/* Write fault on a copy-on-write page: give the current process a
 * private copy and make it writable again.  Called from the page-fault
 * handler, so it must not sleep. */
static int un_wp_page(unsigned long address)
{
    unsigned long *pte;
    unsigned long old, new;

    if (!current->pg_dir)
        return -1;

    pte = pte_slot(current->pg_dir, address);
    if (!pte || !(*pte & PTE_PRESENT))
        return -1;

    old = *pte & PT_MASK;
    new = get_free_page();
    if (!new)
        return -1;

    memcpy((char *)new, (char *)old, PAGE_SIZE);

    *pte = new | PTE_PRESENT | PTE_RW | PTE_USER;
    free_page(old);                 /* release our share of the old page */
    write_cr3(current->pg_dir);     /* that entry was cached in the TLB */
    return 0;
}

/* Release every user page of an address space and the page table that
 * maps them.  The page directory itself is freed by the caller. */
int free_page_tables(unsigned long pgdir, unsigned long from, unsigned long size)
{
    unsigned long *pt;
    int i;

    if (!pgdir)
        return -1;
    if (from < USER_BASE || from + size > USER_WINDOW_TOP)
        panic("free_page_tables: only the user window is per-process");

    pt = user_pt(pgdir);
    if (!pt)
        return 0;

    for (i = 0; i < 1024; i++) {
        if (pt[i] & PTE_PRESENT)
            free_page(pt[i] & PT_MASK);
        pt[i] = 0;
    }

    free_page((unsigned long)pt);
    *pde_slot(pgdir, from) = 0;
    return 0;
}

/* Tear down a whole address space (execve replacing an image, exit). */
void free_user_space(unsigned long pgdir)
{
    if (!pgdir)
        return;
    free_page_tables(pgdir, USER_BASE, USER_WINDOW_SIZE);
    free_page(pgdir);
}

/* Map a flat (non-ELF) image at USER_PROG_START in a fresh address space.
 * Used by the kernel shell's embedded `user` command, which has no file
 * to read from.  Returns the page directory, or 0 on failure. */
unsigned long load_flat_image(const unsigned char *image, unsigned long len,
                              unsigned long *entry_out)
{
    unsigned long pgdir;
    unsigned long off;

    if (len == 0 || len > USER_PROG_END - USER_PROG_START)
        return 0;

    pgdir = alloc_user_pgdir();
    if (!pgdir)
        return 0;

    for (off = 0; off < len; off += PAGE_SIZE) {
        unsigned long pa = alloc_user_page(pgdir, USER_PROG_START + off);
        unsigned long n = len - off;

        if (!pa) {
            free_user_space(pgdir);
            return 0;
        }
        if (n > PAGE_SIZE)
            n = PAGE_SIZE;
        memcpy((char *)pa, image + off, n);
    }

    if (entry_out)
        *entry_out = USER_PROG_START;
    return pgdir;
}

/* --- page fault ---------------------------------------------------- */

/* Counters behind the shell's `memstat` command (init/shell.c).  They are
 * what makes demand paging and copy-on-write visible from Ring3: run a
 * program, then look at how many pages it actually caused. */
unsigned long nr_page_faults = 0;      /* every fault that reached us */
unsigned long nr_demand_pages = 0;     /* pages handed out on first touch */
unsigned long nr_cow_breaks = 0;       /* pages copied to break a COW share */
unsigned long nr_oom = 0;              /* faults that found no free page */

/* How many pages are free right now, and how many are mapped into the
 * calling task's address space. */
void mm_report(void)
{
    int i, free_pages = 0, mapped = 0;
    unsigned long *pt;

    for (i = 0; i < max_map_nr; i++)
        if (mem_map[i] == 0)
            free_pages++;

    if (current && current->pg_dir) {
        pt = user_pt(current->pg_dir);
        if (pt)
            for (i = 0; i < 1024; i++)
                if (pt[i] & PTE_PRESENT)
                    mapped++;
    }

    printk("mem: %d free pages (%dKB), pid %d has %d pages mapped\n",
           free_pages, free_pages * (PAGE_SIZE / 1024),
           current ? (int)current->pid : -1, mapped);
    printk("mem: %lu page faults, %lu demand pages, %lu COW breaks, "
           "%lu out-of-memory\n",
           nr_page_faults, nr_demand_pages, nr_cow_breaks, nr_oom);
    printk("mem: %lu disk interrupts (IRQ14)\n", hd_interrupt_count());
}

/* mm/page.s passes (error_code, eip, cr2).  Three cases matter:
 *
 *   1. write fault on a present page with the COW marker: break the
 *      share and return, so the faulting instruction is retried.  This
 *      works for Ring3 and for the kernel copying into a user buffer
 *      (sys_read after fork), because the retry happens either way.
 *   2. not-present fault inside a valid user region: demand paging.
 *      execve() maps only what the ELF file actually contains; the BSS,
 *      the heap and the stack are handed out on first touch.
 *   3. anything else: the access was illegal (or the machine is out of
 *      memory), so the faulting task dies with SIGSEGV and the kernel
 *      keeps running. */
void do_no_page(unsigned long error_code, unsigned long eip, unsigned long address)
{
    unsigned long pgdir = current ? current->pg_dir : 0;

    nr_page_faults++;

    if (error_code & PTE_PRESENT) {
        if ((error_code & PTE_RW) && pgdir) {
            unsigned long *pte = pte_slot(pgdir, address);

            if (pte && (*pte & PTE_COW)) {
                if (un_wp_page(address) == 0) {
                    nr_cow_breaks++;
                    return;
                }
                nr_oom++;
                printk("\nCOPY-ON-WRITE: out of memory for pid=%d addr=0x%lx\n",
                       current->pid, address);
                goto kill;
            }
        }
        goto bad;
    }

    if (pgdir && user_addr_ok(address, 1)) {
        if (alloc_user_page(pgdir, address & PT_MASK)) {
            nr_demand_pages++;
            write_cr3(pgdir);       /* the missing entry was cached as absent */
            return;
        }
        nr_oom++;
        printk("\nPAGE FAULT: out of memory for pid=%d addr=0x%lx\n",
               current->pid, address);
        goto kill;
    }

bad:
    {
        unsigned long pde = 0, pte = 0;

        if (pgdir) {
            unsigned long *p = pde_slot(pgdir, address);
            unsigned long *t = pte_slot(pgdir, address);
            pde = *p;
            if (t)
                pte = *t;
        }

        printk("\nPAGE FAULT: addr=0x%lx err=0x%lx eip=0x%lx pid=%d state=%d\n",
               address, error_code, eip, current->pid, current->state);
        printk("  cr3=0x%lx pde[0x%x]=0x%lx pte[0x%x]=0x%lx\n",
               read_cr3(), (unsigned)(address >> 22), pde,
               (unsigned)(address >> 12), pte);
    }

kill:
    /* A bad pointer must not crash the whole kernel: terminate only the
       faulting task with SIGSEGV's default action (128 + 11 = 139).
       The task becomes a zombie; its parent's waitpid() reaps it.
       NOTE: sys_exit() switches away and never returns, so a fault
       raised while already inside the kernel (i.e. by the kernel
       itself) also ends the current task — but it cannot re-enter this
       handler, which a bare return would. */
    sys_exit(128 + SIGSEGV);        /* never returns */
}
