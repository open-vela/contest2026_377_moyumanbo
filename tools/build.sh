#!/bin/bash
# VelaSense Build Script
# Usage: ./tools/build.sh [board] [extra_args]
# Example: ./tools/build.sh sf32lb52_devkit_lcd
# Example: ./tools/build.sh lckfb_huangshan_pi --cmake

set -e

BOARD="${1:-sf32lb52_devkit_lcd}"
shift
EXTRA_ARGS="$@"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WORKSPACE_DIR="$(dirname "$PROJECT_DIR")"
BOARD_CONFIG="../vendor/sifli/boards/sf32lb52/${BOARD}/configs/nsh"
BUILD_DIR="cmake_out/${BOARD}"

cd "$WORKSPACE_DIR"

echo "========================================="
echo "  VelaSense Build Script"
echo "========================================="
echo "  Board:     ${BOARD}"
echo "  Config:    ${BOARD_CONFIG}"
echo "  Build Dir: ${BUILD_DIR}"
echo "  Workspace: ${WORKSPACE_DIR}"
echo "========================================="

# Check board config exists
if [ ! -d "$BOARD_CONFIG" ]; then
  echo "ERROR: Board config not found: ${BOARD_CONFIG}"
  echo "Available boards:"
  ls vendor/sifli/boards/sf32lb52/*/configs/nsh/defconfig 2>/dev/null | \
    sed 's|.*boards/sf32lb52/||;s|/configs/nsh/defconfig||'
  exit 1
fi

# Clean build if requested
if [ "$EXTRA_ARGS" = "clean" ]; then
  echo "Cleaning build directory..."
  rm -rf "$BUILD_DIR"
  exit 0
fi

# Build
echo ""
echo "Configuring..."
cmake -B "$BUILD_DIR" -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG="$BOARD_CONFIG" \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations" \
  $EXTRA_ARGS

echo ""
echo "Building..."
cmake --build "$BUILD_DIR"

echo ""
echo "========================================="
echo "  Build complete!"
echo "  Binary: ${BUILD_DIR}/nuttx.bin"
echo "========================================="
echo ""
echo "Flash with:"
echo "  ./tools/flash.sh /dev/ttyUSB0 ${BOARD}"
