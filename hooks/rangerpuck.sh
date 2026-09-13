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
# Respect an identity the caller already set. This was hardcoded to CLAUDE, which
# meant a SECOND machine running this same hook (the Kali box) had its states
# filed under M3's CLAUDE slot — the two would overwrite each other and fight
# over the screen, which is the exact fault the per-agent slots exist to prevent.
# Set env.PUCK_WHO in that machine's ~/.claude/settings.json to name it.
export PUCK_WHO="${PUCK_WHO:-CLAUDE}"
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


# --- what is it ACTUALLY doing? ---------------------------------------------
# The board used to say THINKING "working" and RUNNING "Bash" — a literal string
# and a tool name. Neither tells you anything: every turn looked identical, so
# the screen stopped carrying information and became a busy-light. The payload
# has had the real detail in it all along, tool_input just was never read.
#
# Keep it SHORT: line1 renders at text size 2 and fits ~18 characters.
squash() {   # squash <text> <maxlen> — collapse whitespace, strip noise, clip
  printf '%s' "$1" | tr '\n\t' '  ' | tr -s ' ' \
    | sed -e 's/^ *//' -e 's/ *$//' | cut -c1-"${2:-18}"
}

detail_for() {   # detail_for <tool_name> <payload> -> a human-readable summary
  local tool="$1" p="$2" d=""
  case "$tool" in
    Bash)
      # the command minus any leading env assignments and sudo, first 3 words:
      # "git push origin main" -> "git push origin"
      d=$(printf '%s' "$p" | jq -r '.tool_input.command // empty' 2>/dev/null \
          | sed -e 's/^ *sudo  *//' -e 's/^[A-Z_][A-Z0-9_]*=[^ ]*  *//g' \
          | awk '{print $1, $2, $3}') ;;
    WebSearch)        d=$(printf '%s' "$p" | jq -r '.tool_input.query // empty' 2>/dev/null) ;;
    WebFetch)         # `\?` is a GNU-sed extension and does nothing on BSD sed, so the
                      # scheme survived and 's|/.*||' then clipped it to "https:".
                      # POSIX character class works on both.
                      d=$(printf '%s' "$p" | jq -r '.tool_input.url // empty' 2>/dev/null \
                          | sed -e 's|^[a-zA-Z][a-zA-Z0-9+.-]*://||' -e 's|/.*||') ;;
    Read|Edit|Write|NotebookEdit)
                      d=$(printf '%s' "$p" | jq -r '.tool_input.file_path // empty' 2>/dev/null \
                          | sed 's|.*/||') ;;                       # basename only
    Grep|Glob)        d=$(printf '%s' "$p" | jq -r '.tool_input.pattern // empty' 2>/dev/null) ;;
    Task|Agent)       d=$(printf '%s' "$p" | jq -r '.tool_input.description // empty' 2>/dev/null) ;;
    *)                d="" ;;
  esac
  [ -z "$d" ] && d="$tool"
  squash "$d" 18
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
    # show what was ASKED, not the word "working" — that is the one thing that
    # actually identifies this turn from across the room.
    ask=$(printf '%s' "$payload" | jq -r '.prompt // empty' 2>/dev/null)
    "$SEND" THINKING "$(squash "${ask:-working}" 18)" "$ctx_line" >/dev/null 2>&1 & ;;
  PreToolUse)
    tool=$(printf '%s' "$payload" | jq -r '.tool_name // empty' 2>/dev/null)
    # line1 = what it is doing, line2 = which tool + how long. "git push origin"
    # beats "Bash" every time.
    "$SEND" RUNNING "$(detail_for "$tool" "$payload")" \
            "$(squash "${tool} $(elapsed)" 18)" >/dev/null 2>&1 & ;;
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
