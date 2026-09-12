# 📜 RangerPuck Changelog

All notable changes, milestones, architectural decisions, and agent integrations for the **RangerPuck** physical desk companion.

---

## [1.5.0] — 2026-09-12 (Night) — Three bugs only a human could find

Everything here was found by the user sitting in front of the device with a watch, not
by testing. Each was invisible from inside the system.

### 🔴 Fixed — "9 seconds then thinking showed"
`send.sh` tried **mDNS first**, then the cached IP. But `rangerpuck.local` **resolves and
never answers** — the router will not bridge multicast between Ethernet and Wi-Fi — so
`curl` waited out the full timeout on *every single call*, then fell back to the address
that works. Measured: **2.19 s per send**. With a hook firing on every prompt, that is the
difference between a status light and one you stop believing.

Now the cache is the fast path and mDNS is the fallback, timeout 2 s → 1 s.
**2.19 s → 0.12 s, an 18× improvement.** I had tested the states, the ladder and the
interrupts, and never once measured how long a message took to *arrive*.

### 🔴 Fixed — the hook never said who it was
`~/.claude/hooks/rangerpuck.sh` called `send.sh` without a `who` argument and without
`PUCK_WHO` set. Every state it sent landed in an **anonymous slot with no colour tag**,
competing with anything sent manually as `CLAUDE`. Two slots, one agent, fighting over one
screen — and a stale test state kept winning ties. **The orange CLAUDE tag had never once
appeared**; nobody noticed because there was only ever one agent until Gemini arrived.

### 🔴 Fixed — the radar silently hid the fleet view
`RADAR` (18) outranks `IDLE` (5), so once aircraft were being tracked the machine list
**never appeared again**. Both are ambient and neither is urgent, so they now **alternate
every 12 seconds** when nothing needs a human. A feature that quietly removes another
feature is the kind of regression tests do not catch, because both still work.

### Changed
* `RADAR` urgency **30 → 18** — beats a finished job, never live work. *(An earlier
  attempt at this silently no-opped: the script printed a success message unconditionally
  after a `str.replace` that matched nothing, and the fix was reported as done when it was
  not. Later edits assert the change landed.)*
* Settled states expire in **60 s**, not 3 minutes.
* `SKY` state added (violet, urgency 45) for ISS passes — **above** "your move", far below
  amber. `4-sky-siren` was firing `APPROVE` for a satellite; the very first project built
  to the protocol broke its central rule, because amber is the loudest thing available and
  reaching for it is the natural move. The reasoning now sits in the code beside the call.

### Noted
The working copy and the published repo had drifted **17 lines apart**, and a blind copy
between them wiped a config reader and reintroduced personal paths. They are not the same
file and must never be copied wholesale. Duplicate-with-drift, sixth occurrence this week.

---

## [1.4.0] — 2026-09-12 (Late Evening) — Published, and the promise that wasn't true

