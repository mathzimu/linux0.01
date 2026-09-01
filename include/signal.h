#ifndef _SIGNAL_H
#define _SIGNAL_H

/* Signal numbers (subset of POSIX).  SIGCHLD=17 matches Linux, and
   the kernel's sys_exit() uses bit 17 to notify the parent. */

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGCHLD 17

/* Dispositions supported by this teaching kernel: default or ignore.
   Custom handlers (function pointers) are not implemented; signal()
   rejects anything other than these two values. */

#define SIG_DFL ((unsigned long)0)
#define SIG_IGN ((unsigned long)1)

#endif
