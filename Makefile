# Minimal Linux 0.01 Kernel - Makefile
# Supports: native Linux, Docker, cross-compiler
#
# 依赖要求（详见 DEPENDENCIES.md）：
#   必需: as, gcc, ld, objcopy, make
#   运行: qemu-system-i386
#   ISO:  xorriso (或 genisoimage/mkisofs)
#
#  Linux:  sudo apt install build-essential gcc-multilib qemu-system-x86 xorriso
#  macOS:  brew install qemu xorriso && 使用 Docker 编译
#  Docker: docker build -t linux-0.01-builder .

# --- Toolchain auto-detection ---
ifeq ($(shell uname -s),Linux)
  # Linux native: use GCC + binutils
  AS      = as
  CC      = gcc
  LD      = ld
  OBJCOPY = objcopy
  ASFLAGS = -32
  CFLAGS  = -m32 -Wall -O0 -fstrength-reduce -fomit-frame-pointer \
            -nostdinc -Iinclude -fno-stack-protector -fno-builtin \
            -ffreestanding
  LDFLAGS = -m elf_i386 -T kernel.ld -e startup_32
else ifneq ($(shell command -v i386-elf-gcc 2>/dev/null),)
  # macOS with i386-elf-* cross-compiler
  AS      = i386-elf-as
  CC      = i386-elf-gcc
  LD      = i386-elf-ld
  OBJCOPY = i386-elf-objcopy
  ASFLAGS =
  CFLAGS  = -Wall -O0 -fstrength-reduce -fomit-frame-pointer \
            -nostdinc -Iinclude -fno-stack-protector -fno-builtin \
            -ffreestanding -MMD -MP
  LDFLAGS = -T kernel.ld -e startup_32
else ifneq ($(shell command -v i686-elf-gcc 2>/dev/null),)
  # macOS with Homebrew i686-elf-* cross-compiler
  AS      = i686-elf-as
  CC      = i686-elf-gcc
  LD      = i686-elf-ld
  OBJCOPY = i686-elf-objcopy
  ASFLAGS =
  CFLAGS  = -Wall -O0 -fstrength-reduce -fomit-frame-pointer \
            -nostdinc -Iinclude -fno-stack-protector -fno-builtin \
            -ffreestanding -MMD -MP
  LDFLAGS = -T kernel.ld -e startup_32
else
  # fallback: Docker
  DOCKER_IMAGE = linux-0.01-builder
  DOCKER = docker
  BUILD_IN_DOCKER = $(DOCKER) run --rm -v $(PWD):/kernel -w /kernel \
                     $(DOCKER_IMAGE) make
endif

OBJS = kernel/main.o kernel/sched.o kernel/process.o kernel/sys.o \
       kernel/asm.o kernel/vsprintf.o kernel/panic.o \
       mm/memory.o mm/page.o \
       fs/minix.o fs/buffer.o fs/bitmap.o fs/inode.o fs/file_dev.o fs/namei.o \
       fs/pipe.o \
       drivers/console.o drivers/keyboard.o drivers/hd.o drivers/tty_io.o \
       drivers/serial.o \
       lib/string.o lib/ctype.o lib/malloc.o lib/close.o \
       init/shell.o \
       user/user_data.o

HEAD_OBJ = boot/head.o
SETUP_OBJ = boot/setup.o
BOOT_OBJ = boot/boot.o

all: Image

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble .s files
%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

# Boot sector raw binary
boot/boot: $(BOOT_OBJ)
	$(OBJCOPY) -O binary $(BOOT_OBJ) boot/boot

# Setup binary
boot/setup: $(SETUP_OBJ)
	$(OBJCOPY) -O binary $(SETUP_OBJ) boot/setup

# Link kernel ELF
kernel/system: $(HEAD_OBJ) $(OBJS) user/user.bin
	$(LD) $(LDFLAGS) -o kernel/system $(HEAD_OBJ) $(OBJS)

