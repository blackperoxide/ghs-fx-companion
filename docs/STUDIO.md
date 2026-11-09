# Studio launchpad

The first tab in GHS FX Companion. One screen to open a DAW template, bring a Suno export in,
and see the single next step for a song. Quiet by default, fully offline.

## What it does today

| Area | Behaviour |
|---|---|
| Start a session | Add your Logic / Ableton / Luna (or any) template once. **Open** copies it to `Studio/Sessions/<date>-<name>/`, writes `SESSION-NOTES.md` (tempo, key, stems folder) and opens the copy. The original template is never edited. **Edit** opens the original. |
| Suno drop folder | Drop a Suno Multitrack `.zip` into `Studio/Drop/`. It is unzipped (audio and MIDI only, macOS junk skipped) into `Songs/<slug>/stems/` and becomes a song. A single audio file becomes a song flagged "needs stems". Originals move to `Drop/Done/`. The tab checks every 4 s while visible; files still downloading are left alone. |
| Songs | Six stages: Suno, Stems, Rebuild, Mix, Master, Perform. Each has a short checklist. Some steps tick themselves when the app can see they are true (stems present, BPM set, session started). You can always tick or un-tick by hand and jump to any stage. |
| Next step | One card. Quiet = title only. Nudge = adds a calm "welcome back" after 3+ days away. Coach = adds why it matters and a time box. **I'm stuck** shows the 2-minute version of the step. |

Folders (all plain files, safe to back up or edit):

```
~/Music/GHS/Studio/
  studio.json  studio.backup.json     templates + settings
  Drop/  Drop/Done/
  Songs/<slug>/song.json  stems/  source/
  Sessions/<date>-<name>/<template copy>  SESSION-NOTES.md
```

## Design rules

- Nothing pops up, makes a sound, or resets. No streaks. Welcome-back never mentions what was missed.
- The assistant suggests; it never changes a session or a stage without a click.
- Cueing is rule-based (`Resources/webui/studio-lattice.js`), no AI, no network, unit-tested:
  `node tools/test_studio_lattice.js`.
- User text (titles come from file names) is never put into the page as HTML.

## Code map

- `Source/StudioEngine.*` file work only (juce_core). Thin native wrappers in `PluginEditor.cpp` (`addStudioFunctions`, `st*`).
- `Resources/webui/studio-lattice.js` stages x steps data and `nextStep()` / `stuckStep()`.
- `Resources/webui/studio.js`, `studio.css` the screen.

## Not built yet (planned order)

1. **Local mix analysis**: loudness, spectral balance, stereo width and masking between stems, using the existing `ToneAnalyzer`. Numbers only, computed on the machine.
2. **Advice layer**: turns those numbers into one or two sentences. Optional, off by default, own API key; only numbers leave the machine, never audio.
3. **Per-stem chain suggestions** from the Rack's recommender, attached to the Mix steps.
4. Setting knobs inside hosted plugins from a suggestion (the hard part; last).
5. Auto-detect BPM / key on import.

## Known limits

- Suno has no official import path into Logic or Luna that this app can drive; the hand-off is a prepared session folder, then drag stems in.
- Only audio and MIDI inside a zip are imported. Project files or other types are ignored.
- Desktop JUCE build is untested in CI here; the file engine was built and run against real JUCE core, and the web UI was exercised in headless Chromium with a mock bridge.

## WAX (hear Suno inside the DAW) - test before relying on it

1. WAX on a track as an Instrument. 2. Open Suno in it and try to sign in (Suno recommends Chrome). 3. Press play, confirm audio and meters, session at 48 kHz. 4. Record 30 s and listen for dropouts.
