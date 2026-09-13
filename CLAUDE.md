# RangerPuck — a physical status companion for the Ranger fleet

A €20 screen on the desk that shows what Claude Code is doing right now, and whether
the fleet is healthy. Inspired by the CrabPuck project (youtu.be/Vt5APRiFBSU).

---

## THE HARDWARE — read this before writing a line of code

**Waveshare ESP32-C6-LCD-1.47** (arrived 2026-09-12, €20.26)

| | |
|---|---|
| MCU | ESP32-C6, **RISC-V** 160 MHz HP core + 20 MHz LP core |
| RAM | 512 KB HP SRAM, 16 KB LP SRAM, 320 KB ROM |
| Flash | 4 MB |
| Display | **1.47" ST7789, 172 × 320**, 262K colour |
| Radio | Wi-Fi 6 (2.4 GHz only), BLE 5, **IEEE 802.15.4** (Thread/Zigbee/Matter) |
| Other | USB-C (full-speed, native), microSD slot, onboard RGB LED (WS2812) |

⚠️ **It is NOT a Linux machine.** Amazon's listing says "Operating System: Linux" and
"CPU Model: Core M Family" — both wrong. There is no shell, no SSH, no filesystem in
the Unix sense. It runs a single firmware image. Anyone who plans around SSHing into
it has misunderstood the board.

⚠️ **DO NOT GUESS THE GPIO PINS.** The LCD pins, backlight pin and RGB LED pin must be
taken from Waveshare's own wiki for the **ESP32-C6-LCD-1.47** specifically — not from
a different Waveshare board, not from a generic ESP32-C6 devkit, not from memory.
Confirm them, write them in `docs/PINOUT.md` with the source URL, and only then use
them. A wrong pin here costs hours of "why is the screen black".

---

## WHAT IT SHOWS

Two modes, cycling or switched:

**1. Claude state** (the CrabPuck idea — the primary mode)
```
  ┌──────────────────┐
  │  🎖️   APPROVE    │   ← big, readable across a room
  │      needs a yes │
  │   16:17 · 0m 31s │   ← clock + how long it has been waiting
  └──────────────────┘
```
States: `IDLE` · `THINKING` · `APPROVE` (waiting on David) · `RUNNING` · `DONE` · `ERROR`
The RGB LED mirrors it: amber = waiting on you, blue = working, green = done, red = error.
**The point of the whole build: David should never again miss that Claude is sat
waiting for a yes while he is looking at another screen.**

**2. Fleet health**
```
  M3   ● 18,215
  M4   ● 04:12
  M5   ●
  KALI ○
```
Machine up/down, memory count, last sync time. Data comes from `ranger_memories.db`
and `~/.ranger-memory/config/fleet.conf`.

---

## ARCHITECTURE — deliberately dumb board, smart Mac

```
Claude Code hook  ──►  tools/send.sh  ──►  HTTP POST  ──►  ESP32 shows it
(on M3)                (on M3)              (Wi-Fi)         (dumb display)
```

The board holds **no logic and no secrets**. It listens on HTTP, receives a small JSON
state, and renders it. All decisions happen on the Mac. This keeps the firmware
flashable without re-entering credentials and means a compromised board leaks nothing.

Wi-Fi credentials live in `secrets.h`, which is **gitignored and deny-listed** — the
agent must never read or print it. Same rule as the `.env` files: secrets stay out of
chat.

---

## BUILD ORDER — do not skip ahead

1. **Confirm the pinout** from the Waveshare wiki → `docs/PINOUT.md` with source URL.
2. **Toolchain**: `arduino-cli`, ESP32 core 3.x (needed for C6 — 2.x does NOT support it).
3. **Hello screen** — solid colour, then text. Prove the display works before anything else.
4. **Wi-Fi + HTTP listener** — board gets an IP, responds to a POST, prints to serial.
5. **`tools/send.sh`** — push a state from the Mac, see it on the screen.
6. **State rendering** — the six states, with the RGB LED.
7. **Claude Code hook** — wire real session state into it.
8. **Fleet mode** — second screen, data from the databases.

Each step must work before the next begins. A black screen at step 6 is unfindable;
at step 3 it is obvious.

---

## RULES

- **Never write to `~/.claude/settings.json`** without showing David the JSON first.
- **Never read or print `secrets.h`.**
- The board is at whatever IP DHCP gives it — **use mDNS (`rangerpuck.local`)**, never a
  hard-coded address. Same lesson as the fleet: hard-coded IPs rot (see `fleet.conf`).
- Serial monitor at **115200**.
- If the screen stays black, suspect the **backlight pin** first, then SPI pins, then
  the driver. Do not rewrite the sketch before checking the wiring assumption.

---

## STATUS

