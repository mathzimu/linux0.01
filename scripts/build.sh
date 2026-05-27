#!/bin/bash
# Build and run the kernel
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

MODE="${1:-build}"

case "$MODE" in
    build)
        echo "=== Building Minimal Linux 0.01 Kernel ==="
        make clean 2>/dev/null || true
        make
        echo ""
        echo "Build complete."
        echo "  Run:      ./scripts/build.sh run"
        echo "  Run ISO:  ./scripts/build.sh run-cd"
        echo "  Make ISO: ./scripts/build.sh iso"
        ;;
    iso)
        echo "=== Building ISO ==="
        make iso
        ;;
    docker)
        echo "=== Building with Docker ==="
        docker build -t linux-0.01-builder .
        echo ""
        echo "Running build inside Docker..."
        docker run --rm -v "$PROJECT_DIR:/kernel" -w /kernel linux-0.01-builder make clean all
        echo ""
        echo "Build complete."
        echo "  To build ISO: docker run --rm -v $PROJECT_DIR:/kernel -w /kernel linux-0.01-builder make iso"
        ;;
    run)
        if [ ! -f Image ]; then
            echo "Image not found. Run build first."
            exit 1
        fi
        echo "=== Starting QEMU (Ctrl+A X to exit) ==="
        qemu-system-i386 -fda Image -m 4M -boot a
        ;;
    run-cd)
        if [ ! -f kernel.iso ]; then
            echo "kernel.iso not found. Run 'make iso' first."
            exit 1
        fi
        echo "=== Starting QEMU from CD-ROM (Ctrl+A X to exit) ==="
        qemu-system-i386 -cdrom kernel.iso -m 4M -boot d
        ;;
    debug)
        if [ ! -f Image ]; then
            echo "Image not found. Run build first."
            exit 1
        fi
        echo "=== Starting QEMU with GDB server (:1234) ==="
        qemu-system-i386 -fda Image -m 4M -boot a -s -S
        echo "Connect: gdb -ex 'target remote localhost:1234' kernel/system"
        ;;
    clean)
        make clean
        echo "Cleaned."
        ;;
    *)
        echo "Usage: $0 {build|iso|docker|run|run-cd|debug|clean}"
        exit 1
        ;;
esac
