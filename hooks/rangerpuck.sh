#!/usr/bin/env bash
# ============================================================================
#  rangerpuck.sh — drive the desk puck from Claude Code's own lifecycle.
#
#  CANONICAL COPY: ~/esp32-projects/1-ranger-puck/hooks/rangerpuck.sh
#  ~/.claude/hooks/rangerpuck.sh is a SYMLINK to this file, so the 100 lines that
#  make the amber light mean anything are version-controlled with the firmware
#  they drive. Install on a new machine with tools/install-hooks.sh.
#
#  Claude Code passes a JSON payload on stdin containing transcript_path, so we
#  can read the real numbers behind the status line — context size, tokens out,
#  and how long this turn has been going — and show them on the board.
#
#  MUST stay silent and MUST always exit 0. Every send is backgrounded. The
#  puck is a nicety and never gets to break the session it reports on.
#
#    Notification     -> APPROVE  amber   Claude wants a yes   ← the point
#    UserPromptSubmit -> THINKING blue
#    PreToolUse       -> RUNNING  blue
#    Stop             -> DONE     green, then IDLE after 45s
#    SessionStart     -> IDLE     dim
# ============================================================================
SEND="$HOME/esp32-projects/1-ranger-puck/tools/send.sh"
# Identify. Without this every state lands in an anonymous slot, shows no colour
# tag, and competes with anything sent manually as CLAUDE — two slots, same
# agent, fighting over one screen.
export PUCK_WHO=CLAUDE
[ -x "$SEND" ] || exit 0
EVENT="${1:-}"
STAMP="$HOME/.ranger-memory/config/.puck-last-prompt"
CTXF="$HOME/.ranger-memory/config/.puck-ctx"

# --- read the hook payload (non-blocking; empty if nothing is piped in) -----
payload=""
if [ ! -t 0 ]; then payload=$(timeout 0.4 cat 2>/dev/null); fi
TRANSCRIPT=$(printf '%s' "$payload" | jq -r '.transcript_path // empty' 2>/dev/null)

# --- context size + tokens out, from the tail of the transcript (~7ms) ------
ctx_line=""
if [ -n "$TRANSCRIPT" ] && [ -f "$TRANSCRIPT" ]; then
  read -r ctx out <<<"$(tail -40 "$TRANSCRIPT" 2>/dev/null | jq -sr '
      [.[] | select(.type=="assistant") | .message.usage | select(.!=null)][-1] as $u
      | (( ($u.input_tokens//0) + ($u.cache_read_input_tokens//0)
           + ($u.cache_creation_input_tokens//0) ) | tostring)
        + " " + (($u.output_tokens//0)|tostring)' 2>/dev/null)"
  if [ -n "${ctx:-}" ] && [ "$ctx" -gt 0 ] 2>/dev/null; then
    printf '%s' "$ctx" > "$CTXF" 2>/dev/null
    ctx_line="$((ctx/1000))k ctx"
  fi
fi
[ -z "$ctx_line" ] && [ -f "$CTXF" ] && ctx_line="$(( $(cat "$CTXF") / 1000 ))k ctx"

elapsed() {   # how long since David hit enter
  [ -f "$STAMP" ] || { echo ""; return; }
  local s=$(( $(date +%s) - $(cat "$STAMP" 2>/dev/null || date +%s) ))
  if [ "$s" -ge 3600 ]; then printf '%dh %dm' $((s/3600)) $(((s%3600)/60))
  else printf '%dm %02ds' $((s/60)) $((s%60)); fi
}

when() {      # clock time the wait began — "16:17 · 8m 37s" tells you more than
              # a duration alone, especially if you have been out of the room
              #
              # `date -r EPOCH` is BSD; on Linux -r means "reference file" and it
              # fails with "No such file or directory". GNU wants `date -d @EPOCH`.
              # Try both so one hook script serves the Macs and the Kali box.
  [ -f "$STAMP" ] || { date +%H:%M; return; }
  local s; s=$(cat "$STAMP" 2>/dev/null) || { date +%H:%M; return; }
  date -r "$s" +%H:%M 2>/dev/null || date -d "@$s" +%H:%M 2>/dev/null || date +%H:%M
}

case "$EVENT" in
  Notification)
    # Claude Code fires Notification for TWO different things and they deserve
    # different urgency:
    #   "needs your permission to use X"  -> a real block, amber, look now
    #   "is waiting for your input"       -> just idle after ~60s, not a block
    # Treating both as APPROVE makes amber cry wolf, which ruins the one signal
    # the whole build exists for.
    msg=$(printf '%s' "$payload" | jq -r '.message // empty' 2>/dev/null)
    case "$msg" in
      *permission*|*approve*|*Approve*|*confirm*)
        "$SEND" APPROVE "needs a yes" "$(when) . $(elapsed)" >/dev/null 2>&1 & ;;
      *waiting*|*idle*)
        "$SEND" WAITING "your move" "$(when) . $(elapsed)" >/dev/null 2>&1 & ;;
      *)
        # unknown wording — assume it matters rather than miss a real block
        "$SEND" APPROVE "needs a yes" "$(when) . $(elapsed)" >/dev/null 2>&1 & ;;
    esac ;;
  UserPromptSubmit)
    date +%s > "$STAMP" 2>/dev/null
    "$SEND" THINKING "working" "$ctx_line" >/dev/null 2>&1 & ;;
  PreToolUse)
    tool=$(printf '%s' "$payload" | jq -r '.tool_name // empty' 2>/dev/null)
    "$SEND" RUNNING "${tool:-working}" "$(elapsed)" >/dev/null 2>&1 & ;;
  Stop)
    # The delayed IDLE used to fire unconditionally after 45s. Submit a new prompt
    # inside that window and the late IDLE landed AFTER the new THINKING, in the
    # same CLAUDE slot — so the board read "idle" while Claude was actively
    # working, for up to the 8-minute working expiry. Check whether the prompt
    # stamp moved while we were asleep, and stay quiet if it did.
    ( "$SEND" DONE "$(elapsed)" "$ctx_line" >/dev/null 2>&1
      before=$(cat "$STAMP" 2>/dev/null || echo 0)
      sleep 45
      after=$(cat "$STAMP" 2>/dev/null || echo 0)
      if [ "$before" = "$after" ]; then
        "$SEND" IDLE "ready" "" >/dev/null 2>&1
      fi
      "$HOME/esp32-projects/1-ranger-puck/tools/fleet-push.sh" >/dev/null 2>&1 ) & ;;
  SessionStart)
    "$SEND" IDLE "ready" "" >/dev/null 2>&1 & ;;
esac
exit 0
