#!/usr/bin/env bash
# Watch the board's serial output. Ctrl-C to stop.
PORT="${1:-/dev/cu.usbmodem211401}"
echo "── monitoring $PORT at 115200 (ctrl-c to stop) ──"
arduino-cli monitor -p "$PORT" -c baudrate=115200
