.text
.globl ltr

/* Segment selectors, mirrored from include/linux/head.h. */
.equ KERNEL_DS, 0x10
.equ USER_DS,   0x23

ltr:
    movl 4(%esp), %eax
    ltr %ax
    ret

/* ====================================================================
 * sys_sigreturn (syscall 67) — resume the context an interrupted
 * syscall was going to return to, after a custom signal handler has
 * run and returned through the user stub at USER_SIGRETURN_ENTRY.
 *
 * The saved context lives in the kernel-side struct sig_context
 * "sigreturn_frame" (filled in by do_signal, kernel/process.c); this
 * routine must not touch the user stack at all.  Entered as an ordinary
 * syscall, so the kernel stack holds a return address at 0(%esp) and
 * the saved registers that ret_from_sys_call (boot/head.s) will pop
 * after this returns:
 *
 *     4(%esp)  ebx   8(%esp) ecx   12(%esp) edx
 *    16(%esp)  esi  20(%esp) edi   24(%esp) ebp
 *    28(%esp)  eax (syscall return value; replaces the interrupted one)
 *    32(%esp)  gs   36(%esp) fs   40(%esp) es   44(%esp) ds
 *    48(%esp)  eip  52(%esp) cs   56(%esp) eflags
 *    60(%esp)  esp  64(%esp) ss
 *
 * So: rewrite the frame in place, then return.  0(%esp) is replaced by
 * the *interrupted* eip, making ret_from_sys_call's own "ret" land
 * there instead of here.
 *
 * Layout of struct sig_context (keep in step with kernel/process.c):
 *     0 magic   4 retaddr   8 handler   12 signo
 *    16 eip   20 cs   24 eflags   28 esp   32 ss
 *    36 gs   40 fs   44 es   48 ds   52 eax
 *    56 ebx  60 ecx  64 edx  68 esi  72 edi  76 ebp
 *
 * This routine REWRITES the frame and returns normally into
 * ret_from_sys_call, which pops those slots and irets.  It must not
 * redirect its own "ret" at the interrupted eip: that would leave the
 * kernel executing user code in ring 0.
 * ==================================================================== */
.globl sys_sigreturn
sys_sigreturn:
    movl $KERNEL_DS, %eax
    mov %ax, %ds
    mov %ax, %es

    movl $sigreturn_frame, %esi

    /* The handler ran with its own signal (plus sa_mask) blocked, so put
       the caller's mask back now that it has returned.  Done before the
       frame is rewritten below, and %esi is reloaded afterwards because
       the C call clobbers the registers it uses. */
    call sigreturn_restore_mask
    movl $sigreturn_frame, %esi

    cmpl $0x51475346, 0(%esi)           /* magic  */
    jne sig_bad
    /* The frame carries the return address it was built with; compare
       against it rather than hard-coding the stub address here. */
    movl 4(%esi), %eax
    cmpl $0x083FF100, %eax              /* USER_SIGRETURN_ENTRY */
    jne sig_bad
    movl 16(%esi), %eax                 /* eip */
    testl %eax, %eax
    jz sig_bad
    movl 20(%esi), %eax                 /* cs */
    cmpl $0x1B, %eax                    /* USER_CS */
    jne sig_bad
    movl 32(%esi), %eax                 /* ss */
    cmpl $0x23, %eax                    /* USER_DS */
    jne sig_bad
    movl 28(%esi), %eax                 /* esp */
    testl $3, %eax                      /* must be word aligned */
    jnz sig_bad
    cmpl $0x08300000, %eax              /* USER_STACK_FLOOR */
    jb sig_bad
    cmpl $0x08400000, %eax              /* USER_WINDOW_TOP */
    jae sig_bad

    /* iret frame (ss/esp/eflags/cs/eip are pre-pushed by the CPU's
       stack layout, written in the order they will be popped). */
    movl 32(%esi), %eax
    movl %eax, 64(%esp)                 /* ss */
    movl 28(%esi), %eax
    movl %eax, 60(%esp)                 /* esp */
    movl 24(%esi), %eax
    movl %eax, 56(%esp)                 /* eflags */
    movl 20(%esi), %eax
    movl %eax, 52(%esp)                 /* cs */
    movl 16(%esi), %eax
    movl %eax, 48(%esp)                 /* eip */

    movl 36(%esi), %eax
    movl %eax, 32(%esp)                 /* gs */
    movl 40(%esi), %eax
    movl %eax, 36(%esp)                 /* fs */
    movl 44(%esi), %eax
    movl %eax, 40(%esp)                 /* es */
    movl 48(%esi), %eax
    movl %eax, 44(%esp)                 /* ds */
    movl 52(%esi), %eax
    movl %eax, 28(%esp)                 /* eax */

    /* The six registers ret_from_sys_call pops next. */
    movl 56(%esi), %eax
    movl %eax, 4(%esp)                  /* ebx */
    movl 60(%esi), %eax
    movl %eax, 8(%esp)                  /* ecx */
    movl 64(%esi), %eax
    movl %eax, 12(%esp)                 /* edx */
    movl 68(%esi), %eax
    movl %eax, 16(%esp)                 /* esi */
    movl 72(%esi), %eax
    movl %eax, 20(%esp)                 /* edi */
    movl 76(%esi), %eax
    movl %eax, 24(%esp)                 /* ebp */

    /* Hand the *restored* eax back as this syscall's return value.  It is
       already in the frame's eax slot, but ret_from_sys_call stores the
       value this routine returns into that very slot on its way out
       (mov %eax, 24(%esp)), so returning 0 here would clobber the
       interrupted syscall's return value — which is what a handler that
       interrupts e.g. sigsuspend() must preserve. */
    movl 52(%esi), %eax
    ret

sig_bad:
    /* A malformed frame means the stub was reached illegitimately.
       There is no safe context to return to: stop the machine rather
       than iret into garbage. */
    cli
1:  hlt
    jmp 1b

/* sigreturn_frame itself is defined in kernel/process.c (struct
 * sig_context, 56 bytes); only its address is needed here. */
