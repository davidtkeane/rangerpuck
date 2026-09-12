#!/usr/bin/env bash
# Watch the board's serial output. Ctrl-C to stop.
# find the board rather than assuming a device name that is only right on one machine
PORT="${1:-$(arduino-cli board list 2>/dev/null | awk '/ESP32/{print $1; exit}')}"
[ -z "$PORT" ] && { echo "no ESP32 found on USB — pass the port: $0 /dev/cu.usbmodemXXXX"; exit 1; }
echo "── monitoring $PORT at 115200 (ctrl-c to stop) ──"
arduino-cli monitor -p "$PORT" -c baudrate=115200
