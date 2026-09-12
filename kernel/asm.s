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
 * ==================================================================== */
.globl sys_sigreturn
sys_sigreturn:
    movl $KERNEL_DS, %eax
    mov %ax, %ds
    mov %ax, %es

    movl $sigreturn_frame, %esi

    cmpl $0x51475346, 0(%esi)           /* magic  */
    jne sig_bad
    /* The frame carries the return address it was built with; compare
       against it rather than hard-coding the stub address here. */
    movl 4(%esi), %eax
    cmpl $0x003FF010, %eax              /* USER_SIGRETURN_ENTRY */
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
    cmpl $0x00300000, %eax              /* CHILD_USER_STACK_END */
    jb sig_bad
    cmpl $0x00400000, %eax              /* IDENTITY_MAP_TOP */
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

    movl 16(%esi), %eax                 /* eip -> ret target */
    movl %eax, 0(%esp)

    movl $0, %eax                       /* syscall "return value" (unused) */
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
