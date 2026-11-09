# DigiTech TRIO (original) in the GHS studio

Three jobs, in order of payoff:

1. Trio Capture: get the TRIO's bass and drums out as loops + MIDI (works now).
2. TRIO + RC-300: live song building with the looper (works now, no code).
3. Firmware: re-engineering the TRIO itself (research; first safe step ready).

Labels used here: **tested** = run in the Linux sandbox; **untested** = not run on your
Mac or pedal yet; **inference** = reasoned, not proven.

---

## 1. Trio Capture (`tools/trio_capture.py`)

What it makes, from a TRIO recording:

| Output | What it is |
|---|---|
| `stems/drums.wav`, `stems/bass.wav` | trimmed to whole bars, 16-bit / 44.1 kHz stereo, 3 ms fades so loops never click |
| `midi/bass.mid` | one note at a time, 1/16 grid (pYIN pitch tracking) |
| `midi/drums.mid` | kick 36 / snare 38 / closed hat 42 (General MIDI), 1/16 grid |
| `song.json` | Riff House song folder: Songs > Import > song folder |
| `RC-300/ROLAND/WAVE/0XX_1, 0XX_2` | drums on track 1, bass on track 2, track 3 left free for guitar |

Tested on synthetic parts at 78, 92, 100, 120 and 132 BPM, 2 / 4 / 8 bars:
`python3 tools/test_trio_capture.py 100 4` (all 14 checks pass). Untested on a real TRIO recording.

### Wiring for capture (no resistor trick needed)

The TRIO manual: with BOTH outputs connected, AMP OUT carries guitar only and MIXER OUT
carries bass + drums only. So:

```
Guitar (clean) -> TRIO GUITAR IN
TRIO AMP OUT   -> your amp / THR (guitar only)
TRIO MIXER OUT -> Apollo Line In 4  (bass + drums only)  -> Logic track "TRIO BAND"
```

### Best method: two passes (no AI splitting)

FS3X in Live Mixer mode (firmware MCU 1.4 / DSP 3.0+): hold footswitch 1 = bass muted,
footswitch 2 = drums muted.

1. Teach the TRIO a part. Let it play.
2. Record ~4 bars of MIXER OUT with bass muted -> `drums_pass.wav`.
3. Record ~4 bars with drums muted -> `bass_pass.wav`.
4. Run:

```
~/.riffhouse-venv/bin/python tools/trio_capture.py --drums drums_pass.wav --bass bass_pass.wav \
    --title "Trio Blues 1" --rc300-memory 12
```

### Quick method: one pass

Record MIXER OUT once (bass + drums). Demucs splits it (needs the importer venv):

```
~/.riffhouse-venv/bin/python tools/trio_capture.py trio_band.wav --title "Trio Blues 1"
```