# User-mode program: linked at 0x200000, embedded into the kernel and
# copied there at runtime by run_user_program().
user/user.bin: user/user.o
	$(LD) -m elf_i386 -Ttext 0x200000 -o user/user.elf user/user.o 2>/dev/null \
	    || $(LD) -Ttext 0x200000 -o user/user.elf user/user.o
	$(OBJCOPY) -O binary user/user.elf user/user.bin

user/user.o: user/user.s
	$(AS) $(ASFLAGS) -o $@ $<

# user_data.c embeds user.bin as a C array (avoids objcopy's NOBITS
# section-layout quirks); rebuild it whenever the user program changes
user/user_data.c: user/user.bin
	python3 -c "\
d = open('user/user.bin','rb').read(); f = open('user/user_data.c','w'); \
f.write('/* Auto-generated */\\n#include <linux/kernel.h>\\n'); \
f.write('const unsigned char user_prog[] = {\\n'); \
[ f.write('    ' + ','.join('0x%02x'%b for b in d[i:i+12]) + ',\\n') for i in range(0,len(d),12) ]; \
f.write('};\\nconst unsigned long user_prog_len = %d;\\n' % len(d)); f.close()"

user/user_data.o: user/user_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Convert kernel to raw binary
kernel/system.bin: kernel/system
	$(OBJCOPY) -O binary kernel/system kernel/system.bin

# Build tool
tools/build: tools/build.c
	gcc -m32 -o $@ $< 2>/dev/null || gcc -o $@ $<

# Disk image (bootable floppy, 1.44MB)
Image: boot/boot boot/setup kernel/system.bin tools/build
	tools/build boot/boot boot/setup kernel/system.bin
	@scripts/pad-floppy.sh Image 2>/dev/null || true

# Bootable ISO (El Torito)
iso: Image
	@scripts/mkiso.sh Image kernel.iso

# --- user programs (execve): make prog NAME=hello -------------------
# user/NAME.c + crt.o + lib.o -> user/NAME.elf -> injected into minix.img
user/crt.o: user/crt.s
	$(AS) $(ASFLAGS) -o $@ $<

user/lib.o: user/lib.c user/lib.h
	$(CC) $(CFLAGS) -c -o $@ $<

user/%.o: user/%.c user/lib.h
	$(CC) $(CFLAGS) -c -o $@ $<

user/%.elf: user/crt.o user/%.o user/lib.o
	$(LD) -m elf_i386 -Ttext 0x200000 -o $@ user/crt.o user/$*.o user/lib.o 2>/dev/null \
	    || $(LD) -Ttext 0x200000 -o $@ user/crt.o user/$*.o user/lib.o

prog: tools/mkminix user/$(NAME).elf
	tools/mkminix minix.img user/$(NAME).elf:$(NAME)

# MINIX v1 test disk image (used with: qemu -hda minix.img)
minix.img: tools/mkminix user/hello.elf
	tools/mkminix minix.img

tools/mkminix: tools/mkminix.c
	$(HOST_CC) -O2 -Wall -o $@ $<

HOST_CC ?= gcc

# Docker build
docker-build:
	$(DOCKER) build -t $(DOCKER_IMAGE) .
	$(DOCKER) run --rm -v $(PWD):/kernel -w /kernel $(DOCKER_IMAGE) \
	    make clean all

# auto header dependencies (-MMD)
-include $(OBJS:.o=.d) $(HEAD_OBJ:.o=.d) $(SETUP_OBJ:.o=.d) $(BOOT_OBJ:.o=.d)

clean:
	rm -f *.d */*.d
	rm -f Image kernel.iso kernel/system kernel/system.bin
	rm -f boot/boot boot/setup
	rm -f $(OBJS) $(HEAD_OBJ) $(SETUP_OBJ) $(BOOT_OBJ)
	rm -f tools/build
	rm -f *~ core .image_floppy_padded
	rm -rf .iso_tmp

run: Image
	qemu-system-i386 -fda Image -m 4M -boot a

run-cd: kernel.iso
	qemu-system-i386 -cdrom kernel.iso -m 4M -boot d

debug: Image
	qemu-system-i386 -fda Image -m 4M -boot a -s -S

.PHONY: all clean run run-cd debug iso docker-build