- [x] Board arrived 2026-09-12
- [x] Pinout confirmed from Waveshare wiki → docs/PINOUT.md
- [x] Toolchain installed — arduino-cli 1.5.1, esp32 core 3.3.11
- [x] Hello screen — WORKING 2026-09-12, confirmed on the board
- [x] Wi-Fi + HTTP — rangerpuck.local (address is DHCP; read it from `~/.ranger-memory/config/rangerpuck.ip`), -55dBm
- [x] send.sh working
- [x] States rendering — 6 states, big fonts, colour band
- [x] Claude Code hook wired — 5 events, settings.json backed up
- [ ] Fleet mode

---

## WIRED UP — 2026-09-12

```
Notification     -> APPROVE  amber   Claude wants a yes
UserPromptSubmit -> THINKING blue
PreToolUse       -> RUNNING  blue
Stop             -> DONE     green, then IDLE after 45s
SessionStart     -> IDLE     dim
```

Hook: `~/.claude/hooks/rangerpuck.sh` (silent, always exit 0, every call backgrounded
so a slow or absent puck can never stall a tool call). Existing hooks were APPENDED to,
never replaced — `save-conversation.sh` still runs first on Stop.

**Board:** 192.168.1.12, -53 dBm. mDNS is unreliable here because M3 is on Ethernet and
the puck is on Wi-Fi — consumer routers often will not bridge multicast. `send.sh`
therefore tries the name, falls back to a cached IP at
`~/.ranger-memory/config/rangerpuck.ip`, then sweeps the subnet and re-caches. No IP is
hard-coded anywhere; that is the fleet-config lesson applied.

**Lessons from the build, worth not repeating:**
- Duplicate `secrets.h` copies in each sketch folder went stale silently — the sketch
  compiled against an old password while David edited the new one. Fixed with symlinks
  to one source of truth. Same failure shape as M3's two `cormorant` wallets.
- The router password on the sticker was printed in quotes; the quotes are punctuation,
  not part of the password. 17 chars vs the real 15, giving reason code 204.
- Reason code 204 = handshake timeout = wrong password. 201 = AP not found. Having the
  firmware print the code turned "wifi doesn't work" into a one-line diagnosis.
- Size-1 text on a 1.47" panel is unreadable. Body text is size 2 minimum, state 3-4.

---

## FINISHED 2026-09-12 — everything on the list, plus more

**Mascot:** the Ranger helmet (`ranger_logo.h`, 72×77, **1-bit, 693 bytes**), generated
from `HollywoodSaver/images/ranger.png` by `tools/make-logo.py`. Stored 1-bit *on
purpose* — a 1-bit bitmap can be TINTED with the state colour at draw time, so the
helmet itself goes amber for APPROVE. A full-colour image would be ~30 KB and stuck
one colour. The cat is kept as an alternative; `tools/mascot.sh` switches.

**Orientation:** landscape by default, `tools/rotate.sh` flips it. Saved to NVS.

**Status-line data on the board**, read from Claude Code's own transcript via the hook's
`transcript_path`: context size in thousands, the running tool's name, and elapsed time
since the prompt. Costs **7 ms** on a 21 MB transcript because it only parses the last 40
lines, and caches the last figure so a payload-less hook still shows something.

**Fleet view** when idle — reads `fleet.conf`, probes every machine in parallel with short
timeouts so a sleeping M4 cannot stall it. Drift between machines is visible at a glance.

### Tools
```
tools/flash.sh        compile + upload, finds the port itself
tools/monitor.sh      serial at 115200
tools/send.sh         push a state (name -> cached IP -> subnet sweep)
tools/fleet-push.sh   gather fleet health and push it
tools/rotate.sh       landscape / portrait
tools/mascot.sh       helmet / cat
tools/make-logo.py    any 2-tone PNG -> 1-bit header, with ASCII preview
```

### Gotchas paid for in this build
- **arduino-cli inserts function prototypes above the first function definition.** Adding
  a helper function moved that point above `struct Look`, producing `'Look' does not name
  a type`. Declare all types above every function. The error is about where the *compiler*
  wrote code, not yours.
- **Duplicate `secrets.h` per sketch folder drifted silently** — David edited one while
  the build compiled another. Looked exactly like "nano won't save". Now symlinks to one
  source of truth. Third instance of duplicate-with-drift this week.
- **Router sticker passwords are printed in quotes**; the quotes are punctuation.
- **Wi-Fi reason codes are the diagnosis**: 204 handshake timeout = wrong password,
  201 = AP not found. Print the code and "wifi doesn't work" becomes one line.
- **mDNS does not cross Ethernet↔Wi-Fi** on this router. Never hard-code the IP — cache
  and re-discover instead.
- **Size-1 text is unreadable on a 1.47" panel.** Body size 2 minimum, state 3–4.
