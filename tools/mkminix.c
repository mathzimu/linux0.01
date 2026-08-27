/*
 * mkminix.c - create a MINIX v1 filesystem image for the teaching kernel.
 *
 * The kernel's hd driver reads the whole disk as one MINIX v1 filesystem
 * (superblock at disk block 1), so this tool lays out a flat image:
 *
 *   block 0              : boot block (zeros)
 *   block 1              : superblock
 *   block 2..2+imap-1    : inode bitmap
 *   ...+zmap             : zone bitmap
 *   ...+ninode-blocks    : inode table (32-byte d_inodes, 32/block)
 *   firstdatazone ..     : data zones
 *
 * Usage: mkminix [output.img]   (default: minix.img)
 * The image is 128 KB with a root dir containing:
 *   hello.txt  readme.txt  big.txt (18 KB, uses the indirect zone)
 *   docs/note.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 1024
#define MINIX_MAGIC 0x137F
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define MODE_REG (S_IFREG | 0644)
#define MODE_DIR (S_IFDIR | 0755)

/* minix v1 on-disk structures (packed, little-endian x86).
   NB: use explicit 32-bit types — the host may be 64-bit where
   `unsigned long` is 8 bytes, but the kernel (i386) sees 4. */
struct d_superblock {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned int s_max_size;
    unsigned short s_magic;
} __attribute__((packed));

struct d_inode {
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned int i_size;
    unsigned int i_time;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
} __attribute__((packed));

struct dir_entry {
    unsigned short inode;
    char name[14];
} __attribute__((packed));

#define NINODES 128
#define IMAP_BLOCKS 1
#define ZMAP_BLOCKS 1
#define INODE_BLOCKS (NINODES / 32)          /* 4 */
#define FIRSTDATAZONE (2 + IMAP_BLOCKS + ZMAP_BLOCKS + INODE_BLOCKS) /* 8 */
#define NZONES 128                           /* 128 KB image */

static unsigned char img[NZONES * BLOCK];
static unsigned char imap[BLOCK];
static unsigned char zmap[BLOCK];

static int next_zone = FIRSTDATAZONE;

static void put_super(void)
{
    struct d_superblock *sb = (struct d_superblock *)(img + BLOCK);

    sb->s_ninodes = NINODES;
    sb->s_nzones = NZONES;
    sb->s_imap_blocks = IMAP_BLOCKS;
    sb->s_zmap_blocks = ZMAP_BLOCKS;
    sb->s_firstdatazone = FIRSTDATAZONE;
    sb->s_log_zone_size = 0;
    sb->s_max_size = (7 + BLOCK / 2) * BLOCK;  /* 7 direct + 512 indirect */
    sb->s_magic = MINIX_MAGIC;
}

/* Allocate a zone and mark it used in the zmap.
   Kernel convention: zmap bit j <-> zone (s_firstdatazone + j). */
static int alloc_zone(void)
{
    int z = next_zone++;
    if (z >= NZONES) {
        fprintf(stderr, "mkminix: out of zones\n");
        exit(1);
    }
    {
        int bit = z - FIRSTDATAZONE;
        zmap[bit / 8] |= (1 << (bit % 8));
    }
    return z;
}

/* Write an inode into the inode table. */
static void put_inode(int num, unsigned short mode, unsigned long size,
                      int nlinks, unsigned short *zones, int nzones)
{
    struct d_inode *di;
    int i;

    if (num < 1 || num > NINODES) {
        fprintf(stderr, "mkminix: bad inode %d\n", num);
        exit(1);
    }
    di = (struct d_inode *)(img + (2 + IMAP_BLOCKS + ZMAP_BLOCKS) * BLOCK) + (num - 1);
    di->i_mode = mode;
    di->i_uid = 0;
    di->i_size = size;
    di->i_time = 1000000;          /* arbitrary */
    di->i_gid = 0;
    di->i_nlinks = nlinks;
    for (i = 0; i < nzones && i < 9; i++)
        di->i_zone[i] = zones[i];

    /* Kernel convention: imap bit j <-> inode (j+1), i.e. 0-based.
       (new_inode() scans for a clear bit j and returns inode j+1.) */
    imap[(num - 1) / 8] |= (1 << ((num - 1) % 8));
}

static void add_dir_entry(int dir_zone, int ino, const char *name)
{
    struct dir_entry *de = (struct dir_entry *)(img + dir_zone * BLOCK);

    while (de->inode != 0)
        de++;
    de->inode = ino;
    strncpy(de->name, name, 14);
}

static void fill(int zone, const char *text, int len)
{
    unsigned char *p = img + zone * BLOCK;
    int i;
    for (i = 0; i < BLOCK; i++)
        p[i] = text[i % len];
}

