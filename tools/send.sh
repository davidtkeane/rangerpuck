#!/usr/bin/env bash
# Push a state to the RangerPuck.
#
#   ./tools/send.sh APPROVE "needs a yes" "0m 31s"
#   ./tools/send.sh THINKING "reading files"
#   ./tools/send.sh DONE
#
# States: IDLE THINKING APPROVE RUNNING DONE ERROR
#
# HOW IT FINDS THE BOARD — and why it is built this way:
# mDNS (rangerpuck.local) is unreliable here because M3 is on Ethernet and the
# board is on Wi-Fi; consumer routers often will not forward multicast between
# the two. But hard-coding an IP is what rotted the fleet config — DHCP moves
# them. So: try the name, fall back to the last IP that worked, and cache
# whatever succeeds. Self-healing, no hard-coded address in the source.
set -uo pipefail
CACHE="$HOME/.ranger-memory/config/rangerpuck.ip"
NAME="${PUCK_HOST:-rangerpuck.local}"
# ---------------------------------------------------------------------------
# PORTABILITY: this runs on macOS (M3/M4/M5) and on Linux (the Kali box), and
# three things differ. Wrap them once here rather than sprinkling `uname` checks
# through the logic.
#   local IPv4 : macOS `ipconfig getifaddr en0`  vs  Linux `ip -4 addr`
#   file mtime : macOS `stat -f %m`              vs  Linux `stat -c %Y`
# ---------------------------------------------------------------------------
my_ipv4() {
  if command -v ipconfig >/dev/null 2>&1; then          # macOS
    ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en6 2>/dev/null
  elif command -v ip >/dev/null 2>&1; then              # Linux
    ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1
  else
    hostname -I 2>/dev/null | awk '{print $1}'
  fi
}
mtime() {  # mtime <file> -> epoch seconds, 0 if missing
  stat -f %m "$1" 2>/dev/null || stat -c %Y "$1" 2>/dev/null || echo 0
}

STATE="${1:-IDLE}"; L1="${2:-}"; L2="${3:-}"
# WHO is speaking — any agent can drive this board. Set PUCK_WHO in the caller's
# environment (CLAUDE / GEMINI / OLLAMA / QWEN / WEATHER) or pass it as $4.
WHO="${4:-${PUCK_WHO:-}}"
# Build the JSON with jq, not printf. printf '%s' pasted the strings in RAW, so a
# single quote, backslash or newline anywhere in line1/line2 produced invalid
# JSON: the board answered 400, try() swallowed it with >/dev/null, and the state
# VANISHED with no error anywhere. The hook feeds this arbitrary text — tool
# names, notification messages, file paths — so it was a matter of time.
#   ./send.sh ERROR 'cannot open "notes.txt"'     <- used to be silently dropped
if command -v jq >/dev/null 2>&1; then
  payload=$(jq -cn --arg s "$STATE" --arg a "$L1" --arg b "$L2" --arg w "$WHO" \
            '{state:$s,line1:$a,line2:$b,who:$w}')
else
  # jq absent: escape by hand rather than emit JSON that will be rejected.
  esc() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr -d '\n\r\t'; }
  payload=$(printf '{"state":"%s","line1":"%s","line2":"%s","who":"%s"}' \
            "$(esc "$STATE")" "$(esc "$L1")" "$(esc "$L2")" "$(esc "$WHO")")
fi

try() {  # try <host> -> 0 if it accepted the state
  curl -s --max-time 1 -X POST -H 'Content-Type: application/json' \
       -d "$payload" "http://$1/state" >/dev/null 2>&1
}

# 1. LAST KNOWN GOOD IP FIRST.
#    mDNS was tried first originally, which cost TWO SECONDS on every single
#    call here: rangerpuck.local RESOLVES but does not answer (the router will
#    not bridge multicast between Ethernet and Wi-Fi), so curl waited out the
#    full timeout before falling back to the address that works. With a hook
#    firing on every prompt that delay is the difference between a status light
#    and a status light you have stopped believing.
#    The cache is the fast path. mDNS is the fallback for when the cache is
#    stale or missing.
if [ -f "$CACHE" ]; then
  ip=$(cat "$CACHE")
  if try "$ip"; then echo "→ $STATE ${L1} ${L2}"; exit 0; fi
fi

# 2. the mDNS name, for a first run or after the board moves
if try "$NAME"; then echo "→ $STATE ${L1} ${L2}  (via mDNS)"; exit 0; fi

# 3. sweep the subnet for something that answers as a RangerPuck.
#    Was `seq 2 60` x `--max-time 1` = up to 58 SECONDS of blocking, and it never
#    looked past .60 so a later DHCP lease could not be found at all. Now the
#    whole usable range is probed in PARALLEL BATCHES with a short timeout, so the
#    worst case is a couple of seconds instead of a minute. That matters because a
#    hook fires this on every tool call: when the board was off, the old version
#    stacked up minute-long background sweeps.
#    The stamp file stops a dead board being swept for on every single call.
SWEPT="$HOME/.ranger-memory/config/.puck-swept"
if [ -n "$base" ]; then
  if [ -f "$SWEPT" ]; then
    age=$(( $(date +%s) - $(mtime "$SWEPT") ))
    if [ "$age" -lt 60 ]; then
      echo "puck unreachable — swept ${age}s ago, not sweeping again yet" >&2; exit 1
    fi
  fi
  mkdir -p "$(dirname "$CACHE")"; : > "$SWEPT"
  found=""
  for lo in 2 66 130 194; do
    hi=$(( lo + 63 )); [ "$hi" -gt 254 ] && hi=254
    hits=$(
      for i in $(seq "$lo" "$hi"); do
        ( curl -s --max-time 1 "http://$base.$i/" 2>/dev/null \
            | grep -q RangerPuck && printf '%s\n' "$base.$i" ) &
      done
      wait
    )
    found=$(printf '%s' "$hits" | head -1)
    [ -n "$found" ] && break
  done
  if [ -n "$found" ]; then
    printf '%s\n' "$found" > "$CACHE"
    try "$found" && { echo "→ $STATE ${L1} ${L2}  (found at $found, cached)"; exit 0; }
  fi
fi
echo "puck unreachable — is it powered and on Wi-Fi?" >&2
exit 1
