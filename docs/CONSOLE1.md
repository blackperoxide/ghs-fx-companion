# Console 1 controller (branch `console1-controller`)

Turns a Softube Console 1 Mk I into hands-on knobs for the plugins hosted in GHS FX Companion's Rack:
UAD, FabFilter, Plugin Boutique, anything GHS can load. This is not Softube's own integration. GHS reads the
plain MIDI CCs that the Console 1 sends when "Use as MIDI controller" is on, and maps them to hosted plugin
parameters. The CC layout comes from Ardour's open-source Console 1 surface
(https://github.com/Ardour/ardour/tree/master/libs/surfaces/console1).

## What it does

- Console 1 tab with four panes: Map, Bindings, Setup, MIDI Monitor.
- Map: an on-screen Mk I. Pick a control, then press Assign on a parameter, or press Learn and move a real knob.
  "Suggest a layout" guesses a mapping from parameter names (Pro-Q style numbered bands, SSL style named bands,
  compressors, gates, 1176 style Input/Output). Nothing is bound until you press Apply.
- A simulator (tick "Simulator") lets you drag the on-screen knobs. It goes through the same code path as the
  hardware, so everything except the physical MIDI port can be tried without the Console 1.
- Track buttons 1-8 pick the rack slot under the knobs. Page Up / Page Down step through loaded slots.
- The same knob can be mapped in several slots. The focused slot wins; if none of them is focused, the lowest slot
  wins. Several mappings of one knob inside one slot move together (simple macro).
- Knob modes: Absolute, Pickup (no jump until the knob passes the value), Relative with a Fine setting, Toggle and
  Momentary for buttons. Every mapping has a min/max range and Invert.
- Ring and LED feedback: when the plugin moves (mouse, automation, preset), GHS sends the value back so the ring
  follows. It waits 250 ms after you touch a knob so it doesn't fight your hand.
- Per-plugin profiles: "Save as this plugin's profile" writes one small JSON file per plugin. Next time that
  plugin is loaded into any slot, its knobs come back by themselves. The profile file is named after the plugin's
  maker and name. The AU and VST3 of a plugin share it only if both formats report the same maker and name; check
  the profiles folder if one format doesn't pick it up.
  The previous version is kept as `<name>.previous.json`.
- The mapping is also saved inside the DAW session.

## Files

| File | Job |
|---|---|
| `Source/Console1Mapping.h` | Pure value math: CC to parameter, ranges, pickup, relative, toggle, steps. No JUCE. |
| `Source/Console1Engine.h/.cpp` | Control table, MIDI port (DeviceHub), bindings, learn, feedback, profiles, session state, UI bridge (`invoke`). |
| `Source/PluginProcessor.*` | Owns the engine; tells it when slots load, unload or swap; saves `Console1State` in the session. |
| `Source/PluginEditor.*` | Registers the `c1*` native functions (all routed through `Engine::invoke`) and emits `c1Live` at 30 Hz. |
| `Resources/webui/console1.js / .css` | The tab. |
| `Resources/webui/console1-automap.js` | "Suggest a layout" name heuristics. |
| `tools/test_console1_mapping.cpp` | Value math tests. |
| `tools/test_console1_automap.js` | Auto-map tests. |
| `tools/test_console1_engine.cpp` | Real engine + fake plugins: bindings, modes, learn, focus, host MIDI, profiles, session, bridge. |
| `tools/console1_ui_server.cpp`, `tools/console1-test.html` | Runs the real engine behind the real web UI in a normal browser. |
| `tools/build_console1_engine_test.sh` | Builds the last two on Linux. |

Settings and profiles live in `~/Library/Application Support/GHS FX Companion/`:
`Console1Settings.json` (chosen ports) and `Console1Profiles/<Manufacturer - Plugin>.json`.

## Hardware CC map (Mk I, MIDI channel 1)

From Ardour's `console1.h`. Not yet checked on Shawn's unit: the MIDI Monitor tab is how to check it.

| Section | Controls (CC number) |
|---|---|
| Input | Input gain 107, Low cut 103, High cut 105, Filter to comp 61, Phase 108, Ext sidechain 17 |
| Shape | Shape on 53, Hard gate 59, Gate 54, Gate release 56, Sustain 55, Punch 57 |
| EQ | EQ on 80; High gain/freq/shape 82/83/65; Hi-mid 85/86/87; Lo-mid 88/89/90; Low 91/92/93 |
| Compressor | Comp on 46, Order 14, Threshold 47, Ratio 49, Attack 51, Release 48, Parallel 50 |
| Drive | Drive 15, Character 18 (these two names may be swapped; check on the hardware) |
| Output | Volume 7, Pan 10, Mute 12, Solo 13 |
| Navigation | Preset/Shift 58, Mode 104, Display on 102, Page up 96, Page down 97, Track copy 120, Track group 123 |
| Track buttons | 1-20 = CC 21-40 (GHS uses 1-8 for rack slots) |

Assumptions (from Ardour and user reports, still to confirm):
1. Knobs send absolute values 0-127 (about 3 per detent step) and the ring follows when the same CC is sent back.
2. Buttons send 127 on press and 0 on release; sending 127/0 back lights/clears the LED.
3. All on channel 1.

If any of these is wrong, the fix is small: the table is `controls()` in `Console1Engine.cpp`, and modes are
per mapping (switch a knob to Relative if it turns out to send +/- steps).

## Home test (about 10 minutes)

The same list is in the Setup pane.

1. Open Softube's Console 1 On-Screen Display, open its menu, turn on "Use Mk I/II as MIDI Controllers".
2. Open GHS in your DAW. The top of the Console 1 tab should say "Console 1 connected".
3. Open MIDI Monitor and turn Comp Threshold. You should see CC 47 with changing values.
4. Load any EQ in Rack slot 1. Map tab, slot 1, Suggest, EQ section, Apply.
5. Turn Low Gain on the hardware. The plugin's band 1 gain should move.
6. Move that gain with the mouse in the plugin. The ring on the hardware should follow.
7. Save as this plugin's profile. Remove the plugin, load it again: the knobs come back by themselves.

## Troubleshooting (work down the list)

1. **Doesn't build.** Read the first error only. Most likely: a BinaryData name (`console1_js`,
   `console1automap_js`, `console1_css`) or a JUCE API difference on macOS. Paste the first error to the assistant.
