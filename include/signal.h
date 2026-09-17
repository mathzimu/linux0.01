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
#define SIGCONT  18
#define SIGSTOP  19

/* Signal dispositions.  SIG_DFL/SIG_IGN are the built-in values (all
   upper addresses are rejected, so kernel text can never be handed to a
   handler slot); anything else is a Ring3 handler function pointer,
   which sys_signal() validates against the user program image. */
#define SIG_DFL ((unsigned long)0)
#define SIG_IGN ((unsigned long)1)

/* Signal masks (B5).  A blocked signal is *not* discarded: it stays
   pending and is delivered as soon as it is unblocked, which is what
   makes the mask usable for critical sections.  SIGKILL cannot be
   blocked — this kernel has no job control (no SIGSTOP/SIGCONT), so
   SIGKILL is the only signal a process may not take away. */
#define SIG_BLOCK   1        /* mask |= set   */
#define SIG_UNBLOCK 2        /* mask &= ~set  */
#define SIG_SETMASK 3        /* mask  = set   */
#define SIG_UNBLOCKABLE (1UL << SIGKILL)

/* A handler is reset to SIG_DFL before it runs (classic signal()
   semantics), except for SIGCHLD.  The interrupted context is restored
   by the sigreturn syscall (67), whose stub lives at
   USER_SIGRETURN_ENTRY — see include/memlayout.h. */
#define SIGFRAME_MAGIC 0x51475346UL   /* "FSGQ" */

/* sigaction() (B5 step 3): install a handler that *stays* installed, and
   say which signals should additionally be blocked while it runs.  The
   signal being handled is always blocked for the duration of its own
   handler (POSIX requires it), so a handler that is sent its own signal
   again does not recurse into itself.
 *
 * sa_mask covers signals 1..17 (the ones this kernel defines): the kernel
 * keeps a 16-bit word per (task, signal), so bits for signals that do not
 * exist here cost nothing.
 * SA_RESTART is accepted for source compatibility, but interrupted
 * syscalls are not restarted in this kernel (see docs/limitations.md). */
struct sigaction {
    unsigned long sa_handler;    /* SIG_DFL, SIG_IGN or a Ring3 address */
    unsigned long sa_mask;       /* extra signals to block (1..17) */
    unsigned long sa_flags;      /* SA_RESTART */
};

#define SA_RESTART 1

#endif
