/* 用户态 printf：freestanding 实现，输出到 fd 1（write 系统调用）。 */

#include "lib.h"

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
