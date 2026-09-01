#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096
#define LOW_MEM 0x100000
#define PAGING_PAGES 3840
#define MAP_NR(addr) (((addr) - LOW_MEM) / PAGE_SIZE)
#define USED 100

extern unsigned long memory_end;
extern unsigned long *mem_map;
extern int max_map_nr;

void mem_init(unsigned long start_mem, unsigned long end_mem);
unsigned long get_free_page(void);
void free_page(unsigned long addr);
int free_page_tables(unsigned long from, unsigned long size);
void do_no_page(unsigned long error_code, unsigned long eip, unsigned long address);

/* Grant user-mode (Ring3) access to the linear range [from, from+size)
   by setting the U/S bit in the corresponding page-table entries.
   Everything else stays supervisor-only.  Page granular. */
void grant_user_pages(unsigned long from, unsigned long size);

#endif