#include <linux/kernel.h>
#include <linux/tty.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

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

static int vsprintf(char *buf, const char *fmt, va_list args)
{
    char *p, *str;
    int num;
    unsigned long unum;
    char *end = buf + 1023;  // Leave space for null terminator

    for (str = buf; *fmt && str < end; fmt++) {
        if (*fmt != '%') {
            *str++ = *fmt;
            continue;
        }
        fmt++;

        switch (*fmt) {
        case 'd':
        case 'i':
            num = va_arg(args, int);
            if (num < 0) {
                if (str < end) *str++ = '-';
                unum = (unsigned long)(-(long)num);
                if (str < end)
                    str += num_to_str(str, unum, 10);
            } else {
                if (str < end)
                    str += num_to_str(str, (unsigned long)num, 10);
            }
            break;
        case 'u':
            unum = va_arg(args, unsigned int);
            if (str < end)
                str += num_to_str(str, unum, 10);
            break;
        case 'x':
        case 'X':
            unum = va_arg(args, unsigned int);
            if (str < end)
                str += num_to_str(str, unum, 16);
            break;
        case 's':
            p = va_arg(args, char *);
            while (*p && str < end)
                *str++ = *p++;
            break;
        case 'c':
            if (str < end)
                *str++ = (char)va_arg(args, int);
            break;
        default:
            if (str < end) *str++ = '%';
            if (str < end) *str++ = *fmt;
            break;
        }
    }
    *str = '\0';
    return str - buf;
}

int printk(const char *fmt, ...)
{
    char buf[1024];
    int i;
    va_list args;

    va_start(args, fmt);
    i = vsprintf(buf, fmt, args);
    va_end(args);

    tty_write(&tty_table[0], buf, i);
    return i;
}