If the tempo comes out double or half, add `--feel slow` or `--feel fast` (same idea as the
TRIO's ALT TIME button), or give the exact tempo with `--bpm 96`.

### Logic

Drag `midi/bass.mid` onto a bass instrument track and `midi/drums.mid` onto Drum Kit Designer
or any GM kit. Now you can change every note. The TRIO gave you the idea; Logic finishes it.

---

## 2. TRIO + RC-300 (live)

RC-300 facts used here (owner's manual): INST IN L/MONO + R, MIC IN, AUX IN (stereo mini);
AUX/INST/MIC input sound can be recorded; each track's Output can be MAIN, SUB or MAIN+SUB;
the RC-300 always sends MIDI clock; WAVs go in `ROLAND/WAVE/0XX_Y` (XX = memory,
Y = track), one WAV per folder, 16-bit / 44.1 kHz.

### Option A: record the TRIO band straight into the RC-300

```
Guitar -> TRIO GUITAR IN
TRIO AMP OUT   -> RC-300 INST IN L/MONO      (guitar)
TRIO MIXER OUT -> RC-300 AUX IN              (bass + drums; 1/4" TS to 3.5 mm cable)
RC-300 MAIN OUT -> THR / amp    RC-300 SUB OUT -> Apollo / PA
```

Checklist:
- [ ] Teach the TRIO the verse. Let it play.
- [ ] Don't play guitar. Record Track 1 on a downbeat, stop on a downbeat 4 bars later. Track 1 = TRIO band loop.
- [ ] Stop the TRIO. The RC-300 now holds the band and the tempo.
- [ ] Set Track1:Output = SUB, so the drums go to the PA, not through the guitar amp.
- [ ] Tracks 2 and 3: your guitar layers (Output = MAIN, to the THR).
- [ ] Save as memory 012 (verse). Repeat for the chorus as memory 013, and so on.

The RC-300 has no per-track input select (not found in the manual), so the band and guitar
inputs record together. Recording the band with your guitar silent keeps them apart.

### Option B: cleaner loops from Trio Capture

Run Trio Capture with `--rc300-memory 12 --rc300-drive /Volumes/BOSS_RC-300` (RC-300
SYSTEM > USB Mode = STORAGE). Drums land on track 1 and bass on track 2, each separate and
bar-exact. Track 3 is yours. The tool only writes into EMPTY phrase folders and stops if a
loop is already there. If the RC-300 shows double/half tempo after import, set Measure to the
bar count the tool printed.

### Logic follows the RC-300

The RC-300 always sends MIDI clock. RC-300 MIDI OUT -> your MIDI interface; Logic >
Settings > MIDI > Sync: receive MIDI clock (untested with your interface).

---

## 3. Firmware (re-engineering the TRIO)

### What the updater and firmware are (binary-based, static only, 2026-11-09)

- Trio Updater 2.0.2 for Mac is an Intel (x86_64) Qt 5 app. It runs on your Mac through
  Rosetta (checked: Rosetta works on your Mac).
- It reads `tp-manifest.xml` from DigiTech's server. That server is still live. For the
  original TRIO (Product ID 0x42) it lists:

| Component | File | Version | Size | SHA-256 |
|---|---|---|---|---|
| MP (MCU) | Trio_Update.bin | 3.1.4 (2016-01-08) | 40 KB, ARM Cortex-M | `bab98159...017d22` |
| DSP | DSP1.ldr | 3.3.0 (2016-02-16) | 1,644,260 bytes as ASCII `0xNN` lines | `06cc8b65...5fe4` |

- The updater has a "Set Manifest URL..." menu item, so a local manifest can point it at
  your own files.
- The updater code contains a "Styles" component and the message "Style data too big for
  flash". But the server only lists Styles for the TRIO+ (`TrioPlusStyleData.bin`, header
  `sst`, 144 entries = 12 genres x 12 styles). The original TRIO's 7 genres are
  inside the DSP image. No readable text or style names are in it.
- It also contains: "An MCU update is available but your current MCU firmware cannot be
  updated by USB. It must be programmed directly into the MCU." So the MCU is the part that
  can truly brick the pedal.

### Risk (inference)

The DSP file is sent through the MCU. If a DSP flash goes wrong, the MCU and updater should
still talk, and you can re-flash the stock DSP. A bad MCU flash may need a hardware programmer.
So: **never flash a modified MCU file.** `trio_firmware.py` will not build a manifest with one.

### Safe first step: prove the flash path with STOCK files (untested on the pedal)

```
python3 tools/trio_firmware.py fetch
python3 tools/trio_firmware.py manifest --dsp ~/Music/GHS/Trio/firmware/stock/DSP1.ldr
cd ~/Music/GHS/Trio/firmware/serve-stock && python3 -m http.server 8765 --bind 127.0.0.1
```

Then in Trio Updater: Set Manifest URL -> `http://127.0.0.1:8765/tp-manifest.xml`. Connect
the TRIO by USB and see what versions it reports. Write them down; that is the most important
piece of information we don't have yet. Update the DSP only if it offers one.

### Research road to "better song creation" in the pedal (not started)

1. Identify the DSP chip family from the loader blocks (the ADI loader format is known;
   chip not confirmed yet). Then load it in Ghidra with the right processor module.
2. Find the style tables inside the 1.6 MB DSP image (compare against the TRIO+ `sst`
   format, which has a clear table of offsets).
3. Only then try small edits (for example, a bass pattern), flash the DSP, keep the
   stock DSP ready to restore.

Steps 1-3 are weeks of careful work with uncertain payoff. Trio Capture gets you better
bass and drum parts today.

---

## REA on your Mac

`tools/rea_mac_install.sh` installs REA 6.5.0 (pinned lockfile in `tools/rea/`) into
`~/Tools/rea-6.5.0` with a `rea` command. Add `--mcp` to see, without changing anything,
what hooking it into Claude Desktop would change. Tested on Linux with a temporary home
folder: `rea --version` = 6.5.0.
