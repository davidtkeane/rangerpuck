#!/usr/bin/env bash
# Switch the puck's mascot.  ./tools/mascot.sh        toggle
#                            ./tools/mascot.sh 0      Ranger helmet
#                            ./tools/mascot.sh 1      cat
CACHE="$HOME/.ranger-memory/config/rangerpuck.ip"
for h in "${PUCK_HOST:-rangerpuck.local}" "$(cat "$CACHE" 2>/dev/null)"; do
  [ -z "$h" ] && continue
  if [ -n "${1:-}" ]; then
    r=$(curl -s --max-time 3 -X POST -H 'Content-Type: application/json' -d "{\"mascot\":$1}" "http://$h/mascot")
  else
    r=$(curl -s --max-time 3 "http://$h/mascot")
  fi
  [ -n "$r" ] && { echo "  mascot: ${r%$'\n'}"; exit 0; }
done
echo "  puck unreachable" >&2; exit 1
