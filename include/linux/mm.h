#ifndef _MM_H
#define _MM_H

#include <memlayout.h>
#include <linux/memmap.h>

#define PAGE_SIZE 4096

/* Physical memory is managed relative to the end of the low 1MB, which
 * is where the identity-mapped kernel world begins. */
#define LOW_MEM KERNEL_LOW_MEM
#define MAP_NR(addr) (((addr) - LOW_MEM) / PAGE_SIZE)
#define USED 100

/* Page-table entry bits.  The CPU understands the low three (and the
 * accessed/dirty bits); PTE_COW is a software marker that only this
 * kernel looks at.  Bit 9 is ignored by the 386, which is exactly why
 * Linux 0.01 used it for the same job. */
#define PTE_PRESENT 0x001
#define PTE_RW      0x002
#define PTE_USER    0x004
#define PTE_COW     0x200

extern unsigned long memory_end;
extern unsigned long *mem_map;
extern int max_map_nr;

/* The kernel's own page directory (identity map, no user window); the
 * kernel-only tasks keep this in their TSS. */
extern unsigned long kernel_pg_dir;

void mem_init(unsigned long start_mem, unsigned long end_mem);
unsigned long get_free_page(void);
void free_page(unsigned long addr);

/* --- per-process address spaces (M3) --- */
unsigned long alloc_user_pgdir(void);
void free_user_space(unsigned long pgdir);
unsigned long alloc_user_page(unsigned long pgdir, unsigned long va);
void map_user_page(unsigned long pgdir, unsigned long va, unsigned long pa,
                   unsigned long flags);
int copy_page_tables(unsigned long from_pgdir, unsigned long to_pgdir);
int free_page_tables(unsigned long pgdir, unsigned long from, unsigned long size);
int user_addr_ok(unsigned long addr, unsigned long len);

void do_no_page(unsigned long error_code, unsigned long eip, unsigned long address);

/* Demand-paging / copy-on-write counters and a one-screen summary,
 * exposed to user land through the shell's `memstat` command. */
extern unsigned long nr_page_faults;
extern unsigned long nr_demand_pages;
extern unsigned long nr_cow_breaks;
extern unsigned long nr_oom;
void mm_report(void);

/* Shared by the ELF loader (kernel/sys.c) and the embedded-program entry
 * (init/shell.c): build an address space, copy `len` bytes of image to
 * USER_PROG_START and return the new page directory, or 0 on failure. */
unsigned long load_flat_image(const unsigned char *image, unsigned long len,
                              unsigned long *entry_out);

#endif
