#ifndef _ASM_IO_H
#define _ASM_IO_H

static inline unsigned char inb(unsigned short port)
{
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned char value, unsigned short port)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(unsigned short value, unsigned short port)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned long inl(unsigned short port)
{
    unsigned long result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outl(unsigned long value, unsigned short port)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void insl(int port, void *addr, int count)
{
    __asm__ volatile("cld; rep; insl" : "=D"(addr), "=c"(count)
                     : "d"(port), "0"(addr), "1"(count)
                     : "memory");
}

static inline void outsl(int port, const void *addr, int count)
{
    __asm__ volatile("cld; rep; outsl" : "=S"(addr), "=c"(count)
                     : "d"(port), "0"(addr), "1"(count)
                     : "memory");
}

#endif
