#!/usr/bin/env python3
"""Synthetic end-to-end test for trio_capture.py (no Demucs needed).

Builds a known 4-bar part at 100 BPM - kick on 1 and 3, snare on 2 and 4, 8th-note
hats, and a bass line E1 A1 B1 E2 one note per beat - plus 0.5 s of silence in front
and a ragged tail, then checks tempo, trim, MIDI content and the RC-300 staging/copy
rules (copies into empty folders, refuses to overwrite)."""
import json, subprocess, sys, tempfile
from pathlib import Path
import numpy as np, soundfile as sf, pretty_midi

SR, LEAD = 44100, 0.5
BPM = float(sys.argv[1]) if len(sys.argv) > 1 else 100.0
BARS = int(sys.argv[2]) if len(sys.argv) > 2 else 4
EXTRA = sys.argv[3:]   # e.g. --feel slow
beat = 60.0 / BPM
here = Path(__file__).resolve().parent
tmp = Path(tempfile.mkdtemp())
n = int((LEAD + BARS * 4 * beat + 0.37) * SR)
drums, bass = np.zeros(n), np.zeros(n)
rng = np.random.default_rng(1)

def put(buf, t, sig):
    a = int(t * SR); m = min(len(sig), len(buf) - a); buf[a:a + m] += sig[:m]

tt = np.arange(int(0.25 * SR)) / SR
kick = np.sin(2 * np.pi * (50 + 60 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 18)
snare = rng.normal(0, 1, len(tt)) * np.exp(-tt * 25) * 0.5 + np.sin(2*np.pi*200*tt)*np.exp(-tt*30)*0.4
hat = np.diff(rng.normal(0, 1, int(0.05 * SR) + 1)) * np.exp(-np.arange(int(0.05*SR)) / SR * 90) * 0.3
line = [40, 45, 47, 52]
for b in range(BARS * 4):
    t = LEAD + b * beat
    put(drums, t, kick if b % 2 == 0 else snare)
    put(drums, t, hat); put(drums, t + beat / 2, hat)
    f = 440 * 2 ** ((line[b % 4] - 69) / 12)
    nt = np.arange(int(beat * 0.9 * SR)) / SR
    tone = (np.sin(2*np.pi*f*nt) + 0.3*np.sin(4*np.pi*f*nt)) * np.minimum(1, nt*200) * np.exp(-nt*1.5) * 0.5
    put(bass, t, tone)
sf.write(tmp / "d.wav", drums / np.abs(drums).max() * 0.8, SR)
sf.write(tmp / "b.wav", bass / np.abs(bass).max() * 0.8, SR)

out = tmp / "out"; drive = tmp / "BOSS_RC-300"; (drive / "ROLAND" / "WAVE").mkdir(parents=True)
cmd = [sys.executable, str(here / "trio_capture.py"), "--drums", str(tmp/"d.wav"), "--bass", str(tmp/"b.wav"),
       "--title", "Synthetic Test", "--output-dir", str(out), "--rc300-memory", "7", "--rc300-drive", str(drive)] + EXTRA
r = subprocess.run(cmd, capture_output=True, text=True); print(r.stdout[-1500:], r.stderr[-1500:])
fails = []
def check(ok, msg): print(("PASS " if ok else "FAIL ") + msg); ok or fails.append(msg)
check(r.returncode == 0, "tool exits 0")
song = out / "synthetic-test"; info = json.loads((song / "song.json").read_text())
check(abs(info["bpm"] - BPM) <= 1.0, f"tempo {info['bpm']} ~ {BPM}")
check(info["trio"]["bars"] == BARS, f"bars {info['trio']['bars']} == {BARS}")
d, sr = sf.read(song / "stems" / "drums.wav")
check(sr == 44100 and d.ndim == 2, "loop is 44.1 kHz stereo")
check(abs(len(d) / sr - BARS * 4 * beat) < 0.01, f"loop length {len(d)/sr:.3f}s == {BARS*4*beat:.3f}s")
check(sf.info(song / "stems" / "drums.wav").subtype == "PCM_16", "16-bit PCM")
bm = pretty_midi.PrettyMIDI(str(song / "midi" / "bass.mid")).instruments[0].notes
pitches = [x.pitch for x in sorted(bm, key=lambda x: x.start)]
check(abs(len(bm) - BARS * 4) <= 1, f"{BARS*4} bass notes (got {len(bm)})")
check(pitches[:4] == line, f"bass line {pitches[:4]} == {line}")
check(abs(bm[0].start) < 0.02, f"first bass note on the downbeat ({bm[0].start:.3f}s)")
dm = pretty_midi.PrettyMIDI(str(song / "midi" / "drums.mid")).instruments[0].notes
c = {k: sum(1 for x in dm if x.pitch == k) for k in (36, 38, 42)}
check(c[36] == BARS * 2 and c[38] == BARS * 2, f"{BARS*2} kicks, {BARS*2} snares (got {c[36]}, {c[38]})")
check(c[42] >= BARS * 8 - 2, f"~{BARS*8} hats (got {c[42]})")
check(all(abs(x.start % (beat/4)) < 1e-3 or abs(x.start % (beat/4) - beat/4) < 1e-3 for x in dm), "drums on 1/16 grid")
w = drive / "ROLAND" / "WAVE"
check((w/"007_1"/"TRIODRUM.WAV").is_file() and (w/"007_2"/"TRIOBASS.WAV").is_file(), "copied to RC-300 tracks 1 and 2")
r2 = subprocess.run(cmd, capture_output=True, text=True)
check(r2.returncode != 0 and "never overwrites" in (r2.stderr + r2.stdout), "refuses to overwrite an RC-300 loop")
print("\n%d failed" % len(fails)); sys.exit(1 if fails else 0)
