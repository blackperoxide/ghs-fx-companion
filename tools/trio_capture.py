#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = [
#   "numpy",
#   "librosa>=0.10",
#   "soundfile",
#   "pretty_midi",
# ]
# ///
"""
trio_capture.py - turn a DigiTech TRIO (original) band part into editable parts.

The original TRIO has no MIDI and no separate bass/drum outputs, so the only way
to get its ideas out is audio. This tool takes that audio and gives you:

  * drums.wav + bass.wav trimmed to whole bars (clean loops)
  * bass.mid   (one note at a time, cleaned and snapped to a 1/16 grid)
  * drums.mid  (kick 36 / snare 38 / closed hat 42, General MIDI, 1/16 grid)
  * a Riff House song folder (song.json) you can import in GHS FX Companion
  * RC-300 ready WAVs: drums on track 1, bass on track 2, track 3 left free
    for your guitar. Optionally copied straight onto the RC-300 in STORAGE mode
    (only into EMPTY phrase folders; it never overwrites a loop).

Best recording method (cleanest result, no AI splitting needed)
---------------------------------------------------------------
With an FS3X in "Live Mixer" mode (TRIO firmware MCU 1.4 / DSP 3.0+), hold
footswitch 1 to mute bass, footswitch 2 to mute drums. Record the TRIO MIXER OUT
twice from the same part, one pass drums-only and one pass bass-only:

    python3 tools/trio_capture.py --drums drums_pass.wav --bass bass_pass.wav \
        --title "Trio Blues 1"

Quick method (one recording, split by Demucs)
---------------------------------------------
Record the MIXER OUT once with the band playing (use the 1M-ohm resistor cable
trick so your guitar is not in that output) and let Demucs split it:

    python3 tools/trio_capture.py trio_mix.wav --title "Trio Blues 1"

Then put it on the RC-300 (USB Mode = STORAGE, drive mounted):

    python3 tools/trio_capture.py ... --rc300-memory 12 --rc300-drive /Volumes/BOSS_RC-300

Output goes to ~/Music/GHS/RiffHouse/SongImports/<slug>/ by default, next to the
songs riffhouse_import.py writes.
"""

import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple

# Reuse the exact folder/slug conventions of the existing importer.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from riffhouse_import import default_output_dir, slugify  # noqa: E402

RC300_SR = 44100          # RC-300 imports 16-bit / 44.1 kHz WAV (owner's manual p.36)
GRID = 4                  # steps per beat -> 1/16 notes
KICK, SNARE, HAT = 36, 38, 42
BASS_LOW, BASS_HIGH = 28, 67   # E1 .. G4: anything outside is almost surely an error


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def need(*mods: str) -> None:
    missing = [m for m in mods if importlib.util.find_spec(m) is None]
    if missing:
        print("trio_capture.py: missing Python package(s): " + ", ".join(missing), file=sys.stderr)
        print('    pip install numpy librosa soundfile pretty_midi', file=sys.stderr)
        print("  (the one-recording mode also needs:  pip install \"demucs>=4.0.1\" torch)", file=sys.stderr)
        sys.exit(1)


def load_mono(path: Path, sr: int):
    import librosa
    y, _ = librosa.load(str(path), sr=sr, mono=True)
    return y


FEEL_RANGES = {"auto": (70.0, 170.0), "slow": (55.0, 110.0), "fast": (110.0, 220.0)}


def fold_bpm(bpm: float, lo: float = 70.0, hi: float = 170.0) -> float:
    """TRIO itself picks half/double time (ALT TIME). Fold the estimate into a
    normal playing range so loops are a sensible number of bars."""
    if bpm <= 0:
        return 120.0
    while bpm < lo:
        bpm *= 2.0
    while bpm > hi:
        bpm /= 2.0
    return bpm


