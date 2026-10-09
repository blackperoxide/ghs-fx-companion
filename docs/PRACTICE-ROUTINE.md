# Riff House: daily practice routine and program journal

Built for a stretch away from your normal setup (for example a 3-week intensive
program): practice becomes a small, forgiving daily anchor, and the app quietly
keeps a record of what each day was like.

## What it does

The new **Today** tab (first tab in Riff House) gives you:

- **Day tiles** for the whole program. A tile fills in when you show up. Nothing ever
  un-fills, and a missed day does not reset anything.
- **Check-in** before and after you play (Rough / Low / Okay / Steady / Good, plus an
  optional "where do I feel it" line). Over 21 days this becomes your own record of
  what actually helps you regulate.
- **Four quests**, tracked automatically: Warm up (10 Note Ninja hits), Play one song
  (2 minutes with a chart playing), Save one idea (Save that), Write a few lines.
  Any one of them, or the "I picked it up today" button, counts as a full day.
- **Weekly goal** (checked automatically from your play sessions):
  week 1 Slow Burn (loop one section at 0.7x or slower, reach a 20 combo),
  week 2 Build It (whole song at 0.8x or faster, 70% of notes hit),
  week 3 Boss Fight (whole song at full speed, 70% of notes hit, then save a take).
- **XP and ranks** (Roadie up to Legend) from minutes played, quests, and check-ins.
- **Journal** with optional prompts and tags (Group, Body, Win, Hard, Lyric, Song idea,
  Gratitude). Mac dictation works in the text box if typing is too much.
- **Practice log**: every play session is recorded (song, time, speed, % of notes hit,
  best combo, score).
- **Export program log (Markdown)**: one readable file of the whole stay.

Everything is stored as plain files in `~/Music/GHS/RiffHouse/Journal/`:
`journal.json` (the data, with a rolling `journal.backup.json`) and `Program-Log.md`
(written when you press Export). Nothing leaves your machine.

## Before you leave (one-time, in this order)

1. In Terminal, from the repo folder, run `tools/riffhouse_mac_setup.sh`. It switches to
   the `riffhouse-routine` branch, rebuilds (including the Standalone app, so you do not
   need a DAW open to practice), installs the song-import tools, and prints PASS or FAIL
   for each check. Re-run `tools/riffhouse_mac_setup.sh --verify` any time.
   Done when: it says all automatic checks passed and Riff House opens on **Today**.
2. Put your own songs in one folder as mp3 or wav, one file per song.
   Start with 3 to 5 songs, not 30.
3. Run the importer once: `tools/riffhouse_batch.sh ~/Music/MySongs`
   It splits each song into stems and writes note charts. It skips finished songs, so
   you can re-run it. Done when: each song has a folder in
   `~/Music/GHS/RiffHouse/SongImports/` containing `song.json`.
4. In the plugin: **Songs, Import song folder...**, pick each folder.
   Done when: each song shows as a card on the Songs tab.
5. Load one song, press Play, set the Rate slider to 0.7, and confirm a note scrolls
   toward you and your guitar lights up the live note readout.
6. Open **Today**, press Start (enter the first day of the program), do one check-in.
   Done when: tile 1 has a dot.
7. Pack: laptop, charger, guitar, instrument cable or interface, headphones.
   Optional: a small MIDI keyboard.

## Each day (about 15 minutes; shorter is fine)

1. Open Today. Tap how you feel (Before I play).
2. Warm up: Note Ninja until it says 10 / 10.
3. One song, one section: Songs, pick a chart, Set A / Set B around the section you
   want, turn on Loop, drop the Rate to 0.7, and play it.
4. If anything sounds good, press Save that.
5. Tap how you feel (After I play).
6. Write one line. A prompt is offered; you can ignore it.

On a hard day, do only step 1 or only the "I picked it up today" button. That still
counts as showing up.

## Getting your record afterward

Press **Export program log (Markdown)** on the Today tab, or copy the
`~/Music/GHS/RiffHouse/Journal/` folder. `journal.json` can be re-imported by
copying it back into the same folder.

## Limits worth knowing

- Chart quality depends on the recording. Clean single-instrument stems chart best;
  dense mixes can produce rough notes. Use Wait mode and a low Rate while learning.
- Note detection hears one note at a time (guitar/bass/voice) plus chroma for chords,
  so chords are scored loosely.
