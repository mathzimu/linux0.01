#!/bin/bash
# Pad Image to full 1.44MB floppy size
set -e

IMAGE="${1:-Image}"
FLOPPY_SIZE=$((1440 * 1024))

if [ ! -f "$IMAGE" ]; then
    exit 0
fi

SIZE=$(stat -f%z "$IMAGE" 2>/dev/null || stat -c%s "$IMAGE" 2>/dev/null || echo 0)
if [ "$SIZE" -ge "$FLOPPY_SIZE" ]; then
    exit 0
fi

dd if=/dev/zero bs=1 count=$((FLOPPY_SIZE - SIZE)) >> "$IMAGE" 2>/dev/null
if command -v truncate &>/dev/null; then
    truncate -s "$FLOPPY_SIZE" "$IMAGE" 2>/dev/null || true
fi
