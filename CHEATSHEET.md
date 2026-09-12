# 🎖️ RangerPuck — cheat sheet

## Send a state

```bash
./tools/send.sh <STATE> "<line1>" "<line2>" <WHO>
export PUCK_WHO=CLAUDE          # or set it once
```

| state | colour | meaning |
|-------|--------|---------|
| `APPROVE` | 🟠 amber | **blocked, needs a human** — never expires |
| `WAITING` | 🩵 teal | finished, their move, nothing blocked |
| `THINKING` / `RUNNING` | 🔵 blue | working |
| `DONE` | 🟢 green | complete |
| `ERROR` | 🔴 red | failed |
| `SYNC` | 🟣 purple | a long job |
| `IDLE` | ⚪ dim | falls back to the fleet view |

Agent tags: `CLAUDE` orange · `GEMINI` blue · `OLLAMA` green · `QWEN` magenta · `WEATHER` cyan.

## Tools

```
tools/send.sh       push a state
tools/rotate.sh     landscape ⇄ portrait (remembered)
tools/mascot.sh     helmet ⇄ cat (remembered)
tools/flash.sh      compile + upload
tools/monitor.sh    serial at 115200
tools/make-logo.py  any 2-tone PNG → a tintable 1-bit header
tools/cheat.sh      this page
```

## Raw HTTP

```bash
curl -s http://rangerpuck.local/                        # status
curl -X POST -d '{"state":"DONE","who":"CLAUDE"}' \
     http://rangerpuck.local/state
curl http://rangerpuck.local/rotate                     # cycle orientation
curl http://rangerpuck.local/mascot                     # helmet ⇄ cat
```

## When it misbehaves

| symptom | look here first |
|---------|-----------------|
| black screen, LED alive | **backlight pin (GPIO 22)**, then SPI |
| won't join Wi-Fi | reason **204 = wrong password**, 201 = AP not found. **2.4GHz only** |
| `.local` won't resolve | routers often won't bridge multicast; `send.sh` falls back to a cached IP |
| `'X' does not name a type` | arduino-cli puts prototypes above the first function — declare types above every function |
| edits do nothing | check you're editing the file the build reads |

Pins: [`docs/PINOUT.md`](docs/PINOUT.md). Agents: [`AGENTS.md`](AGENTS.md).