def estimate_bpm(y, sr: int, feel: str = "auto") -> float:
    import librosa
    import numpy as np
    fn = None
    for owner, name in ((getattr(librosa, "feature", None), "tempo"),
                        (getattr(getattr(librosa, "feature", None), "rhythm", None), "tempo"),
                        (librosa.beat, "tempo")):
        try:
            fn = getattr(owner, name)
            break
        except AttributeError:
            continue
    if fn is not None:
        tempo = fn(y=y, sr=sr)
    else:
        tempo, _ = librosa.beat.beat_track(y=y, sr=sr)
    return fold_bpm(float(np.asarray(tempo).reshape(-1)[0]), *FEEL_RANGES[feel])


def first_hit(y, sr: int) -> float:
    """Time (s) of the first clear onset: the TRIO part's downbeat."""
    import librosa
    import numpy as np
    env = librosa.onset.onset_strength(y=y, sr=sr)
    on = librosa.onset.onset_detect(onset_envelope=env, sr=sr, units="time", backtrack=True)
    if len(on) == 0:
        return 0.0
    # ignore tiny noise blips before the real start: first onset >= 30% of max strength
    frames = librosa.time_to_frames(on, sr=sr)
    peak = float(np.max(env)) if len(env) else 0.0
    for t, f in zip(on, frames):
        if f < len(env) and env[min(f + 2, len(env) - 1)] >= 0.3 * peak:
            return float(t)
    return float(on[0])


def loop_window(total_s: float, start_s: float, bpm: float, bars: Optional[int],
                beats_per_bar: int = 4) -> Tuple[float, int]:
    bar_s = beats_per_bar * 60.0 / bpm
    avail = max(0.0, total_s - start_s)
    whole = int(avail // bar_s + 1e-6)
    if bars is None:
        # TRIO parts loop in 2/4/8 bar phrases; keep the largest power of two that fits.
        n = 1
        while n * 2 <= whole:
            n *= 2
        bars = max(1, n)
    elif bars > whole:
        print(f"    note: asked for {bars} bars but only {whole} fit; using {whole}.")
        bars = max(1, whole)
    return bar_s * bars, bars


# ---------------------------------------------------------------------------
# separation (one-recording mode)
# ---------------------------------------------------------------------------

def demucs_split(mix: Path, work: Path, device: str) -> Tuple[Path, Path]:
    need("demucs", "torch")
    print("==> Splitting the TRIO mix into drums and bass with Demucs (htdemucs)")
    cmd = [sys.executable, "-m", "demucs", "-n", "htdemucs", "--two-stems", "drums",
           "-d", device, "-o", str(work), str(mix)]
    # pass 1: drums vs everything-else ; everything-else is the TRIO bass
    if subprocess.run(cmd).returncode != 0:
        print("trio_capture.py: Demucs failed (see above).", file=sys.stderr)
        sys.exit(1)
    d = work / "htdemucs" / mix.stem
    return d / "drums.wav", d / "no_drums.wav"


# ---------------------------------------------------------------------------
# transcription
# ---------------------------------------------------------------------------

def bass_to_notes(y, sr: int, bpm: float, t0: float) -> List[Tuple[float, float, int, int]]:
    """Monophonic bass with pYIN: (start_s, end_s, pitch, velocity), relative to t0."""
    import librosa
    import numpy as np
    hop = 256
    f0, voiced, prob = librosa.pyin(y, fmin=librosa.note_to_hz("C1"), fmax=librosa.note_to_hz("G4"),
                                    sr=sr, frame_length=2048, hop_length=hop)
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=hop)[0]
    rms = rms[: len(f0)]
    times = librosa.frames_to_time(np.arange(len(f0)), sr=sr, hop_length=hop)
    midi = np.where(voiced & np.isfinite(f0), np.round(librosa.hz_to_midi(np.nan_to_num(f0, nan=1.0))), -1)

    onsets = set(librosa.onset.onset_detect(y=y, sr=sr, hop_length=hop, backtrack=True).tolist())
    notes, cur, start, peak = [], -1, 0.0, 0.0
    min_len = (60.0 / bpm) / GRID * 0.5          # half a 1/16 note
    for i, m in enumerate(midi):
        m = int(m)
        retrig = i in onsets and m == cur and m > 0
        if m != cur or retrig:
            if cur > 0 and times[i] - start >= min_len:
                notes.append((start, times[i], cur, peak))
            cur, start, peak = m, times[i], 0.0
        if m > 0:
            peak = max(peak, float(rms[i]))
    if cur > 0 and times[-1] - start >= min_len:
        notes.append((start, float(times[-1]), cur, peak))

    top = max((n[3] for n in notes), default=1.0) or 1.0
    out = []
    for s, e, p, pk in notes:
        if not (BASS_LOW <= p <= BASS_HIGH):
            continue
        vel = int(np.clip(40 + 87 * (pk / top), 1, 127))
        out.append((s - t0, e - t0, p, vel))
    return out


