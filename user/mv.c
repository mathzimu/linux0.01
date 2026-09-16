/* mv：重命名/移动。
 *
 * 这个内核没有 rename 系统调用，而"同一文件系统内的重命名"在 POSIX 下的
 * 等价做法正是 link(old, new) + unlink(old)——链接数短暂变成 2，随后旧名字
 * 消失，数据一块没动。目录不能这样搬（link(2) 会拒绝目录），所以那种情况是
 * 明确报错，而不是假装成功。 */

#include "lib.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("usage: mv <old> <new>\n");
        return 1;
    }

    if (link(argv[1], argv[2]) < 0) {
        printf("mv: %s -> %s: failed (missing, a directory, or %s exists)\n",
               argv[1], argv[2], argv[2]);
        return 1;
    }

    if (unlink(argv[1]) < 0) {
        printf("mv: %s: linked as %s, but the old name could not be removed\n",
               argv[1], argv[2]);
        return 1;
    }
    return 0;
}
