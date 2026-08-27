#ifndef _USER_LIB_H
#define _USER_LIB_H

/* 用户态编程库：execve 加载的程序 #include "lib.h" 后写 main() 即可。
   printf 输出到 stdout（fd 1）；系统调用包装来自 include/unistd.h。 */

#include <unistd.h>

int printf(const char *fmt, ...);

#endif

/* user-mode heap allocator (first-fit free list + bump) */
void *malloc(unsigned long size);
void free(void *p);
