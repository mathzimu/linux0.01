#include <linux/sched.h>

int close(int fd)
{
    return sys_close(fd);
}
