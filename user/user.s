.text
.globl _start

/* 最小用户态程序：验证 Ring3 -> int 0x80 -> 内核 -> Ring3 全链路。
   链接地址 0x200000（内核启动后把嵌入的 user.bin 复制到此运行）。 */

_start:
    /* write(1, msg1, msg1len) */
    movl $4, %eax
    movl $1, %ebx
    leal msg1, %ecx
    movl $msg1len, %edx
    int $0x80

    /* getpid() */
    movl $7, %eax
    int $0x80

    /* 把 pid 转成十进制，倒序写入 buf（buf+8 放换行） */
    leal buf+8, %edi
    movb $10, (%edi)               /* '\n' */
    movl $10, %ebx
1:  xorl %edx, %edx
    divl %ebx
    addb $'0', %dl
    decl %edi
    movb %dl, (%edi)
    testl %eax, %eax
    jnz 1b

    /* write(1, edi, (buf+9) - edi) */
    movl $4, %eax
    movl $1, %ebx
    movl %edi, %ecx
    leal buf+9, %edx
    subl %ecx, %edx
    int $0x80

    /* time(NULL) */
    movl $9, %eax
    xorl %ebx, %ebx
    int $0x80

    /* exit(0) — 结束（init 任务随之进入空闲） */
    movl $1, %eax
    xorl %ebx, %ebx
    int $0x80

.data
msg1:   .ascii "Hello from user mode! pid="
msg1len = . - msg1
.bss
buf:    .space 16
