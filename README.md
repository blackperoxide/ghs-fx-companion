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

## Riff House: daily routine and journal

See [docs/PRACTICE-ROUTINE.md](docs/PRACTICE-ROUTINE.md) for the Today tab (daily check-in, quests, weekly goals, practice log, journal) and `tools/riffhouse_batch.sh` for importing a folder of your own songs in one go.

## Console 1 controller (in progress)

The Console 1 tab lets a Softube Console 1 Mk I control the plugins in the Rack: pick a knob on the on-screen
map, press Assign or Learn, or let "Suggest a layout" map a whole EQ or compressor. Each plugin's knob layout can
be saved and comes back by itself next time. Not yet tested on the real hardware or on macOS.

### Console 1 home test (about 10 minutes)

Do these in order. If a step fails, stop there: the troubleshooting list in
[docs/CONSOLE1.md](docs/CONSOLE1.md) follows the same order.

1. Get the branch and build it:
   ```
   git fetch
   git checkout console1-controller
   ./rebuild.sh
   ```
   If the build fails, copy only the first error.
2. Open Softube's Console 1 On-Screen Display, open its menu and turn on
   "Use Mk I/II as MIDI Controllers".
3. Open GHS in your DAW and go to the Console 1 tab. The top should say
   "Console 1 connected". If it doesn't, pick the port under Setup > Ports.
4. Open the MIDI Monitor pane and turn the Comp Threshold knob. You should see
   CC 47 with changing values.
5. Load any EQ in Rack slot 1. On the Map pane: slot 1, Suggest, EQ section, Apply.
6. Turn Low Gain on the hardware. The plugin's band 1 gain should move.
7. Move that gain with the mouse in the plugin. The ring on the hardware should follow.
8. Press "Save as this plugin's profile". Remove the plugin and load it again:
   the knobs should come back by themselves.

The same steps are in the tab's Setup pane.
