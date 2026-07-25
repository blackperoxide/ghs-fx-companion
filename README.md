# GHS FX Companion

A JUCE-based AU/VST3 plugin that hosts your other real plugins inside itself and
runs actual audio through them — the same category as Kilohearts Snap Heap,
Waves StudioRack, or Safari Audio's Meaw:Chain.

## Where this is right now (Milestone 1)

This proves the *hosting mechanism* only: scan your installed VST3/AU plugins,
pick one from a plain list, load it, and pass real audio through it inside
Logic (or any DAW). No chain building, no AI matching yet — that comes next,
once this works for real on your machine.

## Building on your Mac

You'll need Xcode (for the AU build) and CMake:

```
brew install cmake
```

Then, from this folder:

```
cmake -B build -G Xcode
cmake --build build --target GHSFXCompanion_AU --config Debug
```

The first configure step downloads JUCE itself (~500MB via git) — that's normal,
only happens once.

The built plugin lands in:

```
build/GHSFXCompanion_artefacts/Debug/AU/GHS FX Companion.component
```

Copy (or symlink) that into `~/Library/Audio/Plug-Ins/Components/`, then
relaunch Logic (or run `killall -9 AudioComponentRegistrar` first if Logic
doesn't pick it up right away) and it should show up under your Audio Units
as "GHS FX Companion."

## Testing it

1. Insert it on a channel strip in Logic.
2. Click "Scan for Plugins" — it should list your installed VST3/AU plugins.
3. Pick one, click "Load Selected" (or double-click it).
4. Its own GUI should appear embedded below the list, and audio passing
   through the channel should now be processed by that plugin.

Whatever happens — works, crashes, doesn't find plugins, wrong sound — tell
me exactly what you see and I'll fix it from there.
