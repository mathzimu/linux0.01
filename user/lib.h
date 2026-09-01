#ifndef _USER_LIB_H
#define _USER_LIB_H

/* 用户态编程库：execve 加载的程序 #include "lib.h" 后写 main() 即可。
   printf 输出到 stdout（fd 1）；系统调用包装来自 include/unistd.h。 */

#include <unistd.h>
#include <signal.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

int printf(const char *fmt, ...);

/* directory reading: MINIX v1 dir entry = u16 ino + name[14] */
#define NAME_MAX 14
struct dirent {
    unsigned short d_ino;
    char d_name[NAME_MAX + 1];
};
typedef struct {
    int fd;                  /* open()ed directory */
    struct dirent ent;       /* current entry (stable until next readdir) */
    char buf[16];            /* raw dir entry buffer */
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

/* user-mode heap allocator (first-fit free list + bump) */
void *malloc(unsigned long size);
void free(void *p);

/* string functions (user-lib copies; same signatures as <string.h>) */
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, int n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);
char *strcat(char *dest, const char *src);
int strlen(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memcpy(void *dest, const void *src, int n);
void *memset(void *s, int c, int n);
int memcmp(const void *s1, const void *s2, int n);
void *memmove(void *dest, const void *src, int n);

/* ctype */
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int tolower(int c);
int toupper(int c);

/* stdlib: number parsing */
int atoi(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);

#endif
