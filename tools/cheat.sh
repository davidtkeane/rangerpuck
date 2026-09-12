#!/usr/bin/env bash
# Pretty-print the RangerPuck cheat sheet. Deliberately uses no external
# renderer — glow and bat both open pagers that hang in a non-tty context.
F="$(cd "$(dirname "$0")/.." && pwd)/CHEATSHEET.md"
[ -f "$F" ] || { echo "cheat sheet missing: $F"; exit 1; }
B=$'\033[1m'; C=$'\033[1;36m'; Y=$'\033[1;33m'; D=$'\033[90m'; N=$'\033[0m'
awk -v B="$B" -v C="$C" -v Y="$Y" -v D="$D" -v N="$N" '
  BEGIN { print "" }
  /^# /      { print C "  " substr($0,3) N; next }
  /^## /     { print ""; print Y "  ── " substr($0,4) N; next }
  /^\|[ -]*-/ { next }
  /^\|/      { line=$0; gsub(/^\| */,"",line); gsub(/ *\| *$/,"",line);
               gsub(/ *\| */, D" · "N, line); gsub(/`/,"",line); gsub(/\*\*/,"",line);
               print "    " line; next }
  /^```/     { next }
  /^$/       { print ""; next }
  { line=$0; gsub(/`/,"",line); gsub(/\*\*/,"",line); print "    " line }
  END { print "" }
' "$F"
echo "${D}    file: $F${N}"
echo