def band(y, sr: int, lo: Optional[float], hi: Optional[float]):
    import numpy as np
    Y = np.fft.rfft(y)
    f = np.fft.rfftfreq(len(y), 1.0 / sr)
    mask = np.ones_like(f, dtype=bool)
    if lo is not None:
        mask &= f >= lo
    if hi is not None:
        mask &= f <= hi
    return np.fft.irfft(Y * mask, n=len(y))


def band_env(y, sr: int, lo: Optional[float], hi: Optional[float], hop: int = 128):
    import numpy as np
    b = band(y, sr, lo, hi)
    n = len(b) // hop
    frames = b[: n * hop].reshape(n, hop)
    return np.sqrt((frames ** 2).mean(axis=1) + 1e-12), hop


def drums_to_hits(y, sr: int, t0: float, bpm: float, length_s: float) -> List[Tuple[float, int, int]]:
    """Scan every 1/16 slot of the loop and ask, per drum band, "did this band
    get a new hit here?" (peak near the slot is loud for that band AND clearly
    louder than just before the slot). Returns (time_s, gm_note, velocity)
    relative to t0. Grid scanning is steadier than free onset detection for
    TRIO parts, which are always on a grid."""
    import numpy as np
    step = 60.0 / bpm / GRID
    bands = {KICK: (30, 150), SNARE: (180, 3500), HAT: (6000, None)}
    envs = {n: band_env(y, sr, lo, hi) for n, (lo, hi) in bands.items()}
    slots = int(round(length_s / step))
    found = {n: [] for n in bands}
    for n, (env, hop) in envs.items():
        fps = sr / hop
        for k in range(slots):
            ts = t0 + k * step
            a, b = int((ts - step * 0.3) * fps), int((ts + step * 0.4) * fps)
            p0, p1 = int((ts - step * 0.75) * fps), int((ts - step * 0.25) * fps)
            if a < 0 or b > len(env) or b <= a:
                continue
            peak = float(env[a:b].max())
            pre = float(env[max(0, p0):max(1, p1)].min()) if p1 > 0 else 0.0
            found[n].append((k, peak, pre))
    hits = []
    for n, rows in found.items():
        if not rows:
            continue
        top = max(r[1] for r in rows) or 1.0
        for k, peak, pre in rows:
            if peak >= 0.3 * top and peak >= 1.8 * max(pre, 1e-9):
                hits.append((k, n, peak / top))
    kicks = {k for k, n, _ in hits if n == KICK}
    # a kick's click bleeds into the snare band: keep a snare on a kick slot only if it is strong
    hits = [h for h in hits if not (h[1] == SNARE and h[0] in kicks and h[2] < 0.5)]
    snares = {k for k, n, _ in hits if n == SNARE}
    out = []
    for k, n, rel in hits:
        if n == HAT and k in snares and rel < 0.6:
            continue                      # snare noise bleeding into the hat band
        out.append((k * step, n, int(np.clip(30 + 97 * rel, 1, 127))))
    return out


