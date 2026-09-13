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

/* Reclaim a page so get_free_page() has something to hand out (defined
   further down, next to the eviction policy it implements). */
static int try_to_free_page(void);

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

    /* Nothing free.  Before giving up, try to *reclaim* something: a
       clean page can be dropped and paged back in later (B3).
       One eviction is not always enough: dropping a page that somebody
       else also maps returns no frame at all, so retry a bounded number
       of times — each pass has fewer shared pages to pick and will
       eventually reach one whose frame really comes back. */
    for (i = 0; i < 32; i++) {
        int j;

        if (!try_to_free_page())
            break;

        for (j = 0; j < max_map_nr; j++) {
            if (mem_map[j] != 0)
                continue;
            mem_map[j] = 1;
            addr = LOW_MEM + (unsigned long)j * PAGE_SIZE;
            memset((char *)addr, 0, PAGE_SIZE);
            return addr;
        }
    }

    /* Out of memory, and nothing more is reclaimable.  Since M3 the pool
       is not fenced off from user memory any more, so this is a normal
       condition rather than the "allocator walked into the user image"
       panic it used to be. */
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

/* --- reclaiming pages (B3) -----------------------------------------
 *
 * Demand paging without eviction is only half a memory manager: once the
 * frame pool is empty there is nothing to do but kill somebody.  With the
 * executable recorded as the backing store for image pages, three classes
 * of page can be reclaimed *safely*, which is what this scanner does:
 *
 *   1. an all-zero page — drop it and let the next touch re-create a zero
 *      page (a freshly grown heap or .bss is mostly this);
 *   2. a copy-on-write page that somebody else also maps (mem_map count
 *      is 2 or more) — just unmap it here; the other owner keeps the
 *      frame, and a later write faults it back as a private copy;
 *   3. a page of a *non-writable* image region (program text) — drop it;
 *      the next instruction fetch faults it back in from the executable.
 *
 * Anything else (a dirty anonymous heap/stack page) has no backing store
 * in this kernel — there is no swap area — so it is left alone.  That is
 * the honest boundary of what B3 buys: text, zeros and shares.
 *
 * The scan walks every task's user page table, because a process that
 * cannot allocate is usually not the one holding reclaimable pages.
 */

unsigned long nr_evicted = 0;      /* frames actually returned to the pool */
unsigned long nr_unmapped = 0;     /* COW shares dropped, frame kept      */

/* --- per-process address spaces ------------------------------------ */

static unsigned long evict_cursor = 0;   /* round-robin over task slots */

/* Is this user address inside a non-writable image region of `t`? */
static int is_clean_text(struct task_struct *t, unsigned long va)
{
    int i;

    if (!t->exe_inode)
        return 0;
    for (i = 0; i < t->nr_exe_regions; i++) {
        struct exe_region *r = &t->exe_regions[i];

        if (va < r->va || va >= r->va + r->memsz)
            continue;
        return (r->flags & 2) == 0;      /* PF_W clear = never modified */
    }
    return 0;
}

static int page_is_zero(unsigned long pa)
{
    unsigned long *p = (unsigned long *)pa;
    int i;

    for (i = 0; i < (int)(PAGE_SIZE / sizeof(unsigned long)); i++)
        if (p[i])
            return 0;
    return 1;
}

/* Try to free one page.  Returns 1 if a frame was returned to the pool
   (or a mapping dropped), 0 if there was nothing to reclaim.
 *
 * Policy, in order of preference:
 *   1. an all-zero page — reclaiming it costs a memset to bring back;
 *   2. a clean text page — costs a disk read to bring back;
 *   3. a COW share — costs nothing to drop, but returns no frame, so it
 *      is only used when nothing better exists.
 * Zero pages come first for a reason: the first version scanned in address
 * order, which always found the image (text) pages at the bottom of the
 * window, so every fault evicted text and the next fault read it back
 * from disk — a page-replacement test on a 4MB machine spent its time
 * waiting for the drive instead of exercising the mm.
 */
static unsigned long evict_trace = 0;

