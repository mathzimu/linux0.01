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

/* Write a right-aligned number into *strp, honouring a minimum field
   width (padded with spaces on the left). */
static void write_num(char **strp, char *end, unsigned long num,
                      int base, int width)
{
    char tmp[32];
    char *str = *strp;
    int len = num_to_str(tmp, num, base);   /* num_to_str does not NUL-terminate */
    int pad = width > len ? width - len : 0;
    int i;

    while (pad-- && str < end)
        *str++ = ' ';
    for (i = 0; i < len && str < end; i++)
        *str++ = tmp[i];
    *strp = str;
}

static int vsprintf(char *buf, const char *fmt, va_list args)
{
    char *str, *end;

    str = buf;
    end = buf + 1023;              /* leave space for null terminator */

    while (*fmt && str < end) {
        if (*fmt != '%') {
            *str++ = *fmt++;
            continue;
        }
        fmt++;

        /* minimum field width: %4d */
        {
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');

            /* length modifier: l (long; on i386 long == int width) */
            if (*fmt == 'l')
                fmt++;

            switch (*fmt) {
            case 'd':
            case 'i': {
                long val = va_arg(args, int);
                if (val < 0) {
                    if (str < end)
                        *str++ = '-';
                    write_num(&str, end, (unsigned long)(-val), 10, width);
                } else {
                    write_num(&str, end, (unsigned long)val, 10, width);
                }
                break;
            }
            case 'u':
                write_num(&str, end,
                          (unsigned long)va_arg(args, unsigned int),
                          10, width);
                break;
            case 'x':
            case 'X':
                write_num(&str, end,
                          (unsigned long)va_arg(args, unsigned int),
                          16, width);
                break;
            case 'o':
                write_num(&str, end,
                          (unsigned long)va_arg(args, unsigned int),
                          8, width);
                break;
            case 'p':
                if (str < end)
                    *str++ = '0';
                if (str < end)
                    *str++ = 'x';
                write_num(&str, end,
                          (unsigned long)va_arg(args, void *), 16, 0);
                break;
            case 's': {
                char *p = va_arg(args, char *);
                while (*p && str < end)
                    *str++ = *p++;
                break;
            }
            case 'c':
                if (str < end)
                    *str++ = (char)va_arg(args, int);
                break;
            default:
                if (str < end)
                    *str++ = '%';
                if (*fmt && str < end)
                    *str++ = *fmt;
                break;
            }
        }
        fmt++;
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
