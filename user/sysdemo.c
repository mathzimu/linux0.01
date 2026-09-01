/* Linux 0.01 对齐 syscall 演示：stat/fstat、uid/gid、umask、uname、
   times、setpgid/getpgrp、creat、access、chmod/chown。 */

#include "lib.h"
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/times.h>

int main(void)
{
    struct stat st;
    struct utsname uts;
    struct tms tm;
    int fd;

    if (stat("/hello.txt", (unsigned long *)&st) == 0)
        printf("stat /hello.txt: ino=%d size=%d mode=0%o nlink=%d\n",
               st.st_ino, st.st_size, st.st_mode, st.st_nlink);
    fd = open("/hello.txt", 0, 0);
    if (fd >= 0) {
        if (fstat(fd, (unsigned long *)&st) == 0)
            printf("fstat fd=%d: size=%d\n", fd, st.st_size);
        close(fd);
    }

    printf("uid=%d euid=%d gid=%d egid=%d\n",
           getuid(), geteuid(), getgid(), getegid());
    if (setuid(7) == 0)
        printf("setuid(7) ok -> uid=%d\n", getuid());
    if (setuid(0) == 0)
        printf("setuid(0) ok -> uid=%d\n", getuid());

    printf("umask(022) old=0%o\n", umask(022));

    if (uname((unsigned long *)&uts) == 0)
        printf("uname: %s %s %s %s %s\n", uts.sysname, uts.nodename,
               uts.release, uts.version, uts.machine);

    if (times((unsigned long *)&tm) >= 0)
        printf("times: utime=%d stime=%d\n", tm.tms_utime, tm.tms_stime);

    printf("pgrp=%d\n", getpgrp());
    setpgid(0, 0);
    printf("pgrp after setpgid(0,0) = %d\n", getpgrp());

    if (creat("/sysdemo.tmp", 0644) >= 0) {
        printf("creat /sysdemo.tmp ok\n");
        if (unlink("/sysdemo.tmp") == 0)
            printf("unlink ok\n");
    }

    printf("access /readme.txt = %d\n", access("/readme.txt", 4));

    /* fcntl F_DUPFD: duplicate fd onto the first free slot >= 3 */
    fd = open("/hello.txt", 0, 0);
    if (fd >= 0) {
        int d = fcntl(fd, 0, 5);      /* F_DUPFD, start at 5 */
        printf("fcntl F_DUPFD: fd=%d dup=%d\n", fd, d);
        if (d >= 0) close(d);
        close(fd);
    }

    chmod("/hello.txt", 0600);
    chown("/hello.txt", 1, 2);
    printf("chmod/chown ok\n");
    return 0;
}
