#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/segment.h>

struct m_inode *namei(const char *pathname)
{
    struct m_inode *inode;

    if (!pathname) return NULL;

    if (*pathname == '/') pathname++;

    if (*pathname == '\0') {
        inode = iget(0x301, 1);
        return inode;
    }

    inode = iget(0x301, 1);
    if (!inode) return NULL;

    return inode;
}
