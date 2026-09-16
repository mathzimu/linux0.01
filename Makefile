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
  ASFLAGS = -32 -Iinclude
  CFLAGS  = -m32 -Wall -O0 -fstrength-reduce -fomit-frame-pointer \
            -nostdinc -Iinclude -fno-stack-protector -fno-builtin \
            -ffreestanding -fno-pic -fno-pie
  LDFLAGS = -m elf_i386 -T kernel.ld -e startup_32 --no-pie
else ifneq ($(shell command -v i386-elf-gcc 2>/dev/null),)
  # macOS with i386-elf-* cross-compiler
  AS      = i386-elf-as
  CC      = i386-elf-gcc
  LD      = i386-elf-ld
  OBJCOPY = i386-elf-objcopy
  ASFLAGS = -Iinclude
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
  ASFLAGS = -Iinclude
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
       kernel/asm.o kernel/vsprintf.o kernel/panic.o kernel/sync.o \
       mm/memory.o mm/page.o mm/memcheck.o \
       fs/minix.o fs/buffer.o fs/bitmap.o fs/inode.o fs/file_dev.o fs/namei.o \
       fs/pipe.o \
       drivers/console.o drivers/keyboard.o drivers/hd.o drivers/tty_io.o \
       drivers/serial.o \
       lib/string.o lib/ctype.o lib/malloc.o lib/close.o \
       init/shell.o \
       user/user_data.o

# User programs are linked at this address; the kernel's ELF loader
# maps each LOAD segment into the process's own address space at its
# link-time vaddr (include/memlayout.h, enforced by the assert in
# user/lib.h).  Since M3 this is a *virtual* address in the per-process
# user window, not a physical one.
USER_PROG_START = 0x08000000

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

# User-mode program: linked at USER_PROG_START, embedded into the kernel
# and copied there at runtime by run_user_program().
user/user.bin: user/user.o
	$(LD) -m elf_i386 -Ttext $(USER_PROG_START) \
	    --defsym __user_prog_start=$(USER_PROG_START) \
	    -o user/user.elf user/user.o 2>/dev/null \
	    || $(LD) -Ttext $(USER_PROG_START) \
	       --defsym __user_prog_start=$(USER_PROG_START) \
	       -o user/user.elf user/user.o
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
#
# Every user object depends on the layout headers: the link address and
# the argv/heap/stack addresses come from include/memlayout.h, and a
# stale object linked at the old address fails at execve time (the loader
# checks the entry point against the user program region).
user/crt.o: user/crt.s include/memlayout.h include/memlayout.inc
	$(AS) $(ASFLAGS) -o $@ $<

user/lib.o: user/lib.c user/lib.h include/memlayout.h
	$(CC) $(CFLAGS) -c -o $@ $<

user/%.o: user/%.c user/lib.h include/memlayout.h
	$(CC) $(CFLAGS) -c -o $@ $<

user/%.elf: user/crt.o user/%.o user/lib.o
	$(LD) -m elf_i386 -Ttext $(USER_PROG_START) \
	    --defsym __user_prog_start=$(USER_PROG_START) \
	    -o $@ user/crt.o user/$*.o user/lib.o 2>/dev/null \
	    || $(LD) -Ttext $(USER_PROG_START) \
	       --defsym __user_prog_start=$(USER_PROG_START) \
	       -o $@ user/crt.o user/$*.o user/lib.o

prog: tools/mkminix user/$(NAME).elf
	tools/mkminix minix.img user/$(NAME).elf:$(NAME)

# The userland that ships in the default image.  tools/mkminix always
# adds hello.txt/readme.txt/big.txt and /bin/hello; these are the
# programs that make the Ring3 shell usable the moment you get there
# (`exec /bin/sh` -> ls, cat, head, tail, wc, cp, mv, ln, grep, touch,
# mkdir, rm and a nested sh all resolve instead of printing
# "sh: /bin/ls: cannot execute").
DEFAULT_USERLAND = ls cat head tail cp mv ln grep touch wc mkdir rm sh
DEFAULT_ELVES    = $(patsubst %,user/%.elf,$(DEFAULT_USERLAND))
# NB: patsubst substitutes only the first '%' in the replacement, so the
# "path:name" spec pairs are built with foreach instead.
DEFAULT_SPECS    = $(foreach p,$(DEFAULT_USERLAND),user/$(p).elf:$(p))

# MINIX v1 test disk image (used with: qemu -hda minix.img).
# Scenario preps call tools/mkminix directly with their own injections and
# keep working unchanged: mkminix only ever adds /bin/hello by default,
# and skips it silently when user/hello.elf is not built yet.
minix.img: tools/mkminix user/hello.elf $(DEFAULT_ELVES)
	tools/mkminix minix.img $(DEFAULT_SPECS)

# --- one image that boots the whole system from the first IDE disk -----
#
# `make run` needs two files (Image + minix.img).  `make disk` produces a
# single self-booting image instead:
#
#   LBA 0              boot sector       (from Image)
#   LBA 1..4           setup
#   LBA 5..            kernel image
#   fs_base..          MINIX v1 filesystem + raw swap (disk-fs.img)
#
# fs_base is chosen by tools/mkdisk - the first 1024-byte boundary behind
# the kernel, padded out to a whole cylinder - and written into the boot
# sector of the image it is building.  boot/boot.s forwards it to the
# kernel through the boot parameter block, so no offset is hard-coded on
# both sides and the image and the kernel cannot drift apart.  See
# docs/roadmap.md ("单文件自启动镜像") for why an explicit base LBA was
# chosen over a partition table.
#
# The embedded filesystem is deliberately NOT minix.img: the regression
# scenarios rebuild minix.img with their own /bin contents, and `make
# disk` has to keep embedding the default userland whatever the last test
# left behind.
DISK_IMG = linux.img
DISK_FS  = disk-fs.img

