#ifndef _KERNEL_H
#define _KERNEL_H

#define NULL ((void *)0)

extern int _errno;

void panic(const char *msg);
int printk(const char *fmt, ...);

#endif
