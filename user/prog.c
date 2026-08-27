/* User program loaded by execve() from the MINIX filesystem.
   Linked at 0x200000; started by crt.s with argc/argv on the stack. */

static int sys_write1(const char *s, int len)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "0"(4), "b"(1), "c"(s), "d"(len));
    return r;
}

static void print_str(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    sys_write1(s, len);
}

static void print_uint(unsigned long v)
{
    char buf[12];
    int i = 11;
    buf[11] = '\0';
    if (v == 0)
        buf[--i] = '0';
    while (v) {
        buf[--i] = '0' + (v % 10);
        v /= 10;
    }
    sys_write1(buf + i, 11 - i);
}

int main(int argc, char *argv[])
{
    int i;

    print_str("exec: argc=");
    print_uint((unsigned long)argc);
    print_str("\n");

    for (i = 0; i < argc; i++) {
        print_str("argv[");
        print_uint((unsigned long)i);
        print_str("]=");
        print_str(argv[i]);
        print_str("\n");
    }

    return 42;
}
