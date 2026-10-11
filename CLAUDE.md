# Handoff: GHS FX Companion, Studio launchpad branch

Read this first. Owner: Shawn (musician, combat veteran with TBI and executive dysfunction).
His product goal: one calm dashboard that helps him finish songs. Suno export -> DAW (Logic / Luna / Ableton)
-> rebuild with his own instruments -> mix -> master -> perform. Later, a quiet assistant that acts like a
producer / mix engineer without nagging.

## Branch and rules
- Work on `studio-launchpad` (off `riffhouse-routine`). Do not touch `main`. Open a PR when asked.
- Read `docs/STUDIO.md` (what exists, folders, design rules, planned order, known limits).
- Shawn works best with small, digestible, sequential steps. Do one milestone at a time and report plainly.
- UI must stay calm: no pop-ups, sounds, flashing, streaks or guilt. Assistant suggests, never changes a session without a click.
- Never delete or overwrite his files. Templates are copied, never edited; imported originals go to Drop/Done.
- Do not claim something works unless you ran it. Say what you could not test.

## State (as of 2026-10-11)
Built and pushed: Studio tab (template launcher, Suno Drop folder import, six song stages with checklists,
rule-based next-step card with Quiet/Nudge/Coach, "I'm stuck").

Verified in a Linux sandbox only:
- `Source/StudioEngine.*` compiled against JUCE 8.0.4 core and run on fake data (zip import, session copy, JSON).
- `addStudioFunctions` in `PluginEditor.cpp` syntax-checked against JUCE headers.
- UI exercised in headless Chromium with a mock bridge. `node tools/test_studio_lattice.js` passes (17).

NOT verified (do this first, on macOS):
1. `./rebuild.sh` builds AU + VST3 + Standalone with the new files and BinaryData names
   (`studio_js`, `studiolattice_js`, `studio_css`).
2. Studio tab renders in the real JUCE webview and the `st*` native calls work.
3. Add a real Logic `.logicx` template (a package) and a `.als`; confirm the copy opens in the right DAW.
   Check the file picker can select packages, and that a folder-style template (Luna) copies correctly.
4. Drop a real Suno Multitrack zip; confirm stems import (Suno exports are 32-bit/48 kHz WAV).
5. The window is opened inside Logic as a plugin: confirm file picker and "open in default app" behave there;
   the Standalone target is the intended home for the launchpad.

## Next milestones (in order)
1. Fix whatever step 1-5 above reveals.
2. Local mix analysis from stems (reuse `ToneAnalyzer`): loudness, spectral balance, stereo width, masking. Numbers only.
3. Optional advice layer: turns those numbers into 1-2 sentences. Off by default, user's own API key, never send audio.
4. Attach per-stem chain suggestions (existing recommender) to the Mix steps.
5. Setting knobs inside hosted plugins from a suggestion (hard; last).
6. Auto-detect BPM / key on import.

## Open questions for Shawn
- Which templates and DAWs should be default buttons (Logic, Luna, Ableton)?
- Does WAX load Suno Studio and keep a sign-in inside a DAW track? (Untested; he runs the test on his machine.)
- Meaw:Chain / Meaw:Assist purchase planned for November sale; use it only as a comparison, not a dependency.
