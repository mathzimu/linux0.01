#ifndef _SIGNAL_H
#define _SIGNAL_H

/* Signal numbers — match 1991 Linux 0.01 (kernel/signal.c). */

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17

/* Dispositions supported by this teaching kernel: default or ignore.
   Custom handlers (function pointers) are not implemented; signal()
   rejects anything other than these two values. */

#define SIG_DFL ((unsigned long)0)
#define SIG_IGN ((unsigned long)1)

#endif
