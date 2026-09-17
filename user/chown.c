/* chown <uid>:<gid> <file>...：改属主/属组（系统调用 16）。
 *
 * 演示版只收 "uid:gid" 这种显式写法——没有 stat 就不用问"当前 gid 是多少"
 * （虽然这个内核其实有 stat(2)）。uid/gid 是十进制数字。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    char spec[32];
    char *colon;
    int uid, gid, i, rc = 0;

    if (argc < 3) {
        printf("usage: chown <uid>:<gid> <file>...\n");
        return 1;
    }
    strcpy(spec, argv[1]);
    colon = strchr(spec, ':');
    if (!colon) {
        printf("chown: want uid:gid\n");
        return 1;
    }
    *colon = '\0';
    uid = atoi(spec);
    gid = atoi(colon + 1);

    for (i = 2; i < argc; i++) {
        if (chown(argv[i], uid, gid) < 0) {
            printf("chown: %s: failed\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
