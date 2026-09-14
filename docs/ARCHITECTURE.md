# RangerPuck — System Architecture (read this first)

**Last verified: 2026-09-14.** The one authoritative overview of the whole system.
If this contradicts an older doc (README/CHEATSHEET/AGENTS), trust this one and fix that one.

Board: Waveshare **ESP32-C6-LCD-1.47** (ST7789 172×320), Wi-Fi. `http://rangerpuck.local`
(mDNS) or the cached IP. Default orientation is **landscape** (`rot=1`, 320×172).

---

## 1. The core principle: the board is DUMB

The firmware holds no logic and makes no decisions. It listens for small JSON over HTTP
and renders it. Everything that decides *what to show* lives on the host machines (the
"collectors"). This keeps the firmware reflashable without re-entering anything and leaks
nothing from the desk.

```
collectors on M3 (+ hooks on M3/Kali/MSI)  ──HTTP POST──▶  the board renders
```

Flash budget is **tight — ~89% used** (`~1.17 MB / 1.31 MB`). Removing a feature to add one
is normal. `drawPlaneScreen()` was dead code already deleted for room.

---

## 2. HTTP endpoints

| Endpoint | Who posts | Purpose |
|----------|-----------|---------|
| `POST /state` | the Claude Code **hooks** (M3/Kali/MSI), `send.sh`, sky-siren | an agent's live state (per-agent slot, most-urgent wins) |
| `POST /pig`   | `weather.py --pigpush` | pig-run GO/WAIT — an **ambient** screen (does not interrupt) |
| `POST /weather` | `weather.py --puck` | ambient weather screen |
| `POST /fleet` | `fleet-push.sh` | fleet machine health screen |
| `POST /plane` | `spotter.py` | radar contacts |
| `POST /rotate` `POST /mascot` | by hand | orientation / mascot |
| `GET  /` | anyone | **status + diagnostics** — state, who, line1/2, rot, ambient screen, NTP, every agent slot, primary plane route. Use this to debug; do not squint at the screen. |

JSON is built with `jq` (bash) / `ConvertTo-Json` (PowerShell) / `Invoke-RestMethod` — never
raw `printf`, or a stray quote makes invalid JSON the board rejects as `400` and the state
silently vanishes.

---

## 3. Agent states + per-agent slots

The board keeps a **slot per agent** and renders the **most urgent**, not the most recent
(so a blocked agent always beats a progress update). Slots age by their own clock.

`urgency()` (higher wins):
```
APPROVE 100 (human blocked — NEVER expires)   PIGS 75 (welfare temp)
ERROR   80                                     PIGSOON 60 (rain <10min, PULSES)
PIGGO   50   PIGWAIT 48   SKY 45   WAITING 40   RADAR 18
SYNC/RUNNING/THINKING 20   DONE 15   IDLE/unknown 5
```
Expiry (in `loop()`): APPROVE never; WORKING states 8min; WAITING 22s; SKY ~66s; else 60s.

### Agent colours (`whoColour()`) — who is talking, at a glance
```
CLAUDE  orange 0xFD20   GEMINI blue 0x055F   OLLAMA green 0x07E0
QWEN    magenta 0xF81F  WEATHER cyan 0x07FF
KALI    amber 0xFCE0    MSI violet 0xA81F    PLANE grey 0x8410
```

---

## 4. The five AMBIENT screens (shown when everything is IDLE/RADAR)

Rotate every 12s, skipping any with no data (`have[]` gate). Index → screen:
```
0 planes (drawRadar)   1 fleet (drawFleet)   2 weather (drawWeather)
3 clock (drawClock)    4 pig-run (drawPigRun, from the /pig data)
```
`AMBIENT_COUNT = 5`. An active/blocked agent state overrides the rotation — the board shows
work-in-progress, and only rotates when idle. **When adding a screen: update `AMBIENT_COUNT`,
the `have[]` initializer (add its data flag), AND the dispatch — all three, or it silently
skips.** (This bit twice: a whitespace-mismatched `str.replace` no-op'd the `have[]` and
dispatch edits while the code still compiled+flashed clean.)

---

## 5. Collectors (host side, mostly M3)

