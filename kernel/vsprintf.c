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

/* Write a number into *strp, honouring a minimum field width.  `zero`
   pads with '0' instead of spaces (the 0 flag) and `left` pads on the
   right instead (the - flag).  A negative number's sign is printed by the
   caller before the digits, so "%05d" of -42 comes out as -0042. */
static void write_num(char **strp, char *end, unsigned long num,
                      int base, int width, int zero, int left)
{
    char tmp[32];
    char *str = *strp;
    int len = num_to_str(tmp, num, base);   /* num_to_str does not NUL-terminate */
    int pad = width > len ? width - len : 0;
    int i;

    if (left) {
        for (i = 0; i < len && str < end; i++)
            *str++ = tmp[i];
        while (pad-- && str < end)
            *str++ = ' ';
    } else {
        while (pad-- && str < end)
            *str++ = zero ? '0' : ' ';
        for (i = 0; i < len && str < end; i++)
            *str++ = tmp[i];
    }
    *strp = str;
}

static int vsprintf(char *buf, const char *fmt, va_list args)
{
    char *str, *end;

    str = buf;
    end = buf + 1023;              /* leave space for null terminator */

    while (*fmt && str < end) {
        int left = 0, zero = 0, width = 0, prec = -1;

        if (*fmt != '%') {
            *str++ = *fmt++;
            continue;
        }
        fmt++;

        /* Flags, width and precision are parsed and consumed BEFORE the
           conversion character, and that ordering is the point: an
           unsupported specifier such as "%.15s" used to fall through to
           the default branch without consuming its argument, so every
           later %d in the same printk read the wrong slot - a diagnostic
           that lies is worse than no diagnostic.  '+' '#' ' ' are
           accepted and ignored (the sign/prefix forms are unused here). */
        for (;;) {
            if (*fmt == '-') {
                left = 1;
                fmt++;
            } else if (*fmt == '0') {
                zero = 1;
                fmt++;
            } else if (*fmt == '+' || *fmt == '#' || *fmt == ' ') {
                fmt++;
            } else {
                break;
            }
        }

        /* minimum field width: %4d */
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* precision: %.3s truncates a string, %.3d pads a number */
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }

        /* length modifier: l (long; on i386 long == int width) */
        if (*fmt == 'l')
            fmt++;

        /* A precision on a number means "at least this many digits". */
        if (prec > 0 && *fmt != 's' && *fmt != 'c') {
            zero = 1;
            if (width < prec)
                width = prec;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            long val = va_arg(args, int);

            if (val < 0) {
                if (str < end)
                    *str++ = '-';
                write_num(&str, end, (unsigned long)(-val), 10, width,
                          zero, left);
            } else {
                write_num(&str, end, (unsigned long)val, 10, width,
                          zero, left);
            }
            break;
        }
        case 'u':
            write_num(&str, end,
                      (unsigned long)va_arg(args, unsigned int),
                      10, width, zero, left);
            break;
        case 'x':
        case 'X':
            write_num(&str, end,
                      (unsigned long)va_arg(args, unsigned int),
                      16, width, zero, left);
            break;
        case 'o':
            write_num(&str, end,
                      (unsigned long)va_arg(args, unsigned int),
                      8, width, zero, left);
            break;
        case 'p':
            if (str < end)
                *str++ = '0';
            if (str < end)
                *str++ = 'x';
            write_num(&str, end,
                      (unsigned long)va_arg(args, void *), 16, 0, 0, 0);
            break;
        case 's': {
            char *p = va_arg(args, char *);
            int n = 0, i, pad;

            while (p[n] && (prec < 0 || n < prec))
                n++;
            if (!left) {
                pad = width > n ? width - n : 0;
                while (pad-- && str < end)
                    *str++ = ' ';
            }
            for (i = 0; i < n && str < end; i++)
                *str++ = p[i];
            if (left) {
                pad = width > n ? width - n : 0;
                while (pad-- && str < end)
                    *str++ = ' ';
            }
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

/* Buffer formatter for kernel-internal text generation (e.g. /proc/ps):
   printk() cannot be reused because it always writes to the console.
   Same format subset as printk (see kernel/vsprintf.c). */
int sprintf(char *buf, const char *fmt, ...)
{
    int i;
    va_list args;

    va_start(args, fmt);
    i = vsprintf(buf, fmt, args);
    va_end(args);
    return i;
}