def refine_grid(y, sr: int, t0: float, bpm: float) -> Tuple[float, float]:
    """Jointly fine-tune tempo (+/-3 BPM) and the downbeat (+/-30 ms) so the
    1/16 grid lines up best with onset energy over the whole recording."""
    import librosa
    import numpy as np
    hop = 64
    env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    fps = sr / hop
    dur = len(env) / fps
    offs = np.arange(-0.03, 0.0301, 1.0 / fps)
    best = (-1.0, bpm, t0)
    for cand in np.arange(bpm - 3.0, bpm + 3.0001, 0.02):
        step = 60.0 / cand / GRID
        k = np.arange(0, max(1.0, (dur - t0) / step))
        for off in offs:
            idx = np.round((t0 + off + k * step) * fps).astype(int)
            idx = idx[(idx >= 0) & (idx < len(env))]
            if len(idx) == 0:
                continue
            score = float(env[idx].mean())
            if score > best[0]:
                best = (score, float(cand), float(t0 + off))
    _, bb, tt = best
    if abs(bb - round(bb)) <= 0.15:
        bb = float(round(bb))
    # onset_strength peaks land ~1 frame after the true attack; step back to it
    return round(bb, 2), max(0.0, tt - 1.0 / fps)


def snap(t: float, bpm: float) -> float:
    step = 60.0 / bpm / GRID
    return round(t / step) * step


def write_bass_midi(notes, bpm: float, length_s: float, path: Path) -> int:
    import pretty_midi
    pm = pretty_midi.PrettyMIDI(initial_tempo=bpm)
    inst = pretty_midi.Instrument(program=33, name="TRIO Bass")   # GM Electric Bass (finger)
    step = 60.0 / bpm / GRID
    last_end = 0.0
    for s, e, p, v in sorted(notes):
        s, e = snap(s, bpm), snap(e, bpm)
        if e - s < step:
            e = s + step
        if s < 0 or s >= length_s:
            continue
        s = max(s, last_end)          # keep it one-note-at-a-time
        e = min(e, length_s)
        if e - s < step * 0.5:
            continue
        inst.notes.append(pretty_midi.Note(velocity=v, pitch=p, start=s, end=e))
        last_end = e
    pm.instruments.append(inst)
    path.parent.mkdir(parents=True, exist_ok=True)
    pm.write(str(path))
    return len(inst.notes)


def write_drum_midi(hits, bpm: float, length_s: float, path: Path) -> dict:
    import pretty_midi
    pm = pretty_midi.PrettyMIDI(initial_tempo=bpm)
    inst = pretty_midi.Instrument(program=0, is_drum=True, name="TRIO Drums")
    step = 60.0 / bpm / GRID
    seen = {}
    for t, n, v in hits:
        t = snap(t, bpm)
        if t < 0 or t >= length_s - 1e-6:
            continue
        key = (round(t / step), n)
        seen[key] = max(seen.get(key, 0), v)     # one hit per grid step per drum
    counts = {KICK: 0, SNARE: 0, HAT: 0}
    for (k, n), v in sorted(seen.items()):
        s = k * step
        inst.notes.append(pretty_midi.Note(velocity=v, pitch=n, start=s, end=s + step * 0.5))
        counts[n] += 1
    pm.instruments.append(inst)
    path.parent.mkdir(parents=True, exist_ok=True)
    pm.write(str(path))
    return {"kick": counts[KICK], "snare": counts[SNARE], "hat": counts[HAT]}


# ---------------------------------------------------------------------------
# audio out
# ---------------------------------------------------------------------------

