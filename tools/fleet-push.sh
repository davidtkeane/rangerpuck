#!/usr/bin/env bash
# ============================================================================
#  fleet-push.sh — gather fleet health and push it to the RangerPuck.
#
#  Reads ~/.ranger-memory/config/fleet.conf (the registry, not a hard-coded
#  list) so adding a machine there makes it appear on the puck automatically.
#
#  Machines are probed IN PARALLEL with short timeouts — a sleeping M4 must
#  not hold the whole thing up.
#
#  Run: ./tools/fleet-push.sh          (or from cron every 5 minutes)
# ============================================================================
set -uo pipefail
CONF="$HOME/.ranger-memory/config/fleet.conf"
SEND_DIR="$(cd "$(dirname "$0")" && pwd)"
MEM="$HOME/.ranger-memory/databases/ranger_memories.db"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

self=$(scutil --get LocalHostName 2>/dev/null | grep -oE 'M[0-9]' | head -1)
self=$(echo "${self:-M3}" | tr 'A-Z' 'a-z')

probe() {   # probe <name> <alias>
  local name="$1" alias="$2" up=0 detail=""
  if [ "$alias" = "$self" ]; then
    up=1
    local n; n=$(sqlite3 "$MEM" "SELECT COUNT(*) FROM memories;" 2>/dev/null)
    detail="$(printf "%'d" "${n:-0}" 2>/dev/null || echo "${n:-0}") mem"
  else
    local n
    # A sleeping Mac takes a moment to wake its network stack. Declaring it
    # offline on one impatient probe puts a red dot on the wall for ten minutes
    # over a machine that is perfectly fine — so a failure gets ONE retry with a
    # longer timeout before we believe it. The happy path is unaffected: a
    # machine that is awake answers in well under a second.
    q='sqlite3 ~/.ranger-memory/databases/ranger_memories.db "SELECT COUNT(*) FROM memories;" 2>/dev/null'
    # Three outcomes, not two: "answered straight away", "needed a moment to
    # wake" and "did not answer at all". Collapsing the middle into red puts an
    # alarm on the wall for a healthy machine that was merely asleep.
    #
    # AND a machine with no memory database is NOT asleep. An earlier version ran
    # the sqlite query, got nothing back from a machine that has no database by
    # design, and fell through into the retry branch — so Kali showed amber while
    # it was being actively used. "No answer to THAT question" is not "no answer".
    n=$(timeout 8 ssh -o BatchMode=yes -o ConnectTimeout=5 "$alias" "$q" 2>/dev/null)
    if [ -n "$n" ]; then
      up=1; detail="$(printf "%'d" "$n" 2>/dev/null || echo "$n") mem"
    elif timeout 8 ssh -o BatchMode=yes -o ConnectTimeout=5 "$alias" true 2>/dev/null; then
      up=1; detail="up, no db"                 # answered fine, just has no database
    else
      sleep 1                                   # now it really might be asleep
      n=$(timeout 20 ssh -o BatchMode=yes -o ConnectTimeout=15 "$alias" "$q" 2>/dev/null)
      if [ -n "$n" ]; then
        up=2; detail="$(printf "%'d" "$n" 2>/dev/null || echo "$n") mem (woke)"
      elif timeout 20 ssh -o BatchMode=yes -o ConnectTimeout=15 "$alias" true 2>/dev/null; then
        up=2; detail="woke, no db"
      else
        up=0; detail="offline"
      fi
    fi
  fi
  # up: 0 offline · 1 awake · 2 was asleep, answered on the retry
  # jq, not raw printf — `detail` carries whatever sqlite and ssh hand back, and
  # one stray quote would make the whole fleet payload invalid JSON, which the
  # board rejects as a unit. Same fault that was silently dropping states in
  # send.sh; fixed here before it ever bit.
  local up_b; up_b=$([ "$up" -ge 1 ] && echo true || echo false)
  if command -v jq >/dev/null 2>&1; then
    jq -cn --arg n "$(echo "$name" | tr 'a-z' 'A-Z')" --argjson u "$up_b" \
           --argjson s "$up" --arg d "$detail" \
           '{name:$n,up:$u,state:$s,detail:$d}' > "$TMP/$name.json"
  else
    local nd dd
    nd=$(printf '%s' "$name" | tr 'a-z' 'A-Z' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')
    dd=$(printf '%s' "$detail" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr -d '\n\r\t')
    printf '{"name":"%s","up":%s,"state":%s,"detail":"%s"}\n' "$nd" "$up_b" "$up" "$dd" \
      > "$TMP/$name.json"
  fi
}

# every machine in the registry, enabled or not — you want to SEE a disabled box
n=0
while IFS='|' read -r name alias dbdir os enabled; do
  name=$(echo "$name" | xargs); alias=$(echo "$alias" | xargs)
  [[ -z "$name" || "$name" == \#* ]] && continue
  [ -z "$alias" ] && continue
  probe "$name" "$alias" &
  n=$((n+1))
done < <(grep -v '^[[:space:]]*#' "$CONF" 2>/dev/null | grep '|')
wait

machines=$(cat "$TMP"/*.json 2>/dev/null | paste -sd, -)
payload=$(printf '{"stamp":"%s","machines":[%s]}' "$(date '+%H:%M')" "$machines")

CACHE="$HOME/.ranger-memory/config/rangerpuck.ip"
# Cache FIRST, mDNS second — the ordering rule send.sh explains in full: on this
# network rangerpuck.local RESOLVES but does not ANSWER, so leading with it just
# buys a 3-second timeout on every single run.
for host in "$(cat "$CACHE" 2>/dev/null)" "rangerpuck.local"; do
  [ -z "$host" ] && continue
  if curl -s --max-time 3 -X POST -H 'Content-Type: application/json' \
       -d "$payload" "http://$host/fleet" >/dev/null 2>&1; then
    echo "pushed $n machines to $host"
    echo "$payload" | python3 -m json.tool 2>/dev/null | grep -E '"name"|"up"|"detail"' | paste - - - | sed 's/^/  /'
    exit 0
  fi
done
echo "puck unreachable" >&2; exit 1
