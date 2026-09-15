/*
 * mkdisk.c - assemble the single-file, self-booting hard-disk image.
 *
 * `make run` needs two files: the floppy Image (boot sector + setup +
 * kernel) and minix.img, which the kernel mounts as its root filesystem
 * from the first IDE disk.  This tool merges them into ONE image that
 * boots on its own with `qemu-system-i386 -hda linux.img -boot c`:
 *
 *   LBA 0                    boot sector (copied from Image, with the
 *                            root filesystem base LBA patched in)
 *   LBA 1..setup_sectors     setup
 *   ...                      kernel image, padded to a sector
 *   fs_base..fs_base+fs_len  minix.img: 1MB MINIX v1 fs + 2MB raw swap
 *   ...                      zeroes up to a whole cylinder
 *
 * The kernel does not know fs_base at compile time.  The offset is
 * written into this image's own boot sector (0x1F4), boot/boot.s forwards
 * it to setup.s in %ebx, setup.s parks it in the boot parameter block and
 * main() installs it in the disk driver (see include/linux/memmap.h,
 * BOOT_FS_BASE_ADDR).  Deriving it here, from the kernel size of the
 * image being written, is what makes the two impossible to drift apart:
 * there is no second copy of the number to keep in step.
 *
 * Usage: tools/mkdisk <out.img> <Image> <fs.img>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SECTOR 512
#define BOOT_SIZE 512
#define FS_BASE_FIELD 0x1F4
#define FS_ALIGN 8              /* sectors: 4KB, the fs is read in 1KB blocks */
#define MINIX_MAGIC_OFF (1024 + 16)     /* superblock magic, byte offset */

/* The kernel's IDE driver (drivers/hd.c) addresses the disk as 16 heads /
   63 sectors per track, because that is the geometry QEMU derives for an
   unspecified IDE drive from the image size: cylinders = sectors / 1008,
   rounded down.  Any tail past the last whole cylinder is therefore
   unreachable - and the raw swap area sits at the very end of the image,
   so losing that tail would cost swap slots.  Padding the image to a
   whole cylinder keeps every LBA in it addressable. */
#define SECTORS_PER_TRACK 63
#define HEADS 16
#define CYL_SECTORS (HEADS * SECTORS_PER_TRACK)         /* 1008 */

static void die(const char *msg)
{
    fprintf(stderr, "mkdisk: %s\n", msg);
    exit(1);
}

/* Read a whole file into a fresh buffer; returns NULL on failure. */
static unsigned char *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len;

    if (!f) {
        fprintf(stderr, "mkdisk: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fprintf(stderr, "mkdisk: %s is empty\n", path);
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)len);
    if (!buf) die("out of memory");
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "mkdisk: short read on %s\n", path);
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *size = len;
    return buf;
}

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

int main(int argc, char *argv[])
{
    const char *out_path, *image_path, *fs_path;
    unsigned char *image = NULL, *fs = NULL;
    unsigned char *disk = NULL;
    long image_len, fs_len;
    unsigned int setup_sectors, kernel_sectors, header_sectors;
    unsigned int fs_base, fs_sectors, total;
    unsigned long disk_bytes;
    FILE *out;

    if (argc != 4)
        die("Usage: mkdisk <out.img> <Image> <fs.img>");
    out_path = argv[1];
    image_path = argv[2];
    fs_path = argv[3];

    /* --- the header: exactly the boot sector, setup and kernel that the
       floppy Image boots, so both media run the same code --------------- */
    image = slurp(image_path, &image_len);
    if (!image) return 1;
    if (image_len < BOOT_SIZE)
        die("Image is smaller than a boot sector");
    if (image[510] != 0x55 || image[511] != 0xAA)
        die("Image has no boot signature at offset 510");

    setup_sectors = le16(image + 0x1F0);
    kernel_sectors = le16(image + 0x1F2);
    if (setup_sectors == 0 || kernel_sectors == 0)
        die("Image boot sector carries no setup/kernel sector counts");
    header_sectors = 1 + setup_sectors + kernel_sectors;
    if ((long)header_sectors * SECTOR > image_len)
        die("Image is shorter than the setup + kernel it claims to hold");

    /* --- the filesystem, placed on the first 1024-byte boundary behind
       the kernel -------------------------------------------------------- */
    fs = slurp(fs_path, &fs_len);
    if (!fs) return 1;
    if (fs_len % SECTOR) {
        fprintf(stderr, "mkdisk: %s is %ld bytes, not a multiple of %d\n",
                fs_path, fs_len, SECTOR);
        return 1;
    }
    fs_sectors = (unsigned int)(fs_len / SECTOR);
    fs_base = (header_sectors + FS_ALIGN - 1) & ~(unsigned int)(FS_ALIGN - 1);

    /* A filesystem image that is not MINIX v1 would boot all the way to
       sys_setup()'s "bad magic" panic path, so catch it at build time. */
    if (fs_len < MINIX_MAGIC_OFF + 2 ||
        fs[MINIX_MAGIC_OFF] != 0x7F || fs[MINIX_MAGIC_OFF + 1] != 0x13) {
        fprintf(stderr, "mkdisk: %s does not look like a MINIX v1 "
                "filesystem (magic 0x%02x%02x at offset %d)\n", fs_path,
                fs[MINIX_MAGIC_OFF + 1], fs[MINIX_MAGIC_OFF], MINIX_MAGIC_OFF);
        return 1;
    }

    total = fs_base + fs_sectors;
    if (total % CYL_SECTORS)
        total += CYL_SECTORS - (total % CYL_SECTORS);
    disk_bytes = (unsigned long)total * SECTOR;

    /* --- assemble: header, zeroes, filesystem, zeroes ---------------- */
    disk = calloc((size_t)total, SECTOR);
    if (!disk) die("out of memory");
    memcpy(disk, image, (size_t)header_sectors * SECTOR);
    memcpy(disk + (size_t)fs_base * SECTOR, fs, (size_t)fs_len);

    /* Patch the base LBA into the copy of the boot sector (never into
       Image itself: that one must keep booting the two-file setup). */
    disk[FS_BASE_FIELD + 0] = fs_base & 0xFF;
    disk[FS_BASE_FIELD + 1] = (fs_base >> 8) & 0xFF;
    disk[FS_BASE_FIELD + 2] = (fs_base >> 16) & 0xFF;
    disk[FS_BASE_FIELD + 3] = (fs_base >> 24) & 0xFF;

    out = fopen(out_path, "wb");
    if (!out) die("cannot create the output image");
    if (fwrite(disk, 1, (size_t)disk_bytes, out) != (size_t)disk_bytes)
        die("write error");
    fclose(out);

    printf("mkdisk: %s\n", out_path);
    printf("  boot sector            LBA 0\n");
    printf("  setup                  %u sectors (LBA 1..%u)\n",
           setup_sectors, setup_sectors);
    printf("  kernel                 %u sectors (LBA %u..%u)\n",
           kernel_sectors, 1 + setup_sectors, header_sectors - 1);
    printf("  root filesystem        LBA %u (byte %lu), %u sectors\n",
           fs_base, (unsigned long)fs_base * SECTOR, fs_sectors);
    /* The raw swap area is the tail of the filesystem image, at
       SWAP_START_LBA of it (include/linux/memmap.h).  It is copied
       verbatim along with the image; nothing here computes or duplicates
       that offset. */
    printf("  swap                   the tail of that image (SWAP_START_LBA)\n");
    printf("  total                  %u sectors, %lu bytes (%u cylinders "
           "x %d heads x %d sectors)\n",
           total, disk_bytes, total / CYL_SECTORS, HEADS, SECTORS_PER_TRACK);

    free(image);
    free(fs);
    free(disk);
    return 0;
}
