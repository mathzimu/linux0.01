.text
.globl startup_32, main
.globl divide_error, timer_interrupt, system_call
.globl keyboard_interrupt, hd_interrupt
.globl _gdt, _idt, gdt_descr, idt_descr

.equ PGDIR, 0x100000
.equ PGTBL0, 0x101000
.equ KERNEL_CS, 0x08
.equ KERNEL_DS, 0x10
.equ USER_CS, 0x1B
.equ USER_DS, 0x23

startup_32:
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    lea _end, %esp
    add $0x1000, %esp

    call setup_paging
    call setup_idt

    lgdt gdt_descr
    ljmp $KERNEL_CS, $flush_cs

flush_cs:
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    lea _end, %esp
    add $0x1000, %esp

    xor %eax, %eax
    mov %eax, %cr2

    call main

_idle:
    jmp _idle

setup_paging:
    mov $PGDIR, %eax
    mov %eax, %cr3

    mov $PGDIR, %edi
    xor %eax, %eax
    mov $0x1000, %ecx
    rep stosl

    mov $PGDIR, %edi
    lea (PGTBL0 + 0x07), %eax
    stosl

    mov $PGTBL0, %edi
    lea 0x07, %eax
    mov $0x400, %ecx
1:  stosl
    add $0x1000, %eax
    loop 1b

    mov %cr0, %eax
    or  $0x80000001, %eax
    mov %eax, %cr0
    ret

setup_idt:
    lea _idt, %edi
    mov $256, %ecx
    lea ignore_int, %edx
    mov %edx, %eax
    shr $16, %eax
    shl $16, %eax
    or  $0x8E00, %eax
    mov %edx, %ebx
    and $0xFFFF, %ebx
    or  $0x00080000, %ebx
1:  mov %ebx, (%edi)
    mov %eax, 4(%edi)
    add $8, %edi
    loop 1b

    .macro set_idt_entry vec, handler, type
    lea _idt, %edi
    add $\vec*8, %edi
    lea \handler, %edx
    mov %edx, %eax
    shr $16, %eax
    shl $16, %eax
    or  $\type, %eax
    mov %edx, %ebx
    and $0xFFFF, %ebx
    or  $0x00080000, %ebx
    mov %ebx, (%edi)
    mov %eax, 4(%edi)
    .endm

    set_idt_entry 0, divide_error, 0x8E00
    set_idt_entry 0x20, timer_interrupt, 0x8E00
    set_idt_entry 0x21, keyboard_interrupt, 0x8E00
    set_idt_entry 0x2E, hd_interrupt, 0x8E00
    set_idt_entry 0x0E, page_fault, 0x8E00
    set_idt_entry 0x80, system_call, 0xEF00

    lidt idt_descr
    ret

ignore_int:
    iret

divide_error:
    push $divide_msg
    call panic
    iret

timer_interrupt:
    push %ds
    push %es
    push %fs
    push %gs
    pushal
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov $0x20, %al
    outb %al, $0x20
    call do_timer
    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

system_call:
    movl %esp, syscall_esp
    push %ds
    push %es
    push %fs
    push %gs
    /* save syscall number in a scratch slot below the arg regs */
    push %eax
    /* detect the caller's privilege: the saved cs (at syscall_esp+4,
       i.e. 24(%esp) after the 5 pushes above) carries the RPL; note
       this must run AFTER push %eax so eax keeps the syscall number */
    movl 24(%esp), %eax
    andl $3, %eax
    movl %eax, syscall_cpl

    /* push args in reverse so the lowest-address arg is %ebx (arg0) */
    push %ebp                /* [arg5] */
    push %edi                /* [arg4] */
    push %esi                /* [arg3] */
    push %edx                /* [arg2] */
    push %ecx                /* [arg1] */
    push %ebx                /* [arg0] */

    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov $USER_DS, %ax
    mov %ax, %fs
    mov $KERNEL_DS, %ax
    mov %ax, %gs

    /* reload saved syscall number for dispatch */
    mov 24(%esp), %eax

    /* 24 syscalls: numbers 0..23 valid */
    cmpl $24, %eax
    jb 1f
    movl $-1, %eax
    jmp 2f
1:  call *sys_call_table(,%eax,4)
.globl ret_from_sys_call
ret_from_sys_call:
2:
    /* store return value where saved eax lives */
    mov %eax, 24(%esp)

    /* deliver pending signals before returning to the caller.
       current->signal lives at offset 12 of struct task_struct. */
    movl current, %ecx
    cmpl $0, 12(%ecx)
    je 3f
    pushl %eax                  /* preserve syscall return value */
    call do_signal
    popl %eax
3:
    /* unwind: restore ebx..ebp (6), pop saved_eax into a slot, fixup */
    pop %ebx
    pop %ecx
    pop %edx
    pop %esi
    pop %edi
    pop %ebp
    pop %eax                 /* restore return value into eax */
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

keyboard_interrupt:
    push %ds
    push %es
    push %fs
    push %gs
    pushal
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
1:  /* drain the 8042 output buffer: the i8042 can queue several
       scancodes; reading only one per IRQ loses keys typed fast */
    xor %al, %al
    inb $0x64, %al          /* status register */
    test $0x01, %al         /* output buffer full? */
    jz 2f
    inb $0x60, %al          /* read scancode */
    push %eax
    call kbd_interrupt_handler
    pop %eax
    jmp 1b
2:  mov $0x20, %al
    outb %al, $0x20
    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

hd_interrupt:
    push %ds
    push %es
    push %fs
    push %gs
    pushal
    mov $KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov $0x20, %al
    outb %al, $0x20
    outb %al, $0xA0
    call hd_interrupt_handler
    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

.data
.globl _gdt, _idt, gdt_descr, idt_descr
.globl syscall_esp, syscall_cpl

syscall_esp:
    .long 0

syscall_cpl:
    .long 0

divide_msg:
    .string "Divide error"

_gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00CFFA000000FFFF
    .quad 0x00CFF2000000FFFF
    .fill 132, 8, 0

gdt_descr:
    .word (137*8)-1
    .long _gdt

_idt:
    .fill 256, 8, 0

idt_descr:
    .word (256*8)-1
    .long _idt

sys_call_table:
    .long sys_setup
    .long sys_exit
    .long sys_fork
    .long sys_read
    .long sys_write
    .long sys_open
    .long sys_close
    .long sys_getpid
    .long sys_pause
    .long sys_time
    .long sys_kill
    .long sys_sync
    .long sys_lseek
    .long sys_dup
    .long sys_dup2
    .long sys_getppid
    .long sys_mknod
    .long sys_mkdir
    .long sys_unlink
    .long sys_rmdir
    .long sys_waitpid
    .long sys_execve
    .long sys_signal
    .long sys_chdir
