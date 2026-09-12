# 🎖️ RangerPuck — cheat sheet

Waveshare ESP32-C6-LCD-1.47 · the little Ranger on the desk
Project: `~/esp32-projects/1-ranger-puck` · Board: `192.168.1.12` / `rangerpuck.local`

## Aliases (type these anywhere)

| alias | what it does |
|-------|--------------|
| `puck` | refresh the fleet view — run after a sync |
| `puckstate` | send a state by hand — `puckstate APPROVE "needs a yes" "2m 18s"` |
| `puckrotate` | flip landscape ⇄ portrait (remembers across power cuts) |
| `puckmascot` | switch Ranger helmet ⇄ cat (remembers) |
| `puckflash` | compile + upload firmware, finds the USB port itself |
| `puckmon` | serial monitor at 115200 (hit the board's reset to see boot output) |
| `puckinfo` | what the board reports right now — state, IP, signal, uptime |
| `puckcheat` | this page |

## States and colours

| state | colour | when |
|-------|--------|------|
| `APPROVE` | 🟠 amber + pulse bar | **Claude needs a yes to proceed** — a real block |
| `WAITING` | 🩵 teal | session idle ~60s — **your move, but nothing is blocked** |
| `THINKING` | 🔵 blue | Claude is working |
| `RUNNING` | 🔵 blue | a tool is running (shows which one) |
| `SYNC` | 🟣 purple | fleet sync in progress (shows the stage + machine) |
| `DONE` | 🟢 green | finished |
| `ERROR` | 🔴 red | something failed |
| `IDLE` | ⚪ dim | nothing doing → **falls back to the fleet view** |

## The projects that drive it

| project | what it shows | menu |
|---------|---------------|------|
| `1-ranger-puck` | the board itself, agent states | 4–9 |
| `2-hutch-watch` | weather, dry windows, sunshine, guinea pigs | 1–3 |
| `3-plane-spotter` | aircraft overhead, radar | 15–18 |
| `4-sky-siren` | ISS and Starlink passes | 19–21 |

**Adding a new one:** make `~/esp32-projects/N-name/`, read the location from
`~/.config/rangerpuck/spotter.env` (**never hardcode it**), push states with
`1-ranger-puck/tools/send.sh`, then add a menu entry and a line here.

## Files

| what | where |
|------|-------|
| Project + brief | `~/esp32-projects/1-ranger-puck/CLAUDE.md` |
| Firmware | `RangerPuck/RangerPuck.ino` |
| **Confirmed pinout** | `docs/PINOUT.md` ← never guess these |
| Wi-Fi credentials | `RangerPuck/secrets.h` (gitignored; other sketches symlink to it) |
| Ranger helmet bitmap | `RangerPuck/ranger_logo.h` (1-bit, 693 bytes, tintable) |
| Claude Code hook | `~/.claude/hooks/rangerpuck.sh` |
| Hook wiring | `~/.claude/settings.json` (5 events) |
| Cached board IP | `~/.ranger-memory/config/rangerpuck.ip` |
| Aliases | `~/.zshrc_aliases_folder/ranger_aliases.zsh` |

## Talking to the board directly

```bash
curl http://192.168.1.12/                      # status
curl -X POST -d '{"state":"APPROVE","line1":"needs a yes"}' \
     http://192.168.1.12/state                 # set a state
curl http://192.168.1.12/rotate                # cycle orientation
curl http://192.168.1.12/mascot                # toggle helmet/cat
```

## Rebuilding the logo

```bash
cd ~/esp32-projects/1-ranger-puck
.venv/bin/python tools/make-logo.py <any-2-tone.png> [width]
puckflash
```
Prints an ASCII preview before you flash, so you see it came out right.

## When it misbehaves

| symptom | look here first |
|---------|-----------------|
| Screen black, LED alive | **backlight pin (GPIO 22)**, then SPI pins |
| Won't join Wi-Fi | flash `WifiDiag/` — it prints the **reason code**: 204 = wrong password, 201 = AP not found |
| Can't see the network at all | flash `WifiScan/` — lists every 2.4 GHz SSID it can see. **The C6 cannot see 5 GHz** |
| `rangerpuck.local` not resolving | normal — M3 is wired, the puck is wireless, the router won't bridge multicast. `send.sh` falls back to the cached IP automatically |
| `'X' does not name a type` | arduino-cli inserts prototypes above the first function — **declare types above every function** |
| Edits seem to do nothing | check you're editing the file the build reads. `secrets.h` is symlinked; duplicates drift silently |

## The rule that keeps biting

**Duplicate-with-drift.** Three times in one week: two `cormorant` wallets, two `TODO_MASTER`
copies, three `secrets.h` files. It never announces itself — it always looks like
*"the tool is broken"* when it's really *"you're looking at a different copy."*
One source of truth, symlink the rest.
