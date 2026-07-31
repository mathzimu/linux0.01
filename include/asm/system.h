#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#define _TSS(n) ((8 + (n) * 2) * 8)

extern struct task_struct *current;

static inline void sti(void)
{
    __asm__ volatile("sti");
}

static inline void cli(void)
{
    __asm__ volatile("cli");
}

static inline unsigned long read_cr0(void)
{
    unsigned long result;
    __asm__ volatile("movl %%cr0, %0" : "=r"(result));
    return result;
}

static inline void write_cr0(unsigned long val)
{
    __asm__ volatile("movl %0, %%cr0" : : "r"(val));
}

static inline unsigned long read_cr3(void)
{
    unsigned long result;
    __asm__ volatile("movl %%cr3, %0" : "=r"(result));
    return result;
}

static inline void write_cr3(unsigned long val)
{
    __asm__ volatile("movl %0, %%cr3" : : "r"(val));
}

#define set_tss_desc(n, addr) \
do { \
    unsigned char *__cp = (unsigned char *)(n); \
    unsigned long __addr = (unsigned long)(addr); \
    __cp[0] = 0x67; \
    __cp[1] = 0x00; \
    __cp[2] = (unsigned char)(__addr); \
    __cp[3] = (unsigned char)(__addr >> 8); \
    __cp[4] = (unsigned char)(__addr >> 16); \
    __cp[5] = 0x89; \
    __cp[6] = 0x00; \
    __cp[7] = (unsigned char)(__addr >> 24); \
} while(0)

#define set_ldt_desc(n, addr) \
do { \
    unsigned char *__cp = (unsigned char *)(n); \
    unsigned long __addr = (unsigned long)(addr); \
    __cp[0] = 0x17; \
    __cp[1] = 0x00; \
    __cp[2] = (unsigned char)(__addr); \
    __cp[3] = (unsigned char)(__addr >> 8); \
    __cp[4] = (unsigned char)(__addr >> 16); \
    __cp[5] = 0x82; \
    __cp[6] = 0x00; \
    __cp[7] = (unsigned char)(__addr >> 24); \
} while(0)

#define switch_to(n) \
do { \
    struct { long a, b; } __tmp; \
    __asm__ volatile( \
        "cmpl %%ecx, current\n\t" \
        "je 1f\n\t" \
        "movw %%dx, %1\n\t" \
        "xchgl %%ecx, current\n\t" \
        "ljmp %0\n\t" \
        "1:" \
        : "=m"(*&__tmp.a), "=m"(*&__tmp.b) \
        : "d"(_TSS(n)), "c"(task[n]) \
        : "memory"); \
} while(0)

#ifndef _DESC_STRUCT_DEFINED
#define _DESC_STRUCT_DEFINED
struct desc_struct {
    unsigned long a, b;
};
#endif

void set_intr_gate(unsigned int n, void *addr);
void set_trap_gate(unsigned int n, void *addr);
void set_system_gate(unsigned int n, void *addr);

#endif