int main(int argc, char *argv[])
{
    const char *out = argc > 1 ? argv[1] : "minix.img";
    int root_zone, docs_zone, ind_zone, i;
    unsigned short z;

    memset(img, 0, sizeof(img));
    memset(imap, 0, sizeof(imap));
    memset(zmap, 0, sizeof(zmap));

    put_super();

    /* --- root dir (inode 1): hello.txt, readme.txt, big.txt, docs,
       hello (ELF for execve) --- */
    root_zone = alloc_zone();
    put_inode(1, MODE_DIR, 5 * sizeof(struct dir_entry), 2,
              (unsigned short[]){root_zone}, 1);
    add_dir_entry(root_zone, 2, "hello.txt");
    add_dir_entry(root_zone, 3, "readme.txt");
    add_dir_entry(root_zone, 4, "big.txt");
    add_dir_entry(root_zone, 5, "docs");
    add_dir_entry(root_zone, 7, "hello");

    /* --- hello.txt (inode 3) --- */
    {
        const char *text = "Hello from MINIX v1!\n";
        int zone = alloc_zone();
        fill(zone, text, (int)strlen(text));
        put_inode(2, MODE_REG, (unsigned long)strlen(text), 1,
                  (unsigned short[]){zone}, 1);
    }

    /* --- readme.txt (inode 4) --- */
    {
        const char *text =
            "Minimal Linux 0.01 equivalent kernel.\n"
            "This file is read from the MINIX v1 filesystem\n"
            "through open()/read()/close() via int 0x80.\n";
        int zone = alloc_zone();
        fill(zone, text, (int)strlen(text));
        put_inode(3, MODE_REG, (unsigned long)strlen(text), 1,
                  (unsigned short[]){zone}, 1);
    }

    /* --- big.txt (inode 5): 7 direct + 11 indirect = 18 zones --- */
    {
        unsigned short zones[18];
        const char *pat = "0123456789abcdef\n";   /* 18 bytes */
        for (i = 0; i < 18; i++) {
            zones[i] = alloc_zone();
            fill(zones[i], pat, 18);
        }
        ind_zone = alloc_zone();                   /* indirect block */
        {
            unsigned short *ind = (unsigned short *)(img + ind_zone * BLOCK);
            for (i = 0; i < 11; i++)
                ind[i] = zones[7 + i];
        }
        put_inode(4, MODE_REG, 18UL * BLOCK, 1, zones, 7);  /* 7 direct */
        /* i_zone[7] = indirect block (index 7) */
        {
            struct d_inode *di = (struct d_inode *)
                (img + (2 + IMAP_BLOCKS + ZMAP_BLOCKS) * BLOCK) + 3;
            di->i_zone[7] = (unsigned short)ind_zone;
        }
    }

    /* --- docs/ (inode 6) + note.txt (inode 7) --- */
    docs_zone = alloc_zone();
    put_inode(5, MODE_DIR, 1 * sizeof(struct dir_entry), 2,
              (unsigned short[]){docs_zone}, 1);
    add_dir_entry(docs_zone, 6, "note.txt");
    {
        const char *text = "A file inside a subdirectory.\n";
        int zone = alloc_zone();
        fill(zone, text, (int)strlen(text));
        put_inode(6, MODE_REG, (unsigned long)strlen(text), 1,
                  (unsigned short[]){zone}, 1);
    }

    /* --- assemble: super/imaps at their fixed blocks --- */
    memcpy(img + 2 * BLOCK, imap, BLOCK);
    memcpy(img + (2 + IMAP_BLOCKS) * BLOCK, zmap, BLOCK);

    /* --- /hello (inode 7): the execve demo ELF, read from file --- */
    {
        static unsigned char elf_buf[64 * 1024];
        FILE *ef = fopen("user/hello.elf", "rb");
        int elen, nz, i;

        if (!ef) {
            fprintf(stderr, "mkminix: cannot open user/hello.elf\n");
            return 1;
        }
        elen = (int)fread(elf_buf, 1, sizeof(elf_buf), ef);
        fclose(ef);

        nz = (elen + BLOCK - 1) / BLOCK;
        if (nz > 7) {
            fprintf(stderr, "mkminix: hello.elf too big (%d zones)\n", nz);
            return 1;
        }
        {
            unsigned short zones[9] = {0};
            for (i = 0; i < nz; i++) {
                int len = elen - i * BLOCK;
                zones[i] = alloc_zone();
                if (len > BLOCK)
                    len = BLOCK;
                memset(img + zones[i] * BLOCK, 0, BLOCK);
                memcpy(img + zones[i] * BLOCK, elf_buf + i * BLOCK, len);
            }
            put_inode(7, MODE_REG, (unsigned long)elen, 1, zones, nz);
        }
        printf("mkminix: embedded hello.elf (%d bytes, %d zones)\n",
               elen, nz);
    }

    {
        FILE *f = fopen(out, "wb");
        if (!f) {
            perror("mkminix");
            return 1;
        }
        if (fwrite(img, 1, sizeof(img), f) != sizeof(img)) {
            perror("mkminix: write");
            return 1;
        }
        fclose(f);
    }
    printf("mkminix: wrote %s (%d KB, %d zones used, firstdatazone=%d)\n",
           out, (int)(sizeof(img) / 1024), next_zone - FIRSTDATAZONE,
           FIRSTDATAZONE);
    return 0;
}
