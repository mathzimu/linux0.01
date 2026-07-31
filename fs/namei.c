#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/segment.h>

struct minix_dir_entry {
    unsigned short inode;
    char name[14];
};

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
    for (j = 0; j < 14; j++) {
        if (j == namelen) {
            if (de_name[j] == '\0') return 1;
            return 0;
        }
        if (de_name[j] != name[j]) return 0;
    }
    if (namelen != 14) return 0;
    return 1;
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

struct m_inode *namei(const char *pathname)
{
    struct m_inode *inode;
    const char *p;
    char name[16];
    int namelen;
    unsigned short ino = 1;
    int dev = 0x301;

    if (!pathname) return NULL;

    if (*pathname == '/') pathname++;

    if (*pathname == '\0') {
        inode = iget(dev, 1);
        return inode;
    }

    inode = iget(dev, 1);
    if (!inode) return NULL;

    p = pathname;
    while (1) {
        namelen = 0;
        while (*p && *p != '/' && namelen < 15) {
            name[namelen++] = *p++;
        }
        name[namelen] = '\0';
        while (*p == '/') p++;

        if (find_entry(inode, name, namelen, &ino) < 0) {
            iput(inode);
            return NULL;
        }
        iput(inode);
        inode = iget(dev, ino);
        if (!inode) return NULL;

        if (*p == '\0') return inode;
    }

    return inode;
}