### Published
Three public repos, MIT, each useful on its own:
* **[rangerpuck](https://github.com/davidtkeane/rangerpuck)** — the board, firmware, tools, `AGENTS.md`.
* **[plane-spotter](https://github.com/davidtkeane/plane-spotter)** — ADS-B tracker, optional radar.
* **[between-the-showers](https://github.com/davidtkeane/between-the-showers)** — Met Éireann dry-window finder.

### 🔴 Fixed — an independent model review found five blockers before release
Reviewed by a different model, asked to be the stranger who files the angry issue. Two
findings mattered enough to name:

* **`APPROVE never expires` — the README's headline promise — was FALSE.** `recordAndPick()`
  pruned any agent slot quiet for more than ten minutes. **An agent waiting for a human is
  silent by definition — that is what waiting means.** With the radar posting every 20s, an
  unanswered APPROVE was silently dropped after exactly ten minutes: precisely the "out of
  the room" case the feature exists for. APPROVE is now exempt from pruning, and slot
  eviction drops the *least urgent* rather than the oldest.
* **The advertised build failed on a fresh clone.** `arduino-cli` requires the sketch folder
  name to match the `.ino`; the repo shipped `firmware/`. A stranger following the README
  failed at step one.

Also fixed: expired states resurrecting from their slots on the next unrelated POST; `RADAR`
urgency (30) outranking working agents (20), contradicting the documented "ambient" claim;
a failed first Wi-Fi connect returning before `server.begin()`, leaving the board rejoining
the network permanently unreachable; `range: 0` dividing by zero in `drawRadar`; and
`send.sh` producing invalid JSON on a quoted string while still reporting success, because
`curl` exits 0 on an HTTP 400.

### Changed
* Cache path `~/.ranger-memory/config/rangerpuck.ip` → **`~/.config/rangerpuck/ip`** (XDG, and
  not a personal path) — both repos agree on the filename now; they previously did not, so
  the advertised fallback between them never fired.
* `monitor.sh` discovers the board instead of hardcoding one machine's serial device.
* `make-logo.py` requires an explicit image argument.

### Comfort fixes — from living with it, not from testing
* **Flicker.** The radar repainted the whole screen 2.5×/s for dead reckoning. At 20 km an
  aircraft moves ~200 m/s — a pixel or two — so most repaints showed nothing and cost a
  flash. Now 1 Hz, and **skipped entirely unless something moved at least one pixel**.
* **The label jumped.** The primary swapped every poll as aircraft traded places in the
  scoring. Now **held for 90 s**, and a rival must score 25 points better to take over.
  *"I don't want it flashing beside me"* is feedback no test produces.

---

## [1.3.0] — 2026-09-12 (Evening) — One board, several agents

### Added
* **Per-agent state slots.** The board previously held ONE state, so whoever wrote last won.
  Claude Code's hooks fire automatically every turn; Gemini writes only when it chooses to.
  The result: Claude silently clobbering Gemini's `APPROVE` while Gemini sat genuinely
  blocked, and the user never saw the request. The board now keeps a slot per agent (max 4) and
  renders the **most urgent**, not the most recent. A human being blocked always wins.
* **Board-side state expiry.** Agents crash, get killed, or forget. Settled states
  (`DONE`/`WAITING`/`ERROR`) expire after 3 minutes, working states
  (`THINKING`/`RUNNING`/`SYNC`) after 8. `APPROVE` never expires. This solved "will Gemini
  return to the fleet view like Claude does" **in the board rather than the protocol** — the
  right place, because it cannot be forgotten.
* **Plane radar** (`RADAR` state, `POST /plane`). House at the centre, range rings, every
  contact drawn at its true bearing and distance and **rotated to its real heading**.
  Distance gauge, closest-point-of-approach, altitude trend, airline name.
  The board **dead-reckons between polls** — advancing each icon along its own track at its
  own ground speed — so aircraft crawl rather than jump. It stops extrapolating after 45 s
  and marks itself `stale`; a jet at 400 kt covers 7 km a minute and a guess that old is
  fiction. The plane is drawn from **rotated points, not a bitmap** — a bitmap cannot rotate.
* **Weather states** — `DRY`, `RAIN`, `SUN`, `PIGS` (guinea pig welfare outranks weather in
  the push logic: it is the one that can do harm).
* **Ranger helmet mascot**, stored **1-bit on purpose** (693 bytes) so it can be **tinted**
  with the state colour — the badge itself goes amber when you are needed. A full-colour
  image would be ~30 KB and stuck one colour. Cat mascot kept, `tools/mascot.sh` switches.
* **Landscape/portrait** switch, saved to NVS.
* **Claude Code status-line data on the board** — context size in thousands (the number that
  predicts compaction), the running tool's name, elapsed since the prompt. 7 ms on a 21 MB
  transcript, because it parses only the last 40 lines.

### The rule, applied three times in one evening
`APPROVE` vs `WAITING`; the radar demoted below working agents; and OpenWeather's
minute-rain feed demoted after it claimed "raining now" on a dry evening while Met Éireann
said 6%. **A warning that cries wolf teaches you to ignore it, and then it is useless on the
day it is right.**

---

## [1.2.0] — 2026-09-12 (Evening Session)

### Added
* **Gemini Agent Integration:**
  * Created `GEMINI.md` defining the operational protocol for Gemini models in Antigravity IDE and `agy` CLI.
  * Identity assigned: `PUCK_WHO=GEMINI` with custom **blue** UI theme on the ST7789 display.
  * Installed permanent agent rules in `~/.gemini/antigravity-cli/rules/rangerpuck.md` and `/Users/ranger/scripts/.agents/rules/rangerpuck.md`.
  * Verified live transmission over Wi-Fi to puck at `192.168.1.12`.
* **Outlandish Ideas Expansion (`IDEAS.md`):**
  * Added Section D with 6 creative concepts:
    1. Garda Air Support / Irish Coast Guard emergency helicopter radar (ADS-B).
    2. International Space Station (ISS) & Starlink overhead flyby warning siren.
    3. Met Éireann hyper-local rain nowcaster (7-minute radar warning).
    4. Dublin Mountains Aurora Borealis & solar storm alarm (NOAA Kp-index).
    5. Physical Wi-Fi Deauth / Evil Twin intrusion detection tripwire.
    6. TFI Real-Time Dublin Bus & Luas countdown board.

### Architectural Breakthrough: The Preemptive APPROVE Protocol
* **The Problem Identified:** Claude Code features an external harness daemon hook (`Notification`) that fires on suspend. Gemini has no such daemon hook and freezes the moment a user approval prompt appears. If an agent reports `APPROVE` *after* being blocked, it is already frozen and cannot emit packets—leaving the user unaware in another window.
* **The Solution:** Updated `AGENTS.md`, `GEMINI.md`, and global rules to enforce **Preemptive APPROVE**:
  * Any agent without an automated external hook MUST emit `send.sh APPROVE "needs a yes" ...` *before* triggering any gated action or file edit.
  * If approval is granted or not needed, the subsequent action overwrites the state immediately. A transient 1-second amber is harmless; a silent block defeats the device.

---

## [1.1.0] — 2026-09-12 (Afternoon Session)

### Added
* **Universal Agent Protocol (`AGENTS.md`):**
  * Created a context-free, cold-start protocol document readable by any LLM (Claude, Gemini, Ollama, Qwen, OpenClaw).
  * Standardized payload schema: `{"state": "...", "line1": "...", "line2": "...", "who": "..."}`.
  * Established the **Golden Rule of Amber**: `APPROVE` (amber) is reserved strictly for hard blockers where the user is needed. `WAITING` (teal) is used for turn completion.
* **Agent Personalities & Theme Colors:**
  * `CLAUDE` (Orange)
  * `GEMINI` (Blue)
  * `OLLAMA` (Green)
  * `QWEN` (Magenta)
  * `WEATHER` (Cyan)
* **Self-Healing Transmission Tool (`tools/send.sh`):**
  * Multi-tier connection fallback:
    1. Attempts mDNS (`rangerpuck.local`).
    2. Falls back to cached IP (`~/.ranger-memory/config/rangerpuck.ip`).
    3. Automatically scans subnet (`base.2` to `base.60`) to discover and cache moving DHCP addresses across Ethernet/Wi-Fi bridges.

---

## [1.0.0] — 2026-09-12 (Morning Session) — Initial Hardware Bringup

### Hardware Specs Locked:
* **Board:** Waveshare ESP32-C6-LCD-1.47 (RISC-V 160MHz, 4MB Flash, Wi-Fi 6, BLE 5, IEEE 802.15.4).
* **Display:** 1.47-inch ST7789 SPI LCD (172 × 320 resolution, 262K color).
* **RGB LED:** Built-in WS2812 on GPIO 8.
* **Pinout Documentation:** Created `docs/PINOUT.md` directly verified from Waveshare wiki:
  * `LCD_SCK: 7` | `LCD_MOSI: 6` | `LCD_DC: 15` | `LCD_CS: 14` | `LCD_RST: 21` | `LCD_BL: 22` | `RGB: 8`.

### Firmware Features Implemented (`RangerPuck.ino`):
* **Deliberately Dumb Architecture:** All business logic, decision making, and API fetching remain on the host (Mac/M3); the ESP32 acts solely as a display/status renderer listening over HTTP.
* **Endpoints:**
  * `POST /state`: Accepts JSON payload containing state and 2 lines of text.
  * `GET /`: Returns plain-text health check, signal strength (RSSI), uptime, and current state.
* **Graphics & Rendering:**
  * Custom retro UI layout with clear typography, status pill, and elapsed waiting timer.
  * Onboard RGB LED mirrors the screen state (Amber = Approve, Blue = Thinking/Running, Green = Done, Teal = Waiting, Red = Error).
  * Custom monochrome bitmap logo generator (`tools/make-logo.py` ➡️ `ranger_logo.h`).
* **Tooling:**
  * `tools/flash.sh`: One-command compilation and flashing using `arduino-cli`.
  * `tools/monitor.sh`: Serial console monitor for debugging.
  * `tools/rotate.sh`: Display orientation switcher.
