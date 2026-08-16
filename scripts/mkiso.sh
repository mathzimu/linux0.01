#!/bin/bash
# Create a bootable ISO from the floppy Image
# Usage: ./scripts/mkiso.sh [input:Image] [output:kernel.iso]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

INPUT="${1:-Image}"
OUTPUT="${2:-kernel.iso}"

if [ ! -f "$INPUT" ]; then
    echo "Error: $INPUT not found. Run 'make' first."
    exit 1
fi

IMAGE_SIZE=$(stat -f%z "$INPUT" 2>/dev/null || stat -c%s "$INPUT" 2>/dev/null)
echo "Input: $INPUT ($IMAGE_SIZE bytes)"

# --------------------
# 1. Pad Image to full 1.44MB floppy size
# --------------------
FLOPPY_SIZE=$((1440 * 1024))
PADDED=".image_floppy_padded"
cp "$INPUT" "$PADDED"
if [ "$IMAGE_SIZE" -lt "$FLOPPY_SIZE" ]; then
    dd if=/dev/zero bs=1 count=$((FLOPPY_SIZE - IMAGE_SIZE)) >> "$PADDED" 2>/dev/null
    truncate -s "$FLOPPY_SIZE" "$PADDED" 2>/dev/null || true
    echo "Padded to $FLOPPY_SIZE bytes (1.44MB floppy)"
fi

# --------------------
# 2. Try all available ISO tools in order
# --------------------
ISO_CREATED=0

make_iso_xorriso() {
    echo "Using: xorriso"
    xorriso -as mkisofs \
        -o "$OUTPUT" \
        -b "$PADDED" \
        -V "LINUX001" \
        . 2>/dev/null
}

make_iso_genisoimage() {
    echo "Using: genisoimage"
    mkdir -p .iso_tmp/boot
    cp "$PADDED" .iso_tmp/boot/floppy.img
    genisoimage \
        -o "$OUTPUT" \
        -b boot/floppy.img \
        -V "LINUX001" \
        .iso_tmp 2>/dev/null
    RC=$?
    rm -rf .iso_tmp
    return $RC
}

make_iso_mkisofs() {
    echo "Using: mkisofs"
    mkdir -p .iso_tmp/boot
    cp "$PADDED" .iso_tmp/boot/floppy.img
    mkisofs \
        -o "$OUTPUT" \
        -b boot/floppy.img \
        -V "LINUX001" \
        .iso_tmp 2>/dev/null
    RC=$?
    rm -rf .iso_tmp
    return $RC
}

# Try tools in order of preference
if command -v xorriso &>/dev/null; then
    make_iso_xorriso && ISO_CREATED=1
elif command -v genisoimage &>/dev/null; then
    make_iso_genisoimage && ISO_CREATED=1
elif command -v mkisofs &>/dev/null; then
    make_iso_mkisofs && ISO_CREATED=1
fi

rm -f "$PADDED"

if [ "$ISO_CREATED" -eq 1 ]; then
    ISO_SIZE=$(stat -f%z "$OUTPUT" 2>/dev/null || stat -c%s "$OUTPUT" 2>/dev/null)
    echo "ISO created: $OUTPUT ($ISO_SIZE bytes)"
    echo "Run with: qemu-system-i386 -cdrom $OUTPUT -boot d"
    exit 0
else
    echo "============================ WARNING ============================"
    echo "No ISO creation tool found."
    echo ""
    echo "Install one of:"
    echo "  macOS: brew install xorriso"
    echo "  Linux: sudo apt install xorriso     (or genisoimage)"
    echo ""
    echo "The floppy Image is ready at: $INPUT"
    echo "Use: qemu-system-i386 -fda $INPUT -m 4M -boot a"
    echo "================================================================"
    exit 1
fi
