#!/usr/bin/env bash
# Flip the puck between landscape and portrait. It remembers across reboots.
#   ./tools/rotate.sh            cycle to the next orientation
#   ./tools/rotate.sh 1          landscape   (320x172)
#   ./tools/rotate.sh 0          portrait    (172x320)
#   ./tools/rotate.sh 3          landscape, upside down (cable on the other side)
CACHE="${XDG_CONFIG_HOME:-$HOME/.config}/rangerpuck/ip"
HOST="${PUCK_HOST:-rangerpuck.local}"
reach() { curl -s --max-time 3 "$@" ; }
for h in "$HOST" "$(cat "$CACHE" 2>/dev/null)"; do
  [ -z "$h" ] && continue
  if [ -n "${1:-}" ]; then
    r=$(reach -X POST -H 'Content-Type: application/json' -d "{\"rotation\":$1}" "http://$h/rotate")
  else
    r=$(reach "http://$h/rotate")
  fi
  [ -n "$r" ] && { echo "  ${r%$'\n'}  (0/2 portrait, 1/3 landscape)"; exit 0; }
done
echo "  puck unreachable" >&2; exit 1