def write_loop_wav(src: Path, start_s: float, length_s: float, dst: Path) -> None:
    """Trim to the loop window and write 16-bit / 44.1 kHz stereo (RC-300 format)."""
    import librosa
    import numpy as np
    import soundfile as sf
    y, _ = librosa.load(str(src), sr=RC300_SR, mono=False)
    if y.ndim == 1:
        y = np.vstack([y, y])
    a, n = int(round(start_s * RC300_SR)), int(round(length_s * RC300_SR))
    seg = y[:, a:a + n]
    if seg.shape[1] < n:
        seg = np.pad(seg, ((0, 0), (0, n - seg.shape[1])))
    # 3 ms fades so the loop point never clicks
    f = min(int(0.003 * RC300_SR), n // 2)
    if f > 0:
        ramp = np.linspace(0.0, 1.0, f)
        seg[:, :f] *= ramp
        seg[:, -f:] *= ramp[::-1]
    peak = float(np.max(np.abs(seg))) or 1.0
    if peak > 0.99:
        seg = seg * (0.99 / peak)
    dst.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(dst), seg.T, RC300_SR, subtype="PCM_16")


def stage_rc300(song: Path, memory: int, drums: Path, bass: Path) -> Path:
    root = song / "RC-300" / "ROLAND" / "WAVE"
    for track, src, name in ((1, drums, "TRIODRUM.WAV"), (2, bass, "TRIOBASS.WAV")):
        d = root / f"{memory:03d}_{track}"
        d.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, d / name)
    (root / f"{memory:03d}_3").mkdir(parents=True, exist_ok=True)   # left empty for guitar
    return root


def copy_to_rc300(staged_wave: Path, drive: Path, memory: int) -> List[str]:
    """Copy into the mounted RC-300 only if every target folder is empty."""
    wave = drive / "ROLAND" / "WAVE"
    if not wave.is_dir():
        raise SystemExit(f"trio_capture.py: {wave} not found. Set RC-300 SYSTEM > USB Mode = STORAGE and connect it.")
    done = []
    for track in (1, 2):
        dst = wave / f"{memory:03d}_{track}"
        if dst.is_dir() and any(p.suffix.lower() == ".wav" for p in dst.iterdir()):
            raise SystemExit(f"trio_capture.py: {dst} already has a loop. Pick an empty memory "
                             f"(--rc300-memory) - this tool never overwrites RC-300 loops.")
    for track in (1, 2):
        src_dir = staged_wave / f"{memory:03d}_{track}"
        dst = wave / f"{memory:03d}_{track}"
        dst.mkdir(parents=True, exist_ok=True)
        for f in src_dir.iterdir():
            shutil.copyfile(f, dst / f.name)
            done.append(str(dst / f.name))
    return done


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Turn a DigiTech TRIO band part into loops, MIDI, "
                                            "a Riff House song and RC-300 tracks.",
                                formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    p.add_argument("mix", nargs="?", type=Path, help="one TRIO MIXER OUT recording (bass+drums) - split with Demucs")
    p.add_argument("--drums", type=Path, help="drums-only pass (FS3X Live Mixer: hold footswitch 1 = bass muted)")
    p.add_argument("--bass", type=Path, help="bass-only pass (FS3X Live Mixer: hold footswitch 2 = drums muted)")
    p.add_argument("--title", default=None)
    p.add_argument("--bpm", type=float, default=None, help="override the tempo estimate")
    p.add_argument("--feel", choices=["auto", "slow", "fast"], default="auto",
                   help="like the TRIO's ALT TIME hint: settle half/double-time doubt (slow = 55-110 BPM)")
    p.add_argument("--bars", type=int, default=None, help="loop length in bars (default: largest 2/4/8/16 that fits)")
    p.add_argument("--output-dir", type=Path, default=None)
    p.add_argument("--rc300-memory", type=int, default=None, help="RC-300 phrase memory 1-99 to stage tracks for")
    p.add_argument("--rc300-drive", type=Path, default=None, help="mounted RC-300 drive, e.g. /Volumes/BOSS_RC-300")
    p.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"], help="Demucs device (one-recording mode)")
    a = p.parse_args(argv)
    if a.mix is None and not (a.drums and a.bass):
        p.error("give one MIX recording, or both --drums and --bass passes")
    if a.rc300_memory is not None and not (1 <= a.rc300_memory <= 99):
        p.error("--rc300-memory must be 1-99")
    if a.rc300_drive and a.rc300_memory is None:
        p.error("--rc300-drive needs --rc300-memory")
    return a