tools/mkdisk: tools/mkdisk.c
	$(HOST_CC) -O2 -Wall -o $@ $<

$(DISK_FS): tools/mkminix user/hello.elf $(DEFAULT_ELVES)
	tools/mkminix $(DISK_FS) $(DEFAULT_SPECS)

disk: $(DISK_IMG)

$(DISK_IMG): Image $(DISK_FS) tools/mkdisk
	tools/mkdisk $(DISK_IMG) Image $(DISK_FS)

# --- VMware / VirtualBox: the same one-file system as a VMDK -----------
#
# VMware and VirtualBox cannot attach a raw image, so wrap linux.img in a
# VMDK.  It has to be an IDE adapter (the kernel's disk driver is PIIX
# PIO-only: IRQ14, ports 0x1F0) and qemu-img's descriptor carries the
# geometry the kernel does its own LBA->CHS maths with - 7 cylinders, 16
# heads, 63 sectors, matching tools/mkdisk's cylinder padding.  Neither
# hypervisor needs a partition table: the boot sector is what gets control
# and the kernel finds the filesystem through the base LBA in it.
#
# qemu-img lives in the build container:
#   docker run --rm -v $(PWD):/kernel -w /kernel linux-0.01-builder make vmdk
VMDK_IMG = linux.vmdk
VMDK_FLAT = linux-flat.vmdk

vmdk: $(VMDK_IMG)

$(VMDK_IMG): $(DISK_IMG)
	qemu-img convert -f raw -O vmdk -o adapter_type=ide $< $@

# monolithicFlat instead of the default monolithicSparse: two files (a small
# text descriptor plus a raw data extent), which some VMware versions and
# older VirtualBox builds prefer.  Both were checked to boot.
$(VMDK_FLAT): $(DISK_IMG)
	qemu-img convert -f raw -O vmdk -o adapter_type=ide,subformat=monolithicFlat $< $@

# One-shot regression suite (see scripts/regress.sh): builds a clean
# MINIX disk per scenario, boots QEMU, and asserts the serial output.
# The full run takes ~10 minutes under TCG (no KVM) on a dev machine -
# most of it spent typing keys at the harness's safe 0.5s/character - so
# `test-fast` skips the three heavy cases (autosync, oom, evict) for PR
# runs.  See scripts/regress.sh for TEST_SKIP_HEAVY / TEST_TYPE_DELAY.
test: Image
	scripts/regress.sh

test-fast: Image
	TEST_SKIP_HEAVY=1 scripts/regress.sh

# Static memory-map verification.  Needs no compiler, so it can run
# before (or without) a build; see scripts/check-layout.py.
check-layout:
	python3 scripts/check-layout.py --kernel kernel/system

# Documentation must not describe a memory map the kernel no longer has.
# check-layout.py guards "code vs layout"; this guards "docs vs layout"
# (M3 moved the user address space and several tutorial chapters had not
# caught up).  See scripts/check-docs.py.
check-docs:
	python3 scripts/check-docs.py

# Does check-docs actually fail when it should?  Eight probes.
check-docs-selftest:
	bash scripts/check-docs-selftest.sh

# Everything that can be checked without booting QEMU.
check: check-layout check-docs check-docs-selftest

.PHONY: check check-layout check-docs check-docs-selftest

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
	rm -f linux.img disk-fs.img linux.vmdk linux-flat.vmdk linux-flat-flat.vmdk
	rm -f system system.bin
	rm -f boot/boot boot/setup
	rm -f $(OBJS) $(HEAD_OBJ) $(SETUP_OBJ) $(BOOT_OBJ)
	rm -f tools/build tools/mkdisk
	rm -f user/*.o user/*.elf user/*.bin user/user_data.c
	rm -f *~ core .image_floppy_padded
	rm -rf .iso_tmp

# minix.img is where the MINIX filesystem lives: attach it, or the guest
# comes up with nothing to list, read or exec.  (Booting without it no
# longer resets in a loop - the kernel warns "no root filesystem found"
# and reaches the shell, see scenario 32 - but there is nothing useful to
# do once you are there.)
run: Image minix.img
	qemu-system-i386 -fda Image -m 16M -boot a -hda minix.img

# The ISO carries the kernel only: the MINIX filesystem lives in
# minix.img, so it has to be attached as well or the guest comes up with
# nothing to list, read or exec.  Keep this in step with the quick start.
run-cd: kernel.iso minix.img
	qemu-system-i386 -cdrom kernel.iso -m 16M -boot d -hda minix.img

debug: Image minix.img
	qemu-system-i386 -fda Image -m 16M -boot a -hda minix.img -s -S

# The single-image boot: the whole system - kernel and filesystem - lives
# on the first IDE disk, so this is the only file to attach.  QEMU would
# pick the disk without '-boot c' (there is no other medium), but say it
# anyway: it is the command the README and docs/roadmap.md show.
run-disk: $(DISK_IMG)
	qemu-system-i386 -hda $(DISK_IMG) -m 16M -boot c

.PHONY: all clean run run-cd debug run-disk disk vmdk iso docker-build test test-fast