#!/bin/bash
# Minimal Linux 0.01 Kernel - Build Environment Setup
set -e

echo "=== Minimal Linux 0.01 Kernel - Build Environment ==="
echo ""

OS="$(uname -s)"
MISSING=0

case "$OS" in
    Linux)
        echo "Detected: Linux"
        echo ""
        echo "Installing required packages..."
        if command -v apt-get &>/dev/null; then
            sudo apt-get update
            sudo apt-get install -y build-essential gcc-multilib qemu-system-x86 xorriso
        elif command -v pacman &>/dev/null; then
            sudo pacman -S --needed base-devel qemu xorriso
        elif command -v dnf &>/dev/null; then
            sudo dnf install -y gcc glibc-devel.i686 qemu-system-x86 xorriso
        else
            echo "Unsupported package manager. Install manually:"
            echo "  gcc, make, binutils, qemu-system-i386, xorriso"
        fi
        ;;
    Darwin)
        echo "Detected: macOS"
        echo ""
        echo "Recommended: Use Docker for building"
        echo "  ./scripts/build.sh docker"
        echo ""
        echo "Or install cross-compiler:"
        echo "  brew install xpack-i386-elf-gcc"
        echo "  Or use Docker: docker build -t linux-0.01-builder ."
        echo ""
        echo "For running: brew install qemu"
        if ! command -v qemu-system-i386 &>/dev/null; then
            echo ""
            echo "Install QEMU: brew install qemu"
        fi
        ;;
    *)
        echo "Unsupported OS: $OS"
        exit 1
        ;;
esac

echo ""
echo "=== Verification ==="
for tool in make as gcc ld objcopy qemu-system-i386; do
    if command -v $tool &>/dev/null; then
        echo "  $tool: OK"
    else
        echo "  $tool: MISSING"
        MISSING=1
    fi
done

if [ "$MISSING" -eq 0 ]; then
    echo ""
    echo "=== Ready to build ==="
fi
