#!/usr/bin/env bash
# puck.sh — RangerPuck flash manager (backup / restore / flash / monitor / info).
# A friendly menu around esptool + the existing tools. Run: ./tools/puck.sh   (or: ./tools/puck.sh backup)
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BDIR="$HERE/backups"; mkdir -p "$BDIR"

# --- locate the board + esptool ---
find_port() {
  local p="${PORT:-}"
  [ -z "$p" ] && p="$(arduino-cli board list 2>/dev/null | awk '/ESP32 Family/{print $1; exit}')"
  [ -z "$p" ] && p="$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null | head -1)"
  echo "$p"
}
find_esptool() {
  local e; e="$(ls -t "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)"
  [ -z "$e" ] && e="$(command -v esptool.py || command -v esptool)"
  echo "$e"
}
PORT="$(find_port)"; ESPTOOL="$(find_esptool)"
IP="$(cat "$HOME/.ranger-memory/config/rangerpuck.ip" 2>/dev/null || echo 192.168.1.12)"
need() { [ -n "$PORT" ] || { echo "✗ no ESP32 found on USB (plug it in)"; return 1; }; [ -n "$ESPTOOL" ] || { echo "✗ esptool not found"; return 1; }; }

# size in hex from the chip, default 8MB
flash_hex() {
  local mb; mb="$("$ESPTOOL" --port "$PORT" flash-id 2>/dev/null | awk -F': *' '/Detected flash size/{print $2}' | tr -dc '0-9')"
  case "${mb:-8}" in 4) echo 0x400000;; 8) echo 0x800000;; 16) echo 0x1000000;; *) echo $((mb*1024*1024));; esac
}

cmd_info()   { need || return; "$ESPTOOL" --port "$PORT" flash-id; }
cmd_backup() { need || return; local sz f; sz="$(flash_hex)"; f="$BDIR/rangerpuck-flash-FULL-$(date +%F-%H%M).bin"
  echo "── backing up $sz bytes from $PORT ──"; "$ESPTOOL" --port "$PORT" --baud 921600 read-flash 0 "$sz" "$f"
  echo "✅ $f  ($(stat -f%z "$f") bytes)"; shasum -a256 "$f"; }
cmd_list()   { ls -lt "$BDIR"/rangerpuck-flash-*.bin 2>/dev/null || echo "no backups yet"; }
cmd_restore(){ need || return; local img="${1:-$(ls -t "$BDIR"/rangerpuck-flash-FULL-*.bin 2>/dev/null | head -1)}"
  [ -f "$img" ] || { echo "no image (pass a path)"; return 1; }
  echo "⚠️  WRITE $img -> ENTIRE flash on $PORT (firmware+config+Wi-Fi creds)"; read -r -p "type YES: " ok
  [ "$ok" = YES ] || { echo "aborted"; return 1; }
  "$ESPTOOL" --port "$PORT" --baud 921600 --after hard-reset write-flash 0x0 "$img"; echo "✅ restored, rebooting"; }
cmd_flash()  { bash "$HERE/tools/flash.sh"; }
cmd_monitor(){ bash "$HERE/tools/monitor.sh" "$PORT"; }
cmd_status() { curl -s -m4 "http://$IP/" || echo "puck not answering at $IP"; }
cmd_erase()  { need || return; echo "⚠️  ERASE ALL FLASH on $PORT (back up first!)"; read -r -p "type ERASE: " ok
  [ "$ok" = ERASE ] || { echo "aborted"; return 1; }; "$ESPTOOL" --port "$PORT" erase-flash; }

menu() {
  while true; do
    echo; echo "┌─ RangerPuck flasher ──────────────────────────"
    echo "│ board: ${PORT:-NOT FOUND}   puck: http://$IP"
    echo "├───────────────────────────────────────────────"
    echo "│ 1) info        chip + flash size"
    echo "│ 2) backup      full flash -> timestamped .bin  (do before experiments!)"
    echo "│ 3) list        show backups"
    echo "│ 4) restore     roll back to a backup"
    echo "│ 5) flash       compile + upload the RangerPuck sketch"
    echo "│ 6) monitor     serial output (ctrl-c to stop)"
    echo "│ 7) status      puck HTTP status"
    echo "│ 8) erase       wipe flash (danger)"
    echo "│ q) quit"
    echo "└───────────────────────────────────────────────"
    read -r -p "> " c
    case "$c" in
      1|info) cmd_info;; 2|backup) cmd_backup;; 3|list) cmd_list;; 4|restore) cmd_restore;;
      5|flash) cmd_flash;; 6|monitor) cmd_monitor;; 7|status) cmd_status;; 8|erase) cmd_erase;;
      q|quit|exit) break;; *) echo "?";;
    esac
  done
}

# non-interactive: ./tools/puck.sh backup   |   interactive menu if no arg
if [ $# -gt 0 ]; then cmd="$1"; shift; "cmd_$cmd" "$@"; else menu; fi
