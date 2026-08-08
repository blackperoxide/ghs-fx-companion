#!/bin/sh
# Pull, reconfigure, build (AU + VST3 + scanner), clear the AU cache, and
# validate the AU - the whole test loop from a stopped state, in one command.
# macOS only (AU/auval/Logic's AudioComponentRegistrar don't exist elsewhere).
#
# Usage: ./rebuild.sh

set -e
cd "$(dirname "$0")"

echo "==> git pull"
git pull

echo "==> cmake configure"
cmake -S . -B build

echo "==> cmake build (AU + VST3 + scanner)"
cmake --build build

echo "==> clearing AU cache"
killall -9 AudioComponentRegistrar 2>/dev/null || true
rm -rf ~/Library/Caches/AudioUnitCache

echo "==> auval"
auval -v aumf Ghs1 Ghst
