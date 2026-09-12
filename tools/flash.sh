#!/usr/bin/env bash
# Compile and upload RangerPuck. Usage: ./tools/flash.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-$(arduino-cli board list | awk '/ESP32 Family/{print $1; exit}')}"
[ -z "$PORT" ] && { echo "no ESP32 found on USB"; exit 1; }
echo "── compiling ──"
arduino-cli compile --fqbn esp32:esp32:esp32c6 "$HERE/RangerPuck"
echo "── uploading to $PORT ──"
arduino-cli upload -p "$PORT" --fqbn esp32:esp32:esp32c6 "$HERE/RangerPuck"
echo "✅ done"
