/* permtest — 权限模型回归（M4）
 *
 * 内核一直把 i_mode/i_uid/i_gid 存在 inode 里，却从不检查：系统里只有
 * uid 0，"谁能做什么" 这个问题根本不出现。一旦有程序 setuid() 降权，
 * 文件系统就必须回答它了（见 fs/namei.c 的 permission()）。
 *
 * 本程序用两个降权子进程验证两个方向：
 *   阶段 A（未授权）：读/写他人 0600 文件、在他人 0755 目录里创建/删除、
 *                     chmod/chown 他人文件、进入无 x 位的目录 —— 全部必须被拒；
 *                     同时他人 0644 文件的读必须成功（不能被一刀切）。
 *   阶段 B（root 放宽之后）：0644 可读、0666 可写、0777 目录里可增删、
 *                     自己创建的文件属于自己；而 chmod/chown 他人文件仍然被拒。
 *
 * 用法：make prog NAME=permtest  →  exec /bin/permtest
 */
#include "lib.h"
#include <sys/stat.h>

#define UNPRIV 1000             /* 降权后的 uid/gid */

#define MAY_READ  4
#define MAY_WRITE 2
#define MAY_EXEC  1

static int problems;

/* 期望失败：返回 <0 才算对 */
static void denied(const char *what, int r)
{
    if (r < 0) {
        printf("permtest:   denied  %-32s ok\n", what);
    } else {
        printf("permtest:   denied  %-32s *** FAILED (got %d)\n", what, r);
        problems++;
    }
}

/* 期望成功：返回 >=0 才算对（fd 由调用者负责关闭） */
static int allowed(const char *what, int r)
{
    if (r >= 0) {
        printf("permtest:   allowed %-32s ok\n", what);
        return r;
    }
    printf("permtest:   allowed %-32s *** FAILED (got %d)\n", what, r);
    problems++;
    return r;
}

static void make_file(const char *path, int mode, const char *text)
{
    int fd = creat(path, mode);

    if (fd < 0) {
        printf("permtest: root: creat %s failed\n", path);
        problems++;
        return;
    }
    write(fd, text, (int)strlen(text));
    close(fd);
    chmod(path, mode);          /* creat() honours umask; be explicit */
}

/* 阶段 A：一个 uid=1000 的子进程，什么"越权"的事都要失败 */
static int phase_denied(void)
{
    int fd;
    struct stat tmp;

    printf("permtest: child A: uid=%d euid=%d\n", getuid(), geteuid());

    denied("open(secret, O_RDONLY)", open("/ptest/secret", 0, 0));
    denied("open(secret, O_WRONLY)", open("/ptest/secret", 1, 0));
    denied("open(readable, O_WRONLY)", open("/ptest/readable", 1, 0));

    fd = allowed("open(readable, O_RDONLY)", open("/ptest/readable", 0, 0));
    if (fd >= 0)
        close(fd);

    denied("creat(/ptest/newfile)", creat("/ptest/newfile", 0644));
    denied("mkdir(/ptest/newdir)", mkdir("/ptest/newdir", 0755));
    denied("unlink(/ptest/secret)", unlink("/ptest/secret"));
    denied("unlink(/ptest/readable)", unlink("/ptest/readable"));
    denied("rename(secret -> stolen)", rename("/ptest/secret", "/ptest/stolen"));
    denied("chmod(secret, 0666)", chmod("/ptest/secret", 0666));
    denied("chown(secret, 1000)", chown("/ptest/secret", UNPRIV, UNPRIV));
    denied("access(secret, R_OK)", access("/ptest/secret", MAY_READ));
    allowed("access(readable, R_OK)", access("/ptest/readable", MAY_READ));

    /* 目录的 x 位 = "可以进入/查找"：locked 是 root 的 0700 目录 */
    denied("chdir(/ptest/locked)", chdir("/ptest/locked"));
    denied("stat(/ptest/locked/x)", stat("/ptest/locked/x",
                                         (unsigned long *)&tmp));
    denied("open(/ptest/locked/x)", open("/ptest/locked/x", 0, 0));

    return problems;
}

