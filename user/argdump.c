/* argdump — show what the kernel actually handed to a user program:
 * argc, each argv pointer, and the raw bytes behind it.  Used to
 * diagnose argv corruption in sys_execve.
 *
 * Build: make prog NAME=argdump   (then: exec /bin/argdump a b)
 */

#include "lib.h"

int main(int argc, char *argv[])
{
    int i, k;

    printf("argdump: argc=%d\n", argc);
    for (i = 0; i < argc; i++) {
        printf("argv[%d] ptr=%p bytes:", i, argv[i]);
        for (k = 0; k < 16; k++) {
            unsigned char c = (unsigned char)argv[i][k];
            printf(" %x", c);
            if (!c)
                break;
        }
        printf(" text=[%s]\n", argv[i]);
    }
    printf("argdump: done\n");
    return 0;
}
