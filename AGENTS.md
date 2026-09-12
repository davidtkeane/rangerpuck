# Driving the RangerPuck — for any AI agent

Hand this file to Claude, Gemini, Ollama, Qwen, OpenClaw or anything else. It is all
you need. The board sits on your user's desk and shows what you are doing, so they know
without watching your terminal.

## One command

```bash
./tools/send.sh <STATE> "<line1>" "<line2>" <WHO>
```

Or set `PUCK_WHO` once in your environment and drop the 4th argument.

```bash
export PUCK_WHO=GEMINI
send.sh THINKING "searching the web"
send.sh APPROVE  "needs a yes" "0m 31s"
send.sh DONE     "finished"
```

## States

| state | colour | use it when |
|-------|--------|-------------|
| `APPROVE` | 🟠 amber | **you are BLOCKED and need your user to say yes** — use sparingly |
| `WAITING` | 🩵 teal | you are finished, ball is in his court, nothing blocked |
| `THINKING` | 🔵 blue | reasoning, reading, searching |
| `RUNNING` | 🔵 blue | a tool or command is executing — put its name in line1 |
| `DONE` | 🟢 green | task complete |
| `ERROR` | 🔴 red | something failed |
| `IDLE` | ⚪ dim | nothing doing — the board falls back to the fleet view |

**The one rule that matters: do not overuse `APPROVE`.** Amber is the only signal that
says "your user is needed". If it fires when nothing is blocked, he learns to ignore it
within a week, and then it is useless on the day it matters. Use `WAITING` for
"I'm done, over to you".

## ⚠️ If your harness pauses you for approval, say so BEFORE it does

This is the one thing agents get wrong, and it defeats the whole point of the board.

When your harness stops to ask your user *"accept this file edit? 1. Yes 2. No"*, **you are
suspended.** You cannot send anything at that moment — you are not running. If you wait
until you are blocked to report being blocked, the board never hears about it, and your user
sits in another tab while you sit waiting for him.

So send it **first**:

```bash
send.sh APPROVE "file edit" "waiting"     # BEFORE the edit that will prompt
# ...now make the edit that triggers the prompt...
send.sh RUNNING "applying"                # after he answers
```

Any action you expect to be gated — a file write, a shell command, anything irreversible —
gets an `APPROVE` **before** it, not after. If the approval turns out not to be needed,
your next state overwrites it a second later and no harm is done. A false amber that
clears itself instantly costs nothing; a silent block costs your user the whole point of the
device.

Agents WITH automatic hooks (Claude Code fires on its `Notification` event) do not need to
do this manually. Everything else does.

## Who you are

Pass your name as the 4th argument or `PUCK_WHO`. Recognised, each with its own colour:
`CLAUDE` (orange) · `GEMINI` (blue) · `OLLAMA` (green) · `QWEN` (magenta) · `WEATHER` (cyan).
Anything else shows grey. Keep it under 8 characters.

## Line lengths

The panel is small. `line1` and `line2` are truncated at ~14 characters portrait,
~18 landscape. Write `"compiling"` not `"currently compiling the project"`.

## If you cannot use the script

Plain HTTP, no auth:

```bash
curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"state":"THINKING","line1":"working","line2":"","who":"GEMINI"}' \
  http://rangerpuck.local/state
```

`rangerpuck.local` is unreliable when the caller is on Ethernet and the board on Wi-Fi —
the router will not bridge multicast. `send.sh` handles that by falling back to a cached
IP and then sweeping the subnet. Prefer the script.

## Rules

- **Never block on it.** Background every call and ignore failures. The board is a
  passenger; it must never slow down or fail the work it is reporting on. The supplied
  script already does this.
- **Do not poll it** or send a state more than once a second.
- **Say nothing secret.** Anything sent appears on a screen in a room, and is readable by
  anything on the LAN over plain HTTP.
- **Do not reflash the firmware** unless your user asks. Other agents depend on it.

## You do not have to tidy up

The board **expires its own state**. If nothing new arrives it returns to the fleet view by
itself — after **3 minutes** for `DONE` / `WAITING` / `ERROR`, **8 minutes** for `THINKING` /
`RUNNING` / `SYNC`.

`APPROVE` is the exception and **never expires**: it means someone is genuinely blocked and
your user may be out of the room for an hour. That one keeps asking until it is answered or
replaced.

So you cannot leave the board lying by crashing, being killed, or forgetting to send a final
state. Send `DONE` when you finish if you like — it looks good — but you are not required to.

## Checking it is alive

```bash
curl -s http://rangerpuck.local/ || curl -s "http://$(cat ~/.config/rangerpuck/ip)/"
```

Returns state, IP, signal strength and uptime as plain text.