/* 阶段 B：root 放宽权限之后，同一个 uid=1000 的进程应当能做这些事 */
static int phase_allowed(void)
{
    int fd;
    struct stat st;

    printf("permtest: child B: uid=%d euid=%d\n", getuid(), geteuid());

    fd = allowed("open(secret, O_RDONLY) after chmod 644",
                 open("/ptest/secret", 0, 0));
    if (fd >= 0)
        close(fd);

    denied("open(secret, O_WRONLY) after chmod 644",
           open("/ptest/secret", 1, 0));

    fd = allowed("open(writable, O_WRONLY) mode 666",
                 open("/ptest/writable", 1, 0));
    if (fd >= 0)
        close(fd);

    fd = allowed("creat(/ptest/newfile)", creat("/ptest/newfile", 0644));
    if (fd >= 0) {
        write(fd, "made by uid 1000\n", 17);
        close(fd);
    }
    allowed("unlink(/ptest/newfile)", unlink("/ptest/newfile"));

    allowed("mkdir(/ptest/newdir)", mkdir("/ptest/newdir", 0755));
    allowed("rmdir(/ptest/newdir)", rmdir("/ptest/newdir"));

    fd = creat("/ptest/owned", 0644);
    if (fd >= 0) {
        write(fd, "mine\n", 5);
        close(fd);
        if (stat("/ptest/owned", (unsigned long *)&st) == 0)
            printf("permtest: child B: /ptest/owned uid=%d (want %d)\n",
                   st.st_uid, UNPRIV);
        else
            problems++;
    } else {
        printf("permtest: child B: creat /ptest/owned failed\n");
        problems++;
    }

    denied("chmod(secret, 0666) as non-owner", chmod("/ptest/secret", 0666));
    denied("chown(secret, 1000) as non-root", chown("/ptest/secret", UNPRIV, UNPRIV));
    denied("chdir(/ptest/secret)", chdir("/ptest/secret"));

    return problems;
}

int main(void)
{
    int pid, fd;
    unsigned long status;
    struct stat st;

    printf("permtest: root: uid=%d euid=%d\n", getuid(), geteuid());

    /* 建一棵 root 拥有的实验目录树 */
    mkdir("/ptest", 0755);
    mkdir("/ptest/locked", 0700);
    make_file("/ptest/secret", 0600, "top secret\n");
    make_file("/ptest/readable", 0644, "public\n");
    make_file("/ptest/writable", 0666, "writable\n");

    printf("permtest: --- phase A: unprivileged child (should be denied) ---\n");
    pid = fork();
    if (pid == 0) {
        setuid(UNPRIV);
        exit(phase_denied() & 0xFF);
    }
    if (pid < 0) {
        printf("permtest: fork failed\n");
        return 1;
    }
    waitpid(pid, &status, 0);
    if (status != 0) {
        printf("permtest: phase A reported %d problem(s)\n", (int)status);
        problems += (int)status;
    }

    /* root 放宽：0644 可读、0666 可写、目录可写可进入 */
    chmod("/ptest", 0777);
    chmod("/ptest/locked", 0755);
    chmod("/ptest/secret", 0644);
    printf("permtest: root: chmod /ptest 777, locked 755, secret 644\n");

    printf("permtest: --- phase B: unprivileged child (should be allowed) ---\n");
    pid = fork();
    if (pid == 0) {
        setuid(UNPRIV);
        exit(phase_allowed() & 0xFF);
    }
    waitpid(pid, &status, 0);
    if (status != 0) {
        printf("permtest: phase B reported %d problem(s)\n", (int)status);
        problems += (int)status;
    }

    /* root 覆盖一切：文件已经被 chmod 成 0644，但即使还是 0600 root 也能读 */
    chmod("/ptest/secret", 0600);
    fd = allowed("root open(secret, O_RDONLY) mode 600",
                 open("/ptest/secret", 0, 0));
    if (fd >= 0)
        close(fd);

    if (stat("/ptest/owned", (unsigned long *)&st) == 0)
        printf("permtest: root sees /ptest/owned uid=%d\n", st.st_uid);

    /* 收尾，保持磁盘干净 */
    unlink("/ptest/owned");
    unlink("/ptest/secret");
    unlink("/ptest/readable");
    unlink("/ptest/writable");
    rmdir("/ptest/locked");
    rmdir("/ptest");

    if (problems == 0) {
        printf("permtest: PASS (permissions enforced in both directions)\n");
        return 0;
    }
    printf("permtest: FAIL (%d problem(s))\n", problems);
    return 1;
}
