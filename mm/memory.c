#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <string.h>
#include <asm/system.h>

extern unsigned long _end;

unsigned long memory_end = 0;
unsigned long *mem_map = NULL;

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    int i;

    memory_end = end_mem;
    i = (end_mem - LOW_MEM) / PAGE_SIZE;

    mem_map = (unsigned long *)(end_mem - i * sizeof(unsigned long));

    while (i > 0) {
        i--;
        mem_map[i] = 0;
    }

    mem_map[0] = USED;
    mem_map[1] = USED;
    mem_map[2] = USED;
    mem_map[3] = USED;
}

unsigned long get_free_page(void)
{
    unsigned long addr;
    int i;

    for (i = 0; i < PAGING_PAGES; i++) {
        if (mem_map[i] != 0) continue;
        mem_map[i] = 1;
        addr = LOW_MEM + i * PAGE_SIZE;
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

    i = (addr - LOW_MEM) / PAGE_SIZE;
    if (mem_map[i] <= 0) return;

    mem_map[i]--;
}

int free_page_tables(unsigned long from, unsigned long size)
{
    unsigned long *pg_table;
    unsigned long *pg_dir;
    unsigned long nr;

    if (from & 0x3FFFFF)
        panic("free_page_tables: from must be 4MB aligned");

    pg_dir = (unsigned long *)((from >> 20) & 0xFFC);
    size = (size + 0x3FFFFF) >> 22;

    for (nr = 0; nr < size; nr++) {
        if (*pg_dir & 1) {
            pg_table = (unsigned long *)(0xFFFFF000 & *pg_dir);
            *pg_dir = 0;
        }
        pg_dir++;
    }

    write_cr3(read_cr3());
    return 0;
}

void do_no_page(unsigned long error_code, unsigned long address)
{
    panic("page fault");
}
