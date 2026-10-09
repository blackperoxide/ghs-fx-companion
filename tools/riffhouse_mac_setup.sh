#!/bin/sh
# One-command setup + verification for the Riff House routine on your Mac.
#
#   tools/riffhouse_mac_setup.sh            switch branch, rebuild, set up Python, then verify
#   tools/riffhouse_mac_setup.sh --verify   only run the checks (fast, safe to repeat)
#
# Run it from Terminal (not from a sandboxed tool: it writes to your repo and ~/Music,
# and downloads Python packages).

cd "$(dirname "$0")/.." || exit 1
REPO="$(pwd)"
VENV="$HOME/.riffhouse-venv"
FAILS=0
step() { printf '\n==> %s\n' "$1"; }
pass() { printf '  PASS  %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; FAILS=$((FAILS+1)); }
note() { printf '  note  %s\n' "$1"; }

if [ "${1:-}" != "--verify" ]; then
  step "1/3 Switch to the riffhouse-routine branch"
  git fetch origin && git checkout riffhouse-routine && git pull --ff-only || { echo "Branch step failed; stopping."; exit 1; }

  step "2/3 Rebuild (first run after the update can take several minutes)"
  ./rebuild.sh || note "rebuild.sh reported a problem above (a failed auval check is separate from the build itself)"

  step "3/3 Python tools for importing your songs (one time, ~2-3 GB download)"
  if [ ! -x "$VENV/bin/python" ]; then python3 -m venv "$VENV" || echo "could not create venv"; fi
  "$VENV/bin/python" -m pip install --quiet --upgrade pip
  "$VENV/bin/python" -m pip install "demucs>=4.0.1" "basic-pitch>=0.4.0" torch "librosa>=0.8.0" soundfile numpy \
    || note "pip install had errors above; copy them back to Computer"
fi

step "Verification"
APP="$(find build -maxdepth 4 -name 'GHS FX Companion.app' 2>/dev/null | head -1)"
AU="$(find build -maxdepth 4 -name 'GHS FX Companion.component' 2>/dev/null | head -1)"
VST="$(find build -maxdepth 4 -name 'GHS FX Companion.vst3' 2>/dev/null | head -1)"

[ "$(git branch --show-current)" = "riffhouse-routine" ] && pass "on branch riffhouse-routine" || fail "not on riffhouse-routine (run without --verify)"
[ -f Resources/webui/routine.js ] && pass "Today tab source present" || fail "routine.js missing"

if [ -n "$APP" ]; then
  pass "Standalone app built: $APP"
  EXE="$(find "$APP/Contents/MacOS" -type f | head -1)"
  if [ -n "$EXE" ] && grep -q "rhJournalSave" "$EXE" 2>/dev/null; then pass "build contains the journal code (rhJournalSave)"; else fail "Standalone build does not contain the Today tab; rebuild"; fi
else fail "Standalone app not found under build/"; fi
[ -n "$AU" ]  && pass "AU plugin built"   || fail "AU plugin not found"
[ -n "$VST" ] && pass "VST3 plugin built" || note "VST3 not found (only needed outside Logic)"

mkdir -p "$HOME/Music/GHS/RiffHouse/Journal" && touch "$HOME/Music/GHS/RiffHouse/Journal/.write_test" 2>/dev/null \
  && { rm -f "$HOME/Music/GHS/RiffHouse/Journal/.write_test"; pass "journal folder is writable"; } || fail "cannot write ~/Music/GHS/RiffHouse/Journal"

if [ -x "$VENV/bin/python" ] && "$VENV/bin/python" -c "import demucs, basic_pitch, librosa, soundfile, torch" 2>/dev/null; then
  pass "song importer dependencies load"
else fail "importer dependencies missing or broken (re-run without --verify and read the pip output)"; fi

N=$(find "$HOME/Music/GHS/RiffHouse/SongImports" -maxdepth 2 -name song.json 2>/dev/null | wc -l | tr -d ' ')
[ "$N" -gt 0 ] && pass "$N song(s) imported" || note "no songs imported yet: tools/riffhouse_batch.sh <folder-of-your-songs>"

printf '\n'
if [ "$FAILS" -eq 0 ]; then echo "All automatic checks passed."; else echo "$FAILS check(s) failed. Copy this output back to Computer."; fi
cat <<'MANUAL'

Do these by hand (about 3 minutes):
  [ ] open the app:  open "$(find build -maxdepth 4 -name 'GHS FX Companion.app' | head -1)"
  [ ] Riff House opens on the Today tab, press Start, tap one check-in word
  [ ] Songs tab: import a song folder, load it, set Rate to 0.7, press Play, notes scroll
  [ ] play a note on your guitar: the live note readout at the top changes
  [ ] Today tab: Export program log, then check ~/Music/GHS/RiffHouse/Journal/Program-Log.md exists
MANUAL
exit "$FAILS"
