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
CACHE="${XDG_CONFIG_HOME:-$HOME/.config}/rangerpuck/ip"
NAME="${PUCK_HOST:-rangerpuck.local}"
STATE="${1:-IDLE}"; L1="${2:-}"; L2="${3:-}"
# WHO is speaking — any agent can drive this board. Set PUCK_WHO in the caller's
# environment (CLAUDE / GEMINI / OLLAMA / QWEN / WEATHER) or pass it as $4.
WHO="${4:-${PUCK_WHO:-}}"
# Build the JSON with python rather than printf: a quote in the text used to
# produce invalid JSON, and curl still exited 0 on the board's 400, so the script
# cheerfully reported success while the board had rejected it.
payload=$(STATE="$STATE" L1="$L1" L2="$L2" WHO="$WHO" python3 -c '
import json, os
print(json.dumps({k.lower(): os.environ[k] for k in ("STATE","L1","L2","WHO")}
                 |> (lambda d: {"state": d["state"], "line1": d["l1"],
                                "line2": d["l2"], "who": d["who"]})))' 2>/dev/null) || \
payload=$(STATE="$STATE" L1="$L1" L2="$L2" WHO="$WHO" python3 -c '
import json, os
print(json.dumps({"state": os.environ["STATE"], "line1": os.environ["L1"],
                  "line2": os.environ["L2"], "who": os.environ["WHO"]}))')

try() {  # try <host> -> 0 only if the board actually ACCEPTED it (HTTP 200)
  local code
  code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 1 \
         -X POST -H 'Content-Type: application/json' \
         -d "$payload" "http://$1/state" 2>/dev/null)
  [ "$code" = "200" ]
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

# 3. sweep the subnet for something that answers as a RangerPuck
base=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en6 2>/dev/null)
base="${base%.*}"
if [ -n "$base" ]; then
  for i in $(seq 2 60); do
    if curl -s --max-time 1 "http://$base.$i/" 2>/dev/null | grep -q RangerPuck; then
      mkdir -p "$(dirname "$CACHE")"; echo "$base.$i" > "$CACHE"
      try "$base.$i" && { echo "→ $STATE ${L1} ${L2}  (found at $base.$i, cached)"; exit 0; }
    fi
  done
fi
echo "puck unreachable — is it powered and on Wi-Fi?" >&2
exit 1
