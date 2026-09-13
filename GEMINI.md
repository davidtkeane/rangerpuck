# Driving the RangerPuck — GEMINI Agent Protocol

This file defines the exact protocol for Gemini to report status to the physical Waveshare ESP32-C6 RangerPuck on David's desk.

---

## 1. Environment & Identity

* **Identity:** `GEMINI` (displays in **blue**)
* **Script:** `~/esp32-projects/1-ranger-puck/tools/send.sh`

```bash
export PUCK_WHO=GEMINI
~/esp32-projects/1-ranger-puck/tools/send.sh <STATE> "<line1>" "<line2>" GEMINI
```

---

## 2. ⚠️ THE GOLDEN RULE: Send `APPROVE` BEFORE any gated action, not after!

Unlike Claude Code (which has an automated daemon notification hook that fires on suspend), **Gemini has no harness hook.** Gemini can ONLY speak to the board while actively executing.

The instant the harness pauses and pops up an *"Approve this action?"* prompt to David, **Gemini is suspended.** It cannot execute code or send network packets while paused waiting for a keypress.

### How to prevent silent deadlocks:
1. **Send `APPROVE` BEFORE the gated action:**
   ```bash
   ~/esp32-projects/1-ranger-puck/tools/send.sh APPROVE "needs a yes" "prompting" GEMINI
   ```
2. **Execute the tool / edit / command.**
3. **Overwrite immediately upon resuming:**
   ```bash
   ~/esp32-projects/1-ranger-puck/tools/send.sh RUNNING "applying" "" GEMINI
   ```

> [!IMPORTANT]
> A false amber that clears a second later costs nothing. But a **silent block** means David sits in another tab unaware, completely defeating the purpose of the physical desk puck.

---

## 3. States

| State | Colour | When to use |
| :--- | :--- | :--- |
| **`APPROVE`** | 🟠 Amber | **BEFORE any action that will prompt David for a yes.** |
| **`WAITING`** | 🩵 Teal | Turn complete, ball is in David's court, nothing blocked. |
| **`THINKING`**| 🔵 Blue | Reading files, searching, reasoning. |
| **`RUNNING`** | 🔵 Blue | Executing a tool or running a terminal command. |
| **`DONE`**    | 🟢 Green| Task or build successfully completed. |
| **`ERROR`**   | 🔴 Red  | A command or task failed. |
| **`IDLE`**    | ⚪ Dim  | Standby / inactive. |

---

## 4. Formatting & Line Lengths

* Keep `line1` and `line2` short: **Maximum 14 characters** per line.
* Examples: `"file edit"`, `"compiling"`, `"over to you"`.

---

## 5. Hard Safety Rules

1. **Non-blocking:** Always run calls asynchronously or quickly without stalling the main work.
2. **No Polling:** Do not spam updates faster than 1 call per second.
3. **Privacy:** Never send API keys, passwords, or sensitive paths to the screen.
4. **Firmware Integrity:** **Do NOT reflash or overwrite the ESP32-C6 firmware.**
