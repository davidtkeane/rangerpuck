# 🎖️ RangerPuck

**A €20 screen that shows what your AI agents are doing.**

Claude wants a yes and you're in another tab? The board goes amber. Gemini's thinking?
Blue, with its name on it. Nothing happening? It shows your machines, the weather, or the
aircraft overhead.

Built on a **Waveshare ESP32-C6-LCD-1.47** (~€20). No cloud, no account, no subscription.

## Why

The problem this solves is small and real: an agent finishes, or gets blocked, and sits
there waiting while you're looking at something else. A terminal you aren't watching is a
notification you don't get.

## Any agent can drive it

One HTTP POST. [`AGENTS.md`](AGENTS.md) is written to be handed **to the agent** —
Claude, Gemini, Ollama, anything with a shell:

```bash
./tools/send.sh APPROVE "needs a yes" "2m 18s" GEMINI
```

Each agent gets its own colour tag, and the board keeps **a slot per agent**.

## The rule that makes it work

**Amber only ever means "a human is blocked."**

If it also fires for *"the session went quiet"*, you learn to ignore it within a week —
and then it's useless on the day it matters. `WAITING` (teal) is for "I'm done, over to
you". The board enforces this: **a blocked agent outranks everything**, including a more
recent update from a different agent. The plane radar, the weather and another agent's
progress are all ambient; a human being blocked is not.

An alert that cries wolf is worse than no alert.

## States

| state | colour | meaning |
|-------|--------|---------|
| `APPROVE` | 🟠 amber | **blocked, needs you** — never expires |
| `WAITING` | 🩵 teal | finished, your move, nothing blocked |
| `THINKING` / `RUNNING` | 🔵 blue | working |
| `DONE` | 🟢 green | complete |
| `ERROR` | 🔴 red | failed |
| `SYNC` | 🟣 purple | a long job, with its stage |
| `RADAR` | — | aircraft overhead |
| `IDLE` | ⚪ dim | falls back to the fleet view |

## It tidies up after itself

Agents crash. Sessions get killed. So the **board expires its own state**: 3 minutes for
settled states, 8 for working ones. `APPROVE` never expires, because someone may genuinely
be blocked while you're out of the room.

No agent can leave the display asserting something stale.

## Build

```bash
brew install arduino-cli
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32          # 3.x — 2.x does NOT support the C6
arduino-cli lib install "Adafruit ST7735 and ST7789 Library" "Adafruit GFX Library" \
  "Adafruit NeoPixel" ArduinoJson

cp RangerPuck/secrets.h.example RangerPuck/secrets.h   # your Wi-Fi (2.4GHz — the C6 has no 5GHz)
./tools/flash.sh
```

Pins are confirmed from Waveshare's own wiki in [`docs/PINOUT.md`](docs/PINOUT.md).
**Do not guess them** — there is a near-identical Touch variant.

## Tools

```
tools/send.sh       push a state
tools/rotate.sh     landscape ⇄ portrait (remembered)
tools/mascot.sh     helmet ⇄ cat (remembered)
tools/flash.sh      compile + upload
tools/monitor.sh    serial at 115200
tools/make-logo.py  any 2-tone PNG → a 1-bit header you can tint
tools/cheat.sh      the cheat sheet
```

The logo is stored **1-bit on purpose** — that's what lets it take the state colour, so the
badge itself goes amber when you're needed. A full-colour image would be 40× bigger and
stuck one colour.

## Security — read this before putting it on a network

The board runs an **open, unauthenticated HTTP server**. Anyone on your LAN can read what
it shows and change it. There is no password and no TLS, by design — it is a status light,
not a secure system.

Don't put it on a network you don't trust, and don't display anything you wouldn't write
on a whiteboard. Agents are told the same in `AGENTS.md`.

## Finding the board

`rangerpuck.local` over mDNS, but many routers won't bridge multicast between Ethernet and
Wi-Fi, so `send.sh` falls back to a cached IP and then sweeps the subnet, re-caching what
answers. **No IP is hard-coded anywhere** — DHCP moves them.

## Things that cost me hours

- `arduino-cli` inserts function prototypes **above the first function definition** —
  declare every type above every function, or you get `'X' does not name a type` on a line
  you didn't touch.
- Wi-Fi **reason code 204 = wrong password**, 201 = AP not found. Print the code and
  "wifi doesn't work" becomes a one-line diagnosis.
- Router passwords printed on the sticker are shown **in quotes**. The quotes aren't part
  of the password.
- Size-1 text is unreadable on a 1.47" panel. Body text size 2 minimum.
- A duplicated `secrets.h` per sketch folder drifts silently and looks exactly like
  "my editor isn't saving".

## Companions

- [between-the-showers](https://github.com/davidtkeane/between-the-showers) — Irish weather
- [plane-spotter](https://github.com/davidtkeane/plane-spotter) — aircraft overhead

MIT. Use it for anything.
