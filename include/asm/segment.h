#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

static inline unsigned char get_fs_byte(const char *addr)
{
    unsigned char result;
    __asm__ volatile("movb %%fs:%1, %0" : "=r"(result) : "m"(*addr));
    return result;
}

static inline void put_fs_byte(unsigned char val, char *addr)
{
    __asm__ volatile("movb %0, %%fs:%1" : : "r"(val), "m"(*addr));
}

static inline unsigned long get_fs_long(const unsigned long *addr)
{
    unsigned long result;
    __asm__ volatile("movl %%fs:%1, %0" : "=r"(result) : "m"(*addr));
    return result;
}

static inline void put_fs_long(unsigned long val, unsigned long *addr)
{
    __asm__ volatile("movl %0, %%fs:%1" : : "r"(val), "m"(*addr));
}

#endif
