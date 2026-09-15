#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <string.h>
#include <asm/segment.h>

/* Set to 1 to trace every failed directory lookup.  A miss on the *last*
   component is the normal "file does not exist" case for O_CREAT, so this
   is off by default: otherwise every file creation writes a line into the
   regression logs and buries the failures that matter.  Turn it on when a
   lookup that should succeed comes back NULL. */
#define NAMEI_TRACE 0

static int next_entry(struct m_inode *dir, int i,
                      struct buffer_head **bh_out, struct minix_dir_entry **de_out)
{
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    int block, offset;
    unsigned short blk;

    block = i * sizeof(struct minix_dir_entry) / BLOCK_SIZE;
    offset = (i * sizeof(struct minix_dir_entry)) % BLOCK_SIZE;

    if (block < 7) {
        blk = dir->i_zone[block];
        if (!blk) return -1;
        bh = bread(dir->i_dev, blk);
    } else {
        struct buffer_head *ibh;
        unsigned short indblk = dir->i_zone[7];
        if (!indblk) return -1;
        ibh = bread(dir->i_dev, indblk);
        if (!ibh) return -1;
        blk = ((unsigned short *)ibh->b_data)[block - 7];
        brelse(ibh);
        if (!blk) return -1;
        bh = bread(dir->i_dev, blk);
    }
    if (!bh) return -1;

    de = (struct minix_dir_entry *)(bh->b_data + offset);
    *bh_out = bh;
    *de_out = de;
    return 0;
}

