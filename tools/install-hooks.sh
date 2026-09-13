#!/usr/bin/env bash
# Link the Claude Code hook into place. The repo holds the real file; ~/.claude
# gets a symlink, so the hook cannot drift from the firmware it drives and a
# reinstall or a move to another machine cannot lose it.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)/hooks/rangerpuck.sh"
DST="$HOME/.claude/hooks/rangerpuck.sh"
mkdir -p "$(dirname "$DST")"
if [ -e "$DST" ] && [ ! -L "$DST" ]; then
  cp "$DST" "$DST.replaced-$(date +%Y%m%d%H%M%S)"
  echo "kept your existing file as $DST.replaced-*"
fi
ln -sfn "$SRC" "$DST"
chmod +x "$SRC"
echo "✅ $DST -> $SRC"
echo "   settings.json already calls it; nothing else to change."
