#!/bin/sh
# Import every song in a folder into Riff House song folders, one after another.
# Run this BEFORE you go somewhere without good wifi or power: Demucs + Basic Pitch
# take a few minutes per song on an M1, and you only need to do it once per song.
#
# Usage:  tools/riffhouse_batch.sh ~/Music/MySongs
#
# Skips songs that are already done, so it is safe to run again after adding more.
# Output lands in ~/Music/GHS/RiffHouse/SongImports/<song>/ ; in the plugin use
# Riff House -> Songs -> "Import song folder..." and pick each one.

set -u
SRC="${1:-}"
if [ -z "$SRC" ] || [ ! -d "$SRC" ]; then
  echo "Usage: $0 <folder-with-mp3-or-wav-files>" >&2
  exit 1
fi
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HOME/Music/GHS/RiffHouse/SongImports"
ok=0; skipped=0; failed=0

for f in "$SRC"/*.mp3 "$SRC"/*.wav "$SRC"/*.m4a "$SRC"/*.flac "$SRC"/*.aif "$SRC"/*.aiff; do
  [ -f "$f" ] || continue
  name="$(basename "$f")"; title="${name%.*}"
  slug="$(printf '%s' "$title" | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9]\{1,\}/-/g; s/^-//; s/-$//')"
  if [ -f "$OUT/$slug/song.json" ]; then
    echo "skip  $title (already imported)"; skipped=$((skipped+1)); continue
  fi
  echo "start $title"
  if python3 "$HERE/riffhouse_import.py" "$f" --title "$title" --device auto; then
    ok=$((ok+1)); echo "done  $title"
  else
    failed=$((failed+1)); echo "FAILED $title (keep going)" >&2
  fi
done
echo "Finished: $ok imported, $skipped skipped, $failed failed."