2. **Tab missing or blank.** Right-click > Inspect (Standalone) or check the console for a script error.
   `console1.js` must load after `app.js` and `console1-automap.js`.
3. **Status says "Console 1 not found".**
   - Is the Softube MIDI controller option on (step 1)? Without it the Console 1 sends nothing over MIDI.
   - Setup > Ports: open the list. Is there a port with "Console 1" in its name? If it's named differently, pick it
     by hand; the choice is saved.
   - Another app (Softube's own software, a DAW control surface) may hold the port exclusively on some systems.
     Close it, wait 2 seconds (GHS retries).
4. **Connected, but MIDI Monitor stays at 0 when you turn knobs.** The port exists but nothing arrives:
   wrong port picked (there may be two), or the controller option is off. Try "DAW track MIDI" as the source
   and route the Console 1 to the GHS track in the DAW, to see whether the DAW receives it.
5. **Monitor shows messages but the plugin doesn't move.**
   - Bindings pane: is the mapping there, and does "Now" show a value (not "(missing)")?
   - Is the right slot focused? A knob mapped in two slots only drives the focused one.
   - Pickup mode: turn past the current value first.
   - Check the CC in the Monitor matches the table above; if not, Learn picks up whatever arrives.
6. **Plugin moves, ring doesn't follow.** Needs the "To Console 1" output port (Setup > Ports) and "Rings and LEDs
   follow the plugin" on. If the ring still doesn't follow, assumption 1 is wrong: use Pickup mode for those knobs.
7. **Ring jumps around or fights the knob.** Switch that knob to Relative, or Pickup.
8. **Two GHS windows: only one reacts.** By design. The window you opened last has control; "Take control" moves it.
9. **Knob works in one DAW but not another (Logic AU).** Use the default "Console 1 directly" source. Logic doesn't
   route MIDI to audio effect AUs in a simple way.

## Known limits

- Echo guard: if the Console 1 sends back a value GHS just sent it (within 120 ms), GHS ignores it, so ring
  updates can't nudge a parameter. Not testable without the hardware.
- Unplug/replug: GHS checks every 2 seconds whether the open port still exists, closes it if not, and reopens
  when the Console 1 comes back. Not testable without the hardware.

- Parameter moves made from the Console 1 are not exposed as DAW automation of GHS (hosted parameters aren't
  published to the host). Automating inside the hosted plugin works if the DAW sees it; otherwise record with GHS's
  own tools later.
- GHS doesn't drive the Console 1's own display or Softube's On-Screen Display. The tab's "last touched" line and
  the on-screen map do that job.
- An existing race in the Rack (plugins swapped on the message thread while audio runs) is unchanged by this work.
- Loading a chain preset or session that contains Console 1 mappings replaces all mappings, including those of
  slots the preset doesn't touch. Older presets without mappings leave them alone and pull plugin profiles.
- Session restore assumes the DAW calls setStateInformation on the main thread (Logic does). This was already
  true of the Rack before this feature.
- The 30 Hz live update to the web UI runs whenever a GHS window is open, even on other tabs. Cheap, but it can be
  limited to the Console 1 tab if the window ever feels slow.
- Nothing here has been built or run on macOS yet.

## What has been verified (Linux sandbox, JUCE 8.0.4)

- `Console1Engine.cpp` compiles with `-Wall -Wextra`; `PluginProcessor.cpp` and `PluginEditor.cpp` pass a full
  syntax check against JUCE headers (BinaryData stubbed).
- `tools/test_console1_engine.cpp`: the real engine with fake plugins, 52 checks, also clean under AddressSanitizer
  and UBSan.
- The real web UI driven in headless Chromium against the real engine (`console1_ui_server`): auto-map, simulator
  drag, Learn, pick + Assign, profile save, slot switching from a simulated track button, mode change in Bindings.
  Checked at 1024, 1280 and 1500 px wide.
- Not verified: the physical MIDI port, ring feedback reaching hardware, the macOS build, the JUCE webview,
  real UAD/FabFilter parameter names.

## Running the tests

```
g++ -std=c++17 -I Source tools/test_console1_mapping.cpp -o /tmp/t1 && /tmp/t1
node tools/test_console1_automap.js
JUCE_DIR=../juce tools/build_console1_engine_test.sh && HOME=/tmp/c1home /tmp/c1engine/test
# UI in a browser, real engine behind it:
HOME=/tmp/c1home /tmp/c1engine/ui_server 8765   # then open http://127.0.0.1:8765/
```
