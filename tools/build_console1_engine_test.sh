#!/usr/bin/env bash
# Builds tools/test_console1_engine.cpp against JUCE on Linux (CI / sandbox), no plugin formats.
# Usage: JUCE_DIR=/path/to/JUCE tools/build_console1_engine_test.sh && /tmp/c1engine/test
# Needs: g++, libasound2-dev, libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libfreetype-dev, libfontconfig1-dev
set -euo pipefail
cd "$(dirname "$0")/.."
JUCE_DIR="${JUCE_DIR:-../juce}"
OUT=/tmp/c1engine
mkdir -p "$OUT"
MODS="juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_audio_basics juce_audio_devices juce_audio_processors"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_PLUGINHOST_VST3=0 -DJUCE_PLUGINHOST_LADSPA=0 -DJUCE_PLUGINHOST_LV2=0 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=1 -DJUCE_JACK=0 -DJUCE_MODAL_LOOPS_PERMITTED=1 -DJUCE_DEBUG=1 -DDEBUG=1"
for m in $MODS; do DEFS="$DEFS -DJUCE_MODULE_AVAILABLE_$m=1"; done
INC="-I$JUCE_DIR/modules $(pkg-config --cflags freetype2)"
FLAGS="-std=c++17 -O0 -g $DEFS $INC"
objs=()
for m in $MODS; do
  o="$OUT/$m.o"
  if [ ! -f "$o" ]; then echo "compiling $m"; g++ $FLAGS -c "$JUCE_DIR/modules/$m/$m.cpp" -o "$o" & fi
  objs+=("$o")
done
for extra in juce_graphics/juce_graphics_Harfbuzz.cpp juce_core/juce_core_CompilationTime.cpp; do
  o="$OUT/$(basename "$extra" .cpp).o"
  if [ ! -f "$o" ]; then g++ $FLAGS -c "$JUCE_DIR/modules/$extra" -o "$o" & fi
  objs+=("$o")
done
if [ ! -f "$OUT/sheenbidi.o" ]; then gcc $DEFS $INC -c "$JUCE_DIR/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$OUT/sheenbidi.o" & fi
objs+=("$OUT/sheenbidi.o")
wait
g++ $FLAGS -Wall -Wextra -c Source/Console1Engine.cpp -o "$OUT/Console1Engine.o"
g++ $FLAGS -c tools/test_console1_engine.cpp -o "$OUT/test.o"
g++ "$OUT/test.o" "$OUT/Console1Engine.o" "${objs[@]}" -o "$OUT/test" -lasound -lfreetype -lfontconfig -ldl -lpthread -lrt
g++ $FLAGS -c tools/console1_ui_server.cpp -o "$OUT/ui_server.o"
g++ "$OUT/ui_server.o" "$OUT/Console1Engine.o" "${objs[@]}" -o "$OUT/ui_server" -lasound -lfreetype -lfontconfig -ldl -lpthread -lrt
echo "built $OUT/test and $OUT/ui_server"