static int try_to_free_page(void)
{
    int pass, n;
    unsigned long fallback_pa = 0;
    unsigned long *fallback_pt = NULL;
    struct task_struct *fallback_t = NULL;

    for (pass = 0; pass < 2; pass++) {
        for (n = 0; n < NR_TASKS; n++) {
            struct task_struct *t = task[(evict_cursor + n) % NR_TASKS];
            unsigned long *pt;
            int i;

            if (!t || !t->pg_dir || t->pg_dir == kernel_pg_dir)
                continue;
            pt = user_pt(t->pg_dir);
            if (!pt)
                continue;

            for (i = 0; i < 1024; i++) {
                unsigned long pte = pt[i];
                unsigned long pa, va;

                if (!(pte & PTE_PRESENT) || !(pte & PTE_USER))
                    continue;
                pa = pte & PT_MASK;
                va = USER_BASE + (unsigned long)i * PAGE_SIZE;

                if (pass == 0 && !(pte & PTE_COW) && page_is_zero(pa)) {
                    pt[i] = 0;
                    free_page(pa);
                    nr_evicted++;
                    evict_cursor = (evict_cursor + n + 1) % NR_TASKS;
                    if (t == current)
                        write_cr3(current->pg_dir);
                    if (evict_trace++ < 10)
                        printk("evict: zero page 0x%lx from pid=%lu\n",
                               va, t->pid);
                    return 1;
                }

                if (pass == 1 && !(pte & PTE_COW) && is_clean_text(t, va)) {
                    pt[i] = 0;
                    free_page(pa);
                    nr_evicted++;
                    evict_cursor = (evict_cursor + n + 1) % NR_TASKS;
                    if (t == current)
                        write_cr3(current->pg_dir);
                    if (evict_trace++ < 10)
                        printk("evict: text page 0x%lx from pid=%lu\n",
                               va, t->pid);
                    return 1;
                }

                if (pte & PTE_COW && !fallback_pt) {
                    fallback_pt = &pt[i];
                    fallback_pa = pa;
                    fallback_t = t;
                }
            }
        }
    }

    /* Nothing freeable: fall back to unmapping a shared page so that at
       least the *next* write to it takes the COW path with a private
       frame.  If several processes share it, one of them may still be
       able to keep going. */
    if (fallback_pt) {
        *fallback_pt = 0;
        nr_unmapped++;
        if (fallback_t == current)
            write_cr3(current->pg_dir);
        if (evict_trace++ < 10)
            printk("evict: dropped COW mapping (pid=%lu, frame 0x%lx kept)\n",
                   fallback_t->pid, fallback_pa);
        return 1;
    }
    return 0;
}


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

/* --- paging an image page back in (B3) ----------------------------- */

/* Copy the part of an image page that lives in the executable into the
 * freshly allocated frame `pa` (a physical address, reached through the
 * kernel's identity map).
 *
 * `file_read()` writes its output with put_fs_byte(), i.e. through the
 * flat FS segment, so a physical address is a perfectly good destination
 * here — no user mapping is involved and none is needed.
 *
 * Returns 1 if the page was filled from the file, 0 if it is the zero
 * tail of a segment (BSS), -1 if the address is not part of the image.
 * The frame is already zeroed, so "0" needs no work. */
static int page_in_image(unsigned long va, unsigned long pa)
{
    struct task_struct *t = current;
    int i;

    if (!t || !t->exe_inode)
        return -1;

    for (i = 0; i < t->nr_exe_regions; i++) {
        struct exe_region *r = &t->exe_regions[i];
        unsigned long delta, n;
        struct file f;

        if (va < r->va || va >= r->va + r->memsz)
            continue;

        delta = va - r->va;
        if (delta >= r->filesz)
            return 0;                    /* BSS: the zero page is correct */

        n = r->filesz - delta;
        if (n > PAGE_SIZE)
            n = PAGE_SIZE;

        f.f_mode = 0;
        f.f_flags = 0;
        f.f_count = 1;
        f.f_inode = t->exe_inode;
        f.f_pos = r->file_off + delta;

        if (file_read(t->exe_inode, &f, (char *)pa, (int)n) != (int)n)
            return -1;
        nr_page_ins++;
        return 1;
    }
    return -1;
}

/* --- page fault ---------------------------------------------------- */

/* Counters behind the shell's `memstat` command (init/shell.c).  They are
 * what makes demand paging and copy-on-write visible from Ring3: run a
 * program, then look at how many pages it actually caused. */
unsigned long nr_page_faults = 0;      /* every fault that reached us */
unsigned long nr_demand_pages = 0;     /* pages handed out on first touch */
unsigned long nr_cow_breaks = 0;       /* pages copied to break a COW share */
unsigned long nr_oom = 0;              /* faults that found no free page */
unsigned long nr_page_ins = 0;         /* image pages read back from the file */

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
    printk("mem: %lu image pages read back from the executable, "
           "%lu disk interrupts (IRQ14)\n", nr_page_ins, hd_interrupt_count());
    printk("mem: %lu pages evicted, %lu COW mappings dropped\n",
           nr_evicted, nr_unmapped);
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
        unsigned long va = address & PT_MASK;
        unsigned long pa = get_free_page();     /* still unmapped here */

        if (pa) {
            nr_demand_pages++;
            /* Fill the frame BEFORE mapping it.  A page that is mapped
               but not yet filled looks like an all-zero (or clean text)
               page to the eviction scanner, and another task's fault
               under memory pressure could reclaim it while this one is
               still copying into it.  Unmapped, it is invisible.

               Program-image pages come from the executable: this is both
               the first touch (execve no longer pre-loads the image) and
               the re-fault after an eviction. */
            if (va >= USER_PROG_START && va < USER_PROG_END)
                (void)page_in_image(va, pa);

            map_user_page(pgdir, va, pa, PTE_PRESENT | PTE_RW | PTE_USER);
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
