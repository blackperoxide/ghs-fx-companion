# GHS FX Companion

A JUCE-based AU/VST3 plugin that hosts your other real plugins inside itself and
runs actual audio through them — the same category as Kilohearts Snap Heap,
Waves StudioRack, or Safari Audio's Meaw:Chain.

## Where this is right now (Milestone 1)

This proves the *hosting mechanism* only: pick an installed VST3/AU plugin
from a list, load it, and pass real audio through it inside Logic (or any
DAW). No chain building, no AI matching yet — that comes next, once this
works for real on your machine.

There are two separate pieces, on purpose:

- **GHS FX Companion** — the actual AU/VST3 plugin you load in Logic.
- **GHS FX Companion Scanner** — a small standalone app (not a plugin) you
  run on its own, outside Logic, to build the plugin list. This split exists
  because scanning (briefly instantiating each installed plugin to query it)
  is genuinely unsafe to do from inside a plugin that's itself running
  inside Logic — if any one of your installed plugins misbehaves during that
  probe, it takes the whole DAW down with it (this is exactly what happened
  in testing before this split existed). The Scanner is the isolation
  boundary: if a plugin crashes it, only the small standalone app dies —
  Logic never even knows. The plugin loaded in Logic only ever *reads* the
  list the Scanner wrote; it never scans anything itself.

## Building on your Mac

You'll need CMake (Xcode Command Line Tools are enough — you don't need the
full Xcode app just to build from the command line):

```
brew install cmake
```

Then, from this folder:

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target GHSFXCompanion_AU --config Debug
cmake --build build --target GHSFXScanner --config Debug
```

The first configure step downloads JUCE itself (~500MB via git) — that's normal,
only happens once.

## Installing and running the Scanner (do this first)

```
./build/GHSFXScanner_artefacts/Debug/GHSFXScanner
```

Run it directly from a Terminal — it prints progress, scans your installed
VST3/AU plugins, and writes the results to
`~/Library/Application Support/GHS FX Companion/KnownPlugins.xml`. Re-run it
any time you install new plugins.

## Installing the plugin

```
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R "build/GHSFXCompanion_artefacts/Debug/AU/GHS FX Companion.component" ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar
```

Then open Logic and it should show up under **Audio Units → Grand Healing
Studio → GHS FX Companion** (if it doesn't appear right away, quit and
reopen Logic once — first-time AU scans sometimes need that).

## Testing it

1. Insert it on a channel strip in Logic (mono or stereo channels both work).
2. Its list should already show whatever the Scanner found last time you ran
   it. Click "Refresh Plugin List" if you ran the Scanner again since opening
   Logic.
3. Pick one, click "Load Selected" (or double-click it).
4. Its own GUI should appear embedded below the list, and audio passing
   through the channel should now be processed by that plugin.

Whatever happens — works, crashes, doesn't find plugins, wrong sound — tell
me exactly what you see and I'll fix it from there.
