#!/usr/bin/env bash
# Give Gemini (or any agent) the ability to drive the puck.
# Run once; prints the line to paste into that agent's session.
P="$HOME/esp32-projects/1-ranger-puck"
echo
echo "  Paste this into Gemini:"
echo
echo "    Read $P/AGENTS.md and use it to show me what you are doing."
echo "    Your name is GEMINI. Set PUCK_WHO=GEMINI first."
echo
echo "  Then Gemini drives the board with:"
echo "    export PUCK_WHO=GEMINI"
echo "    $P/tools/send.sh THINKING \"searching\""
echo