| Script | Pushes | Notes |
|--------|--------|-------|
| `2-hutch-watch/weather.py --puck` | `/weather` + PIGS temp alert | Met Éireann, keyless core |
| `2-hutch-watch/weather.py --pigpush` | `/pig` (GO/WAIT) or `/state` PIGSOON | the **pig-run** brain |
| `3-plane-spotter/spotter.py` | `/plane` | adsb.lol, config in `~/.config/rangerpuck/spotter.env` |
| `1-ranger-puck/tools/fleet-push.sh` | `/fleet` | reads `~/.ranger-memory/config/fleet.conf` |
| `4-sky-siren/siren.py --watch` | `/state` SKY (ISS) | staged countdown |
| `1-ranger-puck/tools/send.sh` | `/state` | generic; honours `PUCK_HOST`, `PUCK_WHO` |

Board IP cache (all callers share it): `~/.ranger-memory/config/rangerpuck.ip`.

### launchd services (M3) — the puck is autonomous, no terminal needed
```
com.ranger.puck-ambient  every 600s  refresh.sh --quiet (weather+fleet+planes)
com.ranger.sky-siren     KeepAlive   siren.py --watch (ISS staged alerts)
com.ranger.pig-run       every 180s  weather.py --pigpush
```
All use `bash -lc` so ~/.profile puts **conda python3** (with sgp4) first — a plain exec hits
/usr/bin/python3 which lacks deps. Logs in `~/.ranger-memory/logs/`.

---

## 6. The fleet — three Claudes + Gemini drive one board

Each machine's Claude Code fires lifecycle **hooks** that POST its state, tagged by `PUCK_WHO`:

| Machine | OS | Hook | Tag |
|---------|----|------|-----|
| M3 | macOS | `hooks/rangerpuck.sh` (bash), symlinked via `tools/install-hooks.sh` | CLAUDE |
| Kali (asus-kali) | Linux | same bash hook (clones this repo, `env.PUCK_WHO=KALI`) | KALI |
| MSI | **Windows** | `hooks/rangerpuck.ps1` (**PowerShell** — no jq/curl/bash; `Invoke-RestMethod`) | MSI |

The bash hook and `send.sh` are **portable** (macOS + Linux): `my_ipv4()`, `mtime()` shims and
`date -r`/`date -d @` fallbacks — BSD vs GNU differ and fail *silently* otherwise. MSI uses the
PowerShell hook precisely to avoid that (and missing jq) on Windows.

**To add a machine:** clone the repo (or copy the right hook), set its `PUCK_WHO`, add a colour
to `whoColour()` + reflash, seed `~/.ranger-memory/config/rangerpuck.ip`, wire the hook into
that machine's `~/.claude/settings.json` (merge, don't clobber).

---

## 7. The PIG RUN feature (the point of the device for David)

Answers *"can I walk the few-hundred-yards to the guinea pigs, feed them, spend ~30 min, and
get back dry?"* — a **45-minute** round trip. `pig_run()` in weather.py stitches two horizons:
OpenWeather minute nowcast (0–60min, sharp) + Met Éireann hourly `dry_gaps` (60min+, planning).

- `PIGGO` green — dry window ≥ 45min → go
- `PIGWAIT` red — none now; shows rain-left + next-GO time
- `PIGSOON` red **PULSING** (`loop()` every 450ms) — dry now but rain within 10min: can't-miss

GO/WAIT are the ambient screen (`/pig`); SOON interrupts (`/state`). Honest limit: catches
*approaching* rain well; a shower forming *overhead* gives ~2–3min only (physics).

---

## 8. Flashing + debugging

```
cd 1-ranger-puck && PORT=/dev/cu.usbmodemXXXX ./tools/flash.sh   # board must be on USB
tools/refresh.sh            # repopulate all screens after a reflash reboot
curl http://<ip>/           # the diagnostic endpoint — trust it over the screen
```
**Rule learned the hard way:** every code `str.replace`/edit MUST assert its target exists, and
you VERIFY THE ACTUAL RENDERED BEHAVIOUR (did the state/screen appear via `GET /`?), never just
"it compiled and flashed." Compiling clean ≠ the edit applied.

---

*Maintained by the Ranger Claudes. Update this when the system changes — one source of truth.*
