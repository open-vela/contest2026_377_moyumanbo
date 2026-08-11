#!/bin/bash
# VelaSense Serial Monitor
# Usage: ./tools/serial_monitor.sh [port] [baud]

set -e

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-1000000}"

echo "========================================="
echo "  VelaSense Serial Monitor"
echo "========================================="
echo "  Port: ${PORT}"
echo "  Baud: ${BAUD}"
echo "  Press Ctrl+A Ctrl+X to exit picocom"
echo "========================================="

picocom -b "$BAUD" --noreset --lower-rts --lower-dtr "$PORT"