def main(argv=None) -> int:
    a = parse_args(argv)
    need("numpy", "librosa", "soundfile", "pretty_midi")
    import librosa

    title = a.title or (a.mix or a.drums).stem
    song = (a.output_dir or default_output_dir()) / (slugify(title) or "trio-part")
    (song / "stems").mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="trio_capture_"))
    try:
        if a.mix is not None and not (a.drums and a.bass):
            drums_src, bass_src = demucs_split(a.mix.resolve(), tmp, a.device)
        else:
            drums_src, bass_src = a.drums.resolve(), a.bass.resolve()

        sr = 22050
        print("==> [1/5] Finding tempo and the first downbeat")
        yd, yb = load_mono(drums_src, sr), load_mono(bass_src, sr)
        bpm = float(a.bpm) if a.bpm else estimate_bpm(yd, sr, a.feel)
        t0 = first_hit(yd, sr)
        if not a.bpm:
            bpm, t0 = refine_grid(yd, sr, t0, bpm)
        total = min(len(yd), len(yb)) / sr
        length, bars = loop_window(total, t0, bpm, a.bars)
        print(f"    {bpm:.1f} BPM, starts at {t0:.3f}s, loop = {bars} bar(s) ({length:.2f}s)")

        print("==> [2/5] Writing bar-trimmed loops (16-bit / 44.1 kHz stereo)")
        write_loop_wav(drums_src, t0, length, song / "stems" / "drums.wav")
        write_loop_wav(bass_src, t0, length, song / "stems" / "bass.wav")

        print("==> [3/5] Bass to MIDI (pYIN, one note at a time, 1/16 grid)")
        n_bass = write_bass_midi(bass_to_notes(yb, sr, bpm, t0), bpm, length, song / "midi" / "bass.mid")
        print(f"    {n_bass} bass notes")

        print("==> [4/5] Drums to MIDI (kick 36 / snare 38 / hat 42, 1/16 grid)")
        counts = write_drum_midi(drums_to_hits(yd, sr, t0, bpm, length), bpm, length, song / "midi" / "drums.mid")
        print(f"    kick {counts['kick']}, snare {counts['snare']}, hat {counts['hat']}")

        print("==> [5/5] Riff House song.json" + (" + RC-300 tracks" if a.rc300_memory else ""))
        info = {
            "title": title,
            "bpm": bpm,
            "stems": {"drums": "stems/drums.wav", "bass": "stems/bass.wav"},
            "midi": {"bass": "midi/bass.mid"} if n_bass else {},
            "trio": {"source": "DigiTech TRIO", "bars": bars, "loop_seconds": round(length, 4),
                     "drum_midi": "midi/drums.mid", "drum_counts": counts},
        }
        if a.rc300_memory:
            wave = stage_rc300(song, a.rc300_memory, song / "stems" / "drums.wav", song / "stems" / "bass.wav")
            info["trio"]["rc300"] = {"memory": a.rc300_memory, "track1": "drums", "track2": "bass",
                                     "track3": "free for guitar", "staged": str(wave.relative_to(song))}
            if a.rc300_drive:
                copied = copy_to_rc300(wave, a.rc300_drive, a.rc300_memory)
                print("    copied to RC-300:\n      " + "\n      ".join(copied))
                print("    Eject the drive, then on the RC-300 load memory "
                      f"{a.rc300_memory:03d}. If the tempo shows double/half, fix it with Measure = {bars}.")
        (song / "song.json").write_text(json.dumps(info, indent=2) + "\n")
        print(f"\nDone: {song}")
        print("  Logic: drag midi/bass.mid and midi/drums.mid onto software instrument tracks.")
        print("  Riff House: Songs > Import > song folder > pick this folder.")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
