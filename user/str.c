/* libc 演示：字符串函数 + ctype + atoi/strtol。 */

#include "lib.h"

int main(void)
{
    char buf[64];
    char *end;
    long v;

    printf("strlen(\"kernel\")=%d\n", strlen("kernel"));
    printf("strcmp(abc,abc)=%d  strcmp(abc,abd)=%d\n",
           strcmp("abc", "abc"), strcmp("abc", "abd"));
    printf("strncmp(abc,abd,2)=%d\n", strncmp("abc", "abd", 2));

    strcpy(buf, "hello");
    strcat(buf, ", libc!");
    printf("strcpy+strcat -> \"%s\"\n", buf);

    strncpy(buf, "minix", 3);
    buf[3] = '\0';
    printf("strncpy(3) -> \"%s\"\n", buf);

    printf("strchr(\"hello\",'l') -> \"%s\"\n", strchr("hello", 'l'));
    printf("strrchr(\"hello\",'l') -> \"%s\"\n", strrchr("hello", 'l'));

    memcpy(buf, "memcpy", 6);
    printf("memcpy -> \"%s\"\n", buf);
    memset(buf, 'x', 3);
    buf[3] = '\0';
    printf("memset(3) -> \"%s\"\n", buf);
    printf("memcmp(abc,abd,3)=%d\n", memcmp("abc", "abd", 3));

    printf("isupper('A')=%d isdigit('7')=%d tolower('A')=%c toupper('z')=%c\n",
           isupper('A'), isdigit('7'), tolower('A'), toupper('z'));

    printf("atoi(\"42\")=%d  atoi(\"-7\")=%d\n", atoi("42"), atoi("-7"));
    v = strtol("0x1F", &end, 0);
    printf("strtol(\"0x1F\",base0)=%ld\n", v);
    v = strtol("101", NULL, 2);
    printf("strtol(\"101\",base2)=%ld\n", v);
    v = strtol("  123abc", &end, 10);
    printf("strtol(\"  123abc\")=%ld stop at \"%s\"\n", v, end);
    return 0;
}