static int name_eq(const char *de_name, const char *name, int namelen)
{
    int j;
    for (j = 0; j < namelen && j < 14; j++) {
        if (de_name[j] != name[j]) return 0;
    }
    if (j == namelen) {
        if (j == 14 || de_name[j] == '\0') return 1;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------
 * Permissions (Linux 0.01 fs/namei.c, same rules and same masks)
 *
 * Until now i_mode/i_uid/i_gid were stored but never consulted: the
 * only task in the system was uid 0, so "who may do what" never came
 * up.  The moment a program calls setuid() to drop privileges that
 * stops being true, and the filesystem has to answer the question.
 *
 *   - euid == file owner        -> the owner triad (bits 6..8)
 *   - egid == file group        -> the group triad (bits 3..5)
 *   - otherwise                 -> the other triad (bits 0..2)
 *   - root (euid 0) overrides everything, as in 0.01
 *   - a deleted inode (no links) is inaccessible even to root: its data
 *     zones are already gone
 * ------------------------------------------------------------------ */

int suser(void)
{
    return current->euid == 0;
}

int permission(struct m_inode *inode, int mask)
{
    int mode;

    if (!inode)
        return 0;

    mode = inode->i_mode;

    if (inode->i_dev && !inode->i_nlinks)
        return 0;                       /* unlinked: nobody, not even root */

    if (current->euid == inode->i_uid)
        mode >>= 6;
    else if (current->egid == inode->i_gid)
        mode >>= 3;

    if ((mode & mask & 7) == mask)
        return 1;
    return suser();
}

static int find_entry(struct m_inode *dir, const char *name, int namelen,
                      unsigned short *res_inode)
{
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    int i, entries;

    if (!dir || !(dir->i_mode & 0x4000)) return -1;

    entries = dir->i_size / sizeof(struct minix_dir_entry);
    if (entries == 0) return -1;

    for (i = 0; i < entries; i++) {
        if (next_entry(dir, i, &bh, &de)) continue;
        if (name_eq(de->name, name, namelen)) {
            unsigned short ino = de->inode;
            brelse(bh);
            if (!ino) return -1;
            *res_inode = ino;
            return 0;
        }
        brelse(bh);
    }
    return -1;
}

/* Resolve a pathname to an inode.  Absolute paths ("/a/b") start at
   current->root if chroot() was called (else the fs root); relative
   paths ("a/b", "b") start at current->pwd.  The empty string means
   the current directory.  Returns an inode with a held reference
   (caller must iput it) or NULL.

   Every directory that is *traversed* (not the final component) must be
   executable by the caller: that is what makes a directory's x bit mean
   "you may look inside", and it is checked here so that every syscall
   gets it for free, exactly as 0.01's dir_namei() did. */
struct m_inode *namei(const char *pathname)
{
    struct m_inode *inode;
    const char *p;
    char name[16];
    int namelen;
    unsigned short ino = 1;
    int dev = 0x301;

    if (!pathname) return NULL;

    if (*pathname == '/') {
        pathname++;
        if (current->root) {
            inode = current->root;
            inode->i_count++;          /* extra ref for the walk */
        } else {
            inode = iget(dev, 1);
        }
    } else {
        /* relative: start at the current working directory */
        inode = current->pwd;
        if (!inode)
            return NULL;
        inode->i_count++;          /* extra ref for the walk */
    }

    if (*pathname == '\0')
        return inode;              /* "/" or "" -> root or pwd */

    if (!inode) {
        /* The starting point itself (root or pwd) could not be read: a
           full inode table reaches here, and so does an unreadable root
           block.  This used to return silently, which made "namei
           returned NULL" unattributable. */
        printk("namei: cannot get the starting inode (inode table %d/%d "
               "used)\n", iget_used(), NR_INODE);
        return NULL;
    }

    p = pathname;
    while (1) {
        namelen = 0;
        while (*p && *p != '/' && namelen < 15) {
            name[namelen++] = *p++;
        }
        name[namelen] = '\0';
        while (*p == '/') p++;

        if (namelen == 1 && name[0] == '.') {
            if (*p == '\0')
                return inode;          /* "." -> this directory */
            if (!permission(inode, MAY_EXEC)) {
                iput(inode);           /* "./x" keeps walking from here */
                return NULL;
            }
            continue;
        }

        if (find_entry(inode, name, namelen, &ino) < 0) {
#if NAMEI_TRACE
            {
                int k;

                printk("namei: no entry '");
                for (k = 0; k < namelen; k++)
                    printk("%c", name[k]);
                printk("' (len %d) in dir ino=%d mode=0%o size=%d count=%d, "
                       "inode table %d/%d used\n",
                       namelen, inode->i_num, inode->i_mode,
                       (int)inode->i_size, inode->i_count,
                       iget_used(), NR_INODE);
            }
#endif
            iput(inode);
            return NULL;
        }
        iput(inode);
        inode = iget(dev, ino);
        if (!inode) {
            printk("namei: iget(%d, %d) returned NULL (inode table %d/%d "
                   "used)\n", dev, ino, iget_used(), NR_INODE);
            return NULL;
        }

        if (*p == '\0') return inode;

        /* not the last component: it must be a usable directory */
        if (!(inode->i_mode & S_IFDIR) || !permission(inode, MAY_EXEC)) {
            printk("namei: cannot traverse ino=%d mode=0%o (is_dir=%d "
                   "exec_ok=%d, euid=%d uid=%d egid=%d gid=%d)\n",
                   inode->i_num, inode->i_mode,
                   !!(inode->i_mode & S_IFDIR),
                   permission(inode, MAY_EXEC),
                   current->euid, inode->i_uid,
                   current->egid, inode->i_gid);
            iput(inode);
            return NULL;
        }
    }

    return inode;
}

/* --- public helpers for file/dir creation (sys_mknod / sys_mkdir) --- */

/* Look up name in dir: 0 if present (ino_out gets its inode number),
   -1 if absent. */
int dir_lookup(struct m_inode *dir, const char *name, int namelen,
               unsigned short *ino_out)
{
    unsigned short ino;
    int r = find_entry(dir, name, namelen, &ino);
    if (r == 0 && ino_out)
        *ino_out = ino;
    return r;
}

/* Add an entry (ino, name) to dir: reuse a free slot or append one.
   Returns 0 on success, -1 if the directory's zones are exhausted. */
/* Make sure directory block `block` (in 1KB zone units) exists, allocating
   a new zone (and the single-indirect block when block >= 7) on demand.
   This lets a directory grow beyond the 7 direct zones (>64 entries).
   Returns 0 or -1. */
static int ensure_dir_block(struct m_inode *dir, int block)
{
    struct buffer_head *ibh;
    unsigned short blk, indblk;

    if (block < 7) {
        if (dir->i_zone[block])
            return 0;
        blk = (unsigned short)new_block(dir->i_dev);
        if (!blk)
            return -1;
        dir->i_zone[block] = blk;
        dir->i_dirt = 1;
        return 0;
    }

    /* single-indirect: i_zone[7] -> a block of unsigned short zone ids */
    indblk = dir->i_zone[7];
    if (!indblk) {
        indblk = (unsigned short)new_block(dir->i_dev);
        if (!indblk)
            return -1;
        dir->i_zone[7] = indblk;
        dir->i_dirt = 1;
    }
    ibh = bread(dir->i_dev, indblk);
    if (!ibh)
        return -1;
    blk = ((unsigned short *)ibh->b_data)[block - 7];
    if (blk) {
        brelse(ibh);
        return 0;
    }
    blk = (unsigned short)new_block(dir->i_dev);
    if (!blk) {
        brelse(ibh);
        return -1;
    }
    ((unsigned short *)ibh->b_data)[block - 7] = blk;
    ibh->b_dirt = 1;
    brelse(ibh);
    return 0;
}

int dir_add_entry(struct m_inode *dir, const char *name, int namelen,
                  unsigned short ino)
{
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    int i, entries, k;

    if (!dir || namelen > 14)
        return -1;

    entries = dir->i_size / sizeof(struct minix_dir_entry);

    /* 1) reuse a free slot */
    for (i = 0; i < entries; i++) {
        if (next_entry(dir, i, &bh, &de))
            continue;
        if (de->inode == 0)
            goto found;
        brelse(bh);
    }

    /* 2) append one, growing the directory into indirect zones as needed
       (direct zones 0..6 then a single-indirect block via i_zone[7]) */
    i = entries;
    if (ensure_dir_block(dir, i * sizeof(struct minix_dir_entry) / BLOCK_SIZE) < 0)
        return -1;
    if (next_entry(dir, i, &bh, &de))
        return -1;

found:
    de->inode = ino;
    for (k = 0; k < 14; k++)
        de->name[k] = (k < namelen) ? name[k] : 0;
    bh->b_dirt = 1;
    brelse(bh);

    if (i >= entries) {
        dir->i_size += sizeof(struct minix_dir_entry);
        dir->i_dirt = 1;
    }
    return 0;
}

/* Free every data zone a directory owns (direct zones + the single-indirect
   block and the zones it points to) so rmdir does not leak blocks. */
void free_dir_zones(struct m_inode *inode)
{
    int i;

    for (i = 0; i < 7; i++) {
        if (inode->i_zone[i]) {
            free_block(inode->i_dev, inode->i_zone[i]);
            inode->i_zone[i] = 0;
        }
    }
    if (inode->i_zone[7]) {
        struct buffer_head *ibh = bread(inode->i_dev, inode->i_zone[7]);
        if (ibh) {
            int j;
            for (j = 0; j < BLOCK_SIZE / 2; j++) {
                unsigned short b = ((unsigned short *)ibh->b_data)[j];
                if (b)
                    free_block(inode->i_dev, b);
            }
            brelse(ibh);
        }
        free_block(inode->i_dev, inode->i_zone[7]);
        inode->i_zone[7] = 0;
    }
}

/* Clear the entry for (name) in dir.  Returns 0 or -1. */
int dir_remove_entry(struct m_inode *dir, const char *name, int namelen)
{
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    int i, entries;

    entries = dir->i_size / sizeof(struct minix_dir_entry);
    for (i = 0; i < entries; i++) {
        if (next_entry(dir, i, &bh, &de))
            continue;
        if (de->inode && name_eq(de->name, name, namelen)) {
            de->inode = 0;
            bh->b_dirt = 1;
            brelse(bh);
            return 0;
        }
        brelse(bh);
    }
    return -1;
}

/* 1 if dir contains nothing but '.' and '..'. */
int dir_is_empty(struct m_inode *dir)
{
    struct buffer_head *bh;
    struct minix_dir_entry *de;
    int i, entries;

    entries = dir->i_size / sizeof(struct minix_dir_entry);
    for (i = 2; i < entries; i++) {          /* skip '.' and '..' */
        if (next_entry(dir, i, &bh, &de))
            continue;
        if (de->inode) {
            brelse(bh);
            return 0;
        }
        brelse(bh);
    }
    return 1;
}

/* Split "a/b/c" into dirpath="a/b" and name="c" (name ≤ 14 chars).
   A bare name resolves against the current directory (dirpath = ""). */
int split_path(const char *path, char *dirpath, char *name)
{
    const char *slash;
    int len;

    if (!path || !*path)
        return -1;

    slash = strrchr(path, '/');
    if (!slash) {
        dirpath[0] = '\0';             /* relative to pwd */
        len = (int)strlen(path);
        if (len == 0 || len > 14)
            return -1;
        strcpy(name, path);
        return 0;
    }

    if (slash == path) {
        strcpy(dirpath, "/");
    } else {
        memcpy(dirpath, path, (unsigned long)(slash - path));
        dirpath[slash - path] = '\0';
    }
    len = (int)strlen(slash + 1);
    if (len == 0 || len > 14)
        return -1;
    strcpy(name, slash + 1);
    return 0;
}