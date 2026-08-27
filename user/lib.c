/* 用户态 printf + malloc/free：freestanding 实现，输出到 fd 1。 */

#include "lib.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

static void outn(const char *s, int n)
{
    write(1, s, n);
}

static int num_to_str(char *str, unsigned long num, int base)
{
    char digits[] = "0123456789abcdef";
    char buf[32];
    int i = 0, j = 0;

    if (num == 0) {
        *str = '0';
        return 1;
    }
    while (num) {
        buf[i++] = digits[num % base];
        num /= base;
    }
    while (i > 0)
        str[j++] = buf[--i];
    return j;
}

static void write_num(unsigned long num, int base, int width)
{
    char tmp[32];
    int len = num_to_str(tmp, num, base);
    int pad = width > len ? width - len : 0;
    int i;

    while (pad--)
        outn(" ", 1);
    for (i = 0; i < len; i++)
        outn(&tmp[i], 1);
}

int printf(const char *fmt, ...)
{
    va_list args;
    int n = 0;

    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            outn(fmt, 1);
            fmt++;
            n++;
            continue;
        }
        fmt++;
        {
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
            if (*fmt == 'l')
                fmt++;

            switch (*fmt) {
            case 'd':
            case 'i': {
                int v = va_arg(args, int);
                if (v < 0) {
                    outn("-", 1);
                    n++;
                    write_num((unsigned long)(-(long)v), 10, width);
                } else {
                    write_num((unsigned long)v, 10, width);
                }
                break;
            }
            case 'u':
                write_num((unsigned long)va_arg(args, unsigned int), 10, width);
                break;
            case 'x':
            case 'X':
                write_num((unsigned long)va_arg(args, unsigned int), 16, width);
                break;
            case 'o':
                write_num((unsigned long)va_arg(args, unsigned int), 8, width);
                break;
            case 'p':
                outn("0x", 2);
                write_num((unsigned long)va_arg(args, void *), 16, 0);
                break;
            case 's': {
                char *s = va_arg(args, char *);
                int len = 0;
                while (s[len])
                    len++;
                outn(s, len);
                n += len;
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                outn(&c, 1);
                n++;
                break;
            }
            default:
                outn("%", 1);
                n++;
                break;
            }
            fmt++;
        }
    }
    va_end(args);
    return n;
}

/* --- user-mode heap ---------------------------------------------------
   Region: [0x310000, 0x3FE000) — above the kernel buffer cache and
   below the user stack (top 0x3FF000).  Free chunks form a singly
   linked list; allocation is first-fit, then bump from the top. */

#define HEAP_START 0x310000UL
#define HEAP_END   0x3FE000UL

typedef struct chunk {
    unsigned long size;          /* usable data size */
    struct chunk *next;          /* free-list link */
} chunk_t;                       /* 8-byte header */

static chunk_t *free_list = NULL;
static unsigned long heap_top = HEAP_START;

void *malloc(unsigned long size)
{
    chunk_t *c, *prev = NULL;

    size = (size + 7) & ~7UL;
    if (size == 0)
        size = 8;

    /* first-fit over the free list */
    for (c = free_list; c; prev = c, c = c->next) {
        if (c->size >= size) {
            if (prev)
                prev->next = c->next;
            else
                free_list = c->next;
            return (void *)(c + 1);
        }
    }

    /* no reusable chunk: bump */
    if (heap_top + sizeof(chunk_t) + size > HEAP_END)
        return NULL;
    c = (chunk_t *)heap_top;
    heap_top += sizeof(chunk_t) + size;
    c->size = size;
    return (void *)(c + 1);
}

void free(void *p)
{
    chunk_t *c;

    if (!p)
        return;
    c = (chunk_t *)p - 1;
    c->next = free_list;
    free_list = c;
}
