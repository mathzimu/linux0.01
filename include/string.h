#ifndef _STRING_H
#define _STRING_H

#ifndef __ASSEMBLER__

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);
char *strcat(char *dest, const char *src);
int strlen(const char *s);
char *strchr(const char *s, int c);
void *memcpy(void *dest, const void *src, int n);
void *memset(void *s, int c, int n);
int memcmp(const void *s1, const void *s2, int n);
void *memmove(void *dest, const void *src, int n);

#endif
#endif
