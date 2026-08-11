#!/bin/bash
# VelaSense Flash Script
# Usage: ./tools/flash.sh [port] [board]
# Example: ./tools/flash.sh /dev/ttyUSB0 sf32lb52_devkit_lcd

set -e

PORT="${1:-/dev/ttyUSB0}"
BOARD="${2:-sf32lb52_devkit_lcd}"
BUILD_DIR="cmake_out/${BOARD}"
BIN_FILE="${BUILD_DIR}/nuttx.bin"
BAUD=1000000

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WORKSPACE_DIR="$(dirname "$PROJECT_DIR")"

cd "$WORKSPACE_DIR"

echo "========================================="
echo "  VelaSense Flash Script"
echo "========================================="
echo "  Board:  ${BOARD}"
echo "  Port:   ${PORT}"
echo "  Binary: ${BIN_FILE}"
echo "========================================="

# Check if binary exists
if [ ! -f "$BIN_FILE" ]; then
  echo "ERROR: Binary not found: ${BIN_FILE}"
  echo "Run build first:"
  echo "  cmake -B ${BUILD_DIR} -S nuttx -GNinja \\"
  echo "    -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/${BOARD}/configs/nsh"
  echo "  cmake --build ${BUILD_DIR}"
  exit 1
fi

# Check if sftool is installed
if ! command -v sftool &> /dev/null; then
  echo "ERROR: sftool not found. Install it:"
  echo "  cargo install sftool"
  echo "  or download from: https://github.com/OpenSiFli/sftool/releases"
  exit 1
fi

# Flash
echo ""
echo "Flashing..."
sftool -c SF32LB52 -p "$PORT" -b "$BAUD" \
  --before default_reset --after soft_reset \
  write_flash "${BIN_FILE}@0x12010000"

echo ""
echo "Flash complete!"
echo "Connect to console:"
echo "  picocom -b ${BAUD} --noreset --lower-rts --lower-dtr ${PORT}"
