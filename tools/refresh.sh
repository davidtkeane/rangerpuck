#!/usr/bin/env bash
# ============================================================================
#  refresh.sh — re-send everything the board holds in RAM.
#
#  The puck is deliberately dumb: it stores nothing and cannot ask for data, so
#  a reflash or a power cut leaves it with only the clock (which it gets from
#  NTP). This puts the other three screens back.
#
#  Used BOTH by hand and by launchd (com.ranger.puck-ambient, every 10 min), so
#  there is exactly one definition of "refresh everything" to keep correct.
#
#    ./refresh.sh            chatty — for a human at a terminal
#    ./refresh.sh --quiet    timestamped, stdout dropped, STDERR KEPT — for launchd
#
#  WHY STDERR IS KEPT: the scheduled job used to end every command with
#  `>/dev/null 2>&1`, which threw tracebacks away BEFORE launchd could write
#  them to StandardErrorPath. The log sat at 0 bytes for a day while three real
#  crashes went unseen. stdout is noise (weather.py prints a full colour
#  report); stderr is the only thing that ever matters here.
# ============================================================================
set -uo pipefail
E="$HOME/esp32-projects"
CACHE="$HOME/.ranger-memory/config/rangerpuck.ip"
QUIET=0; [ "${1:-}" = "--quiet" ] && QUIET=1

run() {  # run <label> <command...>
  local label="$1"; shift
  if [ "$QUIET" = 1 ]; then
    # stdout dropped, stderr flows through to launchd's log
    if "$@" >/dev/null; then :; else echo "[$(date '+%F %T')] FAILED: $label" >&2; fi
  else
    printf '  %-8s…' "$label"
    if "$@" >/dev/null 2>&1; then echo " ok"; else echo " FAILED"; fi
  fi
}

[ "$QUIET" = 1 ] && echo "[$(date '+%F %T')] ambient refresh" >&2

run weather "$E/2-hutch-watch/weather.py" --puck
run fleet   "$E/1-ranger-puck/tools/fleet-push.sh"
run planes  "$E/3-plane-spotter/spotter.py"

# The board's own status line. Cache FIRST, then mDNS — mDNS resolves but does
# not answer when M3 is on Ethernet and the puck is on Wi-Fi, so trying it first
# just buys a timeout. Same ordering rule as send.sh.
[ "$QUIET" = 1 ] && exit 0
echo
for h in "$(cat "$CACHE" 2>/dev/null)" "rangerpuck.local"; do
  [ -z "$h" ] && continue
  if out=$(curl -s --max-time 3 "http://$h/" 2>/dev/null) && [ -n "$out" ]; then
    printf '%s\n' "$out" | head -3 | sed 's/^/  /'; exit 0
  fi
done
echo "  puck unreachable" >&2
