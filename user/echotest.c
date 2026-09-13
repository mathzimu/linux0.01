/* echotest — isolate the Ring3 stdin path: what does read(0,&c,1) return,
 * and does writing the byte back echo exactly one character?
 *
 * Build: make prog NAME=echotest   (then: exec /bin/echotest, type "abc")
 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char c;
    int n = 0;

    printf("echotest: type three characters\n");
    while (n < 3) {
        int r = read(0, &c, 1);
        printf("echotest: read=%d char=%d\n", r, (int)(unsigned char)c);
        if (r == 1) {
            write(1, "echo=[", 6);
            write(1, &c, 1);
            write(1, "]\n", 2);
            n++;
        }
    }
    printf("echotest: done\n");
    return 0;
}
