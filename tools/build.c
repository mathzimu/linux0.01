#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SETUP_SECTORS 4
#define BOOT_SIZE 512
#define SECTOR_SIZE 512

static void die(const char *msg)
{
    fprintf(stderr, "build: %s\n", msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    FILE *boot_f, *setup_f, *system_f, *out;
    unsigned char buf[65536];
    int len;
    long setup_size, system_size;
    unsigned int setup_sectors, kernel_sectors;

    if (argc != 4)
        die("Usage: build boot setup system");

    /* --- Read boot sector --- */
    boot_f = fopen(argv[1], "rb");
    if (!boot_f) die("Cannot open boot file");
    len = fread(buf, 1, BOOT_SIZE, boot_f);
    fclose(boot_f);

    if (len != BOOT_SIZE)
        die("Boot must be exactly 512 bytes");
    if (buf[510] != 0x55 || buf[511] != 0xAA)
        die("Boot must have 0xAA55 signature");

    /* --- Measure setup --- */
    setup_f = fopen(argv[2], "rb");
    if (!setup_f) die("Cannot open setup file");
    fseek(setup_f, 0, SEEK_END);
    setup_size = ftell(setup_f);
    fseek(setup_f, 0, SEEK_SET);

    setup_sectors = (setup_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (setup_sectors > SETUP_SECTORS) {
        fprintf(stderr, "Warning: setup is %ld bytes, needs %d sectors; truncating to %d\n",
                setup_size, setup_sectors, SETUP_SECTORS);
        setup_sectors = SETUP_SECTORS;
    }
    /* Always pad setup to exactly SETUP_SECTORS to match boot.s */
    setup_sectors = SETUP_SECTORS;

    /* --- Measure system --- */
    system_f = fopen(argv[3], "rb");
    if (!system_f) die("Cannot open system file");
    fseek(system_f, 0, SEEK_END);
    system_size = ftell(system_f);
    fseek(system_f, 0, SEEK_SET);

    kernel_sectors = (system_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    fprintf(stderr, "Kernel: %ld bytes -> %d sectors\n", system_size, kernel_sectors);
    fprintf(stderr, "Setup: %ld bytes -> %d sectors\n", setup_size, setup_sectors);

    /* --- Patch boot sector with sector counts --- */
    buf[0x1F0] = setup_sectors & 0xFF;
    buf[0x1F1] = (setup_sectors >> 8) & 0xFF;
    buf[0x1F2] = kernel_sectors & 0xFF;
    buf[0x1F3] = (kernel_sectors >> 8) & 0xFF;

    /* --- Open output image --- */
    out = fopen("Image", "wb");
    if (!out) die("Cannot create Image");

    /* --- Write boot sector (512 bytes) --- */
    if (fwrite(buf, 1, BOOT_SIZE, out) != BOOT_SIZE)
        die("Write error on boot");

    /* --- Write setup, padded to SETUP_SECTORS sectors --- */
    memset(buf, 0, sizeof(buf));
    len = fread(buf, 1, sizeof(buf), setup_f);
    fclose(setup_f);
    if (fwrite(buf, 1, setup_sectors * SECTOR_SIZE, out) != (unsigned)(setup_sectors * SECTOR_SIZE))
        die("Write error on setup");

    /* --- Write system, padded to sector boundary --- */
    memset(buf, 0, sizeof(buf));
    while ((len = fread(buf, 1, sizeof(buf), system_f)) > 0)
        if (fwrite(buf, 1, len, out) != (size_t)len)
            die("Write error on system");
    fclose(system_f);

    /* Pad system to sector boundary */
    {
        unsigned int remain = (kernel_sectors * SECTOR_SIZE) - system_size;
        if (remain > 0) {
            memset(buf, 0, sizeof(buf));
            fwrite(buf, 1, remain, out);
        }
    }

    fclose(out);

    struct stat st;
    if (stat("Image", &st) == 0)
        fprintf(stderr, "Image: %ld bytes (%ld KB)\n",
                (long)st.st_size, (long)(st.st_size / 1024));

    return 0;
}