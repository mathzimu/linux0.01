.text
.globl _start

/* 最小用户态程序：Ring3 -> int 0x80 -> 内核 -> Ring3 全链路 + fork 演示。
   链接地址 0x200000（内核启动后把嵌入的 user.bin 复制到此运行）。 */

_start:
    /* write(1, msg, msglen) */
    movl $4, %eax
    movl $1, %ebx
    leal msg, %ecx
    movl $msglen, %edx
    int $0x80

    /* fork() */
    movl $2, %eax
    int $0x80
    testl %eax, %eax
    jnz parent_path

    /* ---- child ---- */
    movl $4, %eax
    movl $1, %ebx
    leal cmsg, %ecx
    movl $cmsglen, %edx
    int $0x80
    movl $7, %eax                 /* getpid() */
    int $0x80
    call print_uint               /* eax -> decimal -> write */
    call print_nl
    movl $1, %eax                 /* exit(0) */
    xorl %ebx, %ebx
    int $0x80

parent_path:
    /* ---- parent: eax = fork() return value; keep it in esi ---- */
    movl %eax, %esi
    movl $4, %eax
    movl $1, %ebx
    leal pmsg, %ecx
    movl $pmsglen, %edx
    int $0x80
    movl %esi, %eax
    call print_uint
    call print_nl
    movl $1, %eax                 /* exit(0) */
    xorl %ebx, %ebx
    int $0x80

/* eax -> decimal string -> write(1, buf, len) */
print_uint:
    leal buf+8, %edi
    movb $0, (%edi)
    movl $10, %ebx
1:  xorl %edx, %edx
    divl %ebx
    addb $'0', %dl
    decl %edi
    movb %dl, (%edi)
    testl %eax, %eax
    jnz 1b
    movl $4, %eax
    movl $1, %ebx
    movl %edi, %ecx
    leal buf+8, %edx
    subl %ecx, %edx
    int $0x80
    ret

print_nl:
    movl $4, %eax
    movl $1, %ebx
    leal nl, %ecx
    movl $1, %edx
    int $0x80
    ret

.data
msg:    .ascii "Hello from user mode!\n"
msglen = . - msg
cmsg:   .ascii "child: getpid()="
cmsglen = . - cmsg
pmsg:   .ascii "parent: fork ok, child pid="
pmsglen = . - pmsg
nl:     .byte 10
.bss
buf:    .space 16
