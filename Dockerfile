FROM ubuntu:22.04 AS builder

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    gcc-multilib \
    g++-multilib \
    make \
    xorriso \
    qemu-system-x86 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /kernel

COPY . .

RUN make clean && make && make iso

CMD ["qemu-system-i386", "-cdrom", "kernel.iso", "-m", "4M", "-boot", "d"]
