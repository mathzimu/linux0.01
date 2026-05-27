#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SETUP_SECTORS 4
#define BOOT_SIZE 512
#define SECTOR_SIZE 512
#define FLOPPY_SIZE (1440 * 1024)

static void die(const char *msg)
{
    fprintf(stderr, "build: %s\n", msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    FILE *f, *out;
    unsigned char buf[65536];
    int len, i;
    long file_size;
    unsigned int remain;

    if (argc != 4)
        die("Usage: build boot setup system");

    /* Read boot sector */
    f = fopen(argv[1], "rb");
    if (!f) die("Cannot open boot file");
    len = fread(buf, 1, BOOT_SIZE, f);
    fclose(f);

    if (len != BOOT_SIZE)
        die("Boot must be exactly 512 bytes");
    if (buf[510] != 0x55 || buf[511] != 0xAA)
        die("Boot must have 0xAA55 signature");

    /* Read setup, compute setup sectors */
    f = fopen(argv[2], "rb");
    if (!f) die("Cannot open setup file");
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned int setup_sectors = (file_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (setup_sectors > SETUP_SECTORS) {
        fprintf(stderr, "Warning: setup is %ld bytes, needs %d sectors; padding to %d\n",
                file_size, setup_sectors, SETUP_SECTORS);
        setup_sectors = SETUP_SECTORS;
    }

    /* Read system, compute kernel sectors */
    f = fopen(argv[3], "rb");
    if (!f) die("Cannot open system file");
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned int kernel_sectors = (file_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    fprintf(stderr, "Kernel: %ld bytes -> %d sectors\n", file_size, kernel_sectors);
    fprintf(stderr, "Setup: %ld bytes -> %d sectors\n", file_size, setup_sectors);

    /* Write setup and kernel sector counts into boot sector */
    buf[0x1F0] = setup_sectors & 0xFF;
    buf[0x1F1] = (setup_sectors >> 8) & 0xFF;
    buf[0x1F2] = kernel_sectors & 0xFF;
    buf[0x1F3] = (kernel_sectors >> 8) & 0xFF;

    /* Open output image */
    out = fopen("Image", "wb");
    if (!out) die("Cannot create Image");

    /* Write boot sector (512 bytes) */
    if (fwrite(buf, 1, BOOT_SIZE, out) != BOOT_SIZE)
        die("Write error on boot");

    /* Write setup, padded to SETUP_SECTORS sectors */
    memset(buf, 0, sizeof(buf));
    len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (fwrite(buf, 1, setup_sectors * SECTOR_SIZE, out) != setup_sectors * SECTOR_SIZE)
        die("Write error on setup");

    /* Write system, padded to sector boundary */
    f = fopen(argv[3], "rb");
    if (!f) die("Cannot reopen system");
    memset(buf, 0, sizeof(buf));
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0)
        if (fwrite(buf, 1, len, out) != (size_t)len)
            die("Write error on system");
    fclose(f);

    /* Pad to sector boundary */
    remain = (kernel_sectors * SECTOR_SIZE) - file_size;
    memset(buf, 0, sizeof(buf));
    if (remain > 0)
        fwrite(buf, 1, remain, out);

    /* Pad to full floppy size? No, keep minimal */

    fclose(out);

    struct stat st;
    if (stat("Image", &st) == 0)
        fprintf(stderr, "Image: %ld bytes (%ld KB)\n",
                (long)st.st_size, (long)(st.st_size / 1024));

    return 0;
}
