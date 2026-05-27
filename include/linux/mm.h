#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096
#define LOW_MEM 0x100000
#define PAGING_MEMORY (15*1024*1024)
#define PAGING_PAGES (PAGING_MEMORY/PAGE_SIZE)
#define MAP_NR(addr) ((addr - LOW_MEM) / PAGE_SIZE)
#define USED 100

extern unsigned long memory_end;
extern unsigned long *mem_map;

void mem_init(unsigned long start_mem, unsigned long end_mem);
unsigned long get_free_page(void);
void free_page(unsigned long addr);
int free_page_tables(unsigned long from, unsigned long size);

#endif
