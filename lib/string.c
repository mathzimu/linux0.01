#include <string.h>

#define NULL ((void *)0)

char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;
    while ((*dest++ = *src++) != '\0');
    return tmp;
}

char *strncpy(char *dest, const char *src, int n)
{
    int i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, int n)
{
    while (n-- && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    if (n < 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcat(char *dest, const char *src)
{
    char *tmp = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++) != '\0');
    return tmp;
}

int strlen(const char *s)
{
    int len = 0;
    while (*s++) len++;
    return len;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return NULL;
}

char *strrchr(const char *s, int c)
{
    const char *found = NULL;
    while (*s) {
        if (*s == (char)c) found = s;
        s++;
    }
    return (char *)found;
}

void *memcpy(void *dest, const void *src, int n)
{
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *s, int c, int n)
{
    char *p = s;
    while (n--) *p++ = (char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, int n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memmove(void *dest, const void *src, int n)
{
    char *d = dest;
    const char *s = src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
