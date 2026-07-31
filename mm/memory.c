#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <string.h>
#include <asm/system.h>

extern unsigned long _end;

unsigned long memory_end = 0;
unsigned long *mem_map = NULL;
int max_map_nr = 0;

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
    mem_map[0] = USED;  /* 0x100000: page directory */
    mem_map[1] = USED;  /* 0x101000: page table 0 */

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
}

unsigned long get_free_page(void)
{
    unsigned long addr;
    int i;

    for (i = 0; i < max_map_nr; i++) {
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
    if (addr & (PAGE_SIZE - 1)) return;

    i = MAP_NR(addr);
    if (i >= max_map_nr) return;
    if (mem_map[i] <= 0) return;

    mem_map[i]--;
}

int free_page_tables(unsigned long from, unsigned long size)
{
    unsigned long *pg_table;
    unsigned long *pg_dir;
    unsigned long nr;
    unsigned long dir_index;

    if (from & 0x3FFFFF)
        panic("free_page_tables: from must be 4MB aligned");

    pg_dir = (unsigned long *)(0x100000 + ((from >> 22) << 2));
    size = (size + 0x3FFFFF) >> 22;

    for (nr = 0; nr < size; nr++) {
        dir_index = (from >> 22) + nr;
        if (dir_index >= 1024) break;
        pg_dir = (unsigned long *)(0x100000 + dir_index * 4);
        if (*pg_dir & 1) {
            pg_table = (unsigned long *)(0xFFFFF000 & *pg_dir);
            {
                int j;
                for (j = 0; j < 1024; j++) {
                    if (pg_table[j] & 1)
                        free_page(pg_table[j] & 0xFFFFF000);
                }
            }
            free_page((unsigned long)pg_table & 0xFFFFF000);
            *pg_dir = 0;
        }
    }

    write_cr3(read_cr3());
    return 0;
}

void do_no_page(unsigned long error_code, unsigned long address)
{
    panic("page fault");
}