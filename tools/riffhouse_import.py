#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = [
#   "demucs>=4.0.1",
#   "basic-pitch>=0.4.0",
#   "torch>=2.1",
#   "numpy",
#   "librosa>=0.8.0",
#   "soundfile",
# ]
# ///
"""
riffhouse_import.py - turn a song into a GHS FX Companion "Riff House" song folder.

Riff House (Source/RiffHouseEngine.cpp, Engine::importSongFolderAsync) can import a
folder containing a `song.json` plus separated stem audio and per-stem MIDI. This
script builds exactly that folder from a single mp3/wav (e.g. something you made in
Suno, or any other song you own the rights to practice against):

  1. Demucs (htdemucs_6s, 6 stems) splits the song into vocals/drums/bass/guitar/
     piano/other.
  2. Spotify's Basic Pitch transcribes the pitched, mostly-monophonic-or-chordal
     stems (guitar, bass, piano, vocals -- NOT drums or "other") into MIDI.
  3. A single song BPM is estimated (librosa onset/beat tracking, with a tiny
     autocorrelation fallback if librosa isn't importable for some reason).
  4. Everything is written out as:

       <output-dir>/<slug-of-title>/
           song.json
           stems/
               vocals.wav
               drums.wav
               bass.wav
               guitar.wav
               piano.wav
               other.wav
           midi/
               vocals.mid
               bass.mid
               guitar.mid
               piano.mid

     `song.json` looks like:

       {
         "title": "My Song",
         "bpm": 118,
         "stems": { "vocals": "stems/vocals.wav", "drums": "stems/drums.wav", ... },
         "midi":  { "vocals": "midi/vocals.mid", "guitar": "midi/guitar.mid", ... }
       }

     All paths inside song.json are relative to the song folder itself -- that's
     what RiffHouseEngine.cpp's importSongFolderAsync expects (it resolves every
     stems/midi entry with `folder.getChildFile(path)`). "drums" and "other" are
     never given MIDI entries because the C++ side hard-excludes those two stem
     names from transcription/charting (it only ever charts the other four).

Example
-------
    python3 tools/riffhouse_import.py ~/Music/suno-tracks/midnight_drive.mp3 \\
        --title "Midnight Drive" \\
        --device auto

  That writes (by default) to:
      ~/Music/GHS/RiffHouse/SongImports/midnight-drive/

  Then in GHS FX Companion: Riff House -> Import -> "song folder" -> pick that
  folder.

Skipping a stem (e.g. you never want the catch-all "other" bucket taking up
space / separation time) and keeping Demucs' raw output around for inspection:

    python3 tools/riffhouse_import.py song.wav --skip-stems other --keep-temp

Installing dependencies
------------------------
    pip install "demucs>=4.0.1" "basic-pitch>=0.4.0" torch librosa soundfile numpy

(On Apple Silicon, a normal `pip install torch` already ships with MPS support;
nothing extra is needed to use --device mps / --device auto.)
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

# ---------------------------------------------------------------------------
# constants matching the C++ side (Source/RiffHouseEngine.cpp)
# ---------------------------------------------------------------------------

DEMUCS_MODEL = "htdemucs_6s"
ALL_STEMS = ["vocals", "drums", "bass", "guitar", "piano", "other"]
# RiffHouseEngine.cpp::importSongFolderAsync hard-excludes these two stem names
# from transcription/charting ("drums" and "other" never get a chart part), so
# there is never a reason to run Basic Pitch on them.
PITCHED_STEMS = ["vocals", "bass", "guitar", "piano"]


def default_output_dir() -> Path:
    """
    RiffHouseEngine.h/.cpp define rootFolder() = ~/Music/GHS/RiffHouse and
    chartsFolder() = ~/Music/GHS/RiffHouse/Charts, but there is no canonical
    folder for *song-folder imports* -- the plugin just opens a native folder
    picker rooted at ~/Music (see PluginEditor.cpp's rhImport native function)
    and lets the user navigate to wherever this script wrote its output.

    So this is a judgment call: default to a sibling of Charts/ under the same
    RiffHouse root, which keeps generated song folders next to everything else
    Riff House owns and inside the ~/Music tree the folder picker starts in.
    """
    return Path.home() / "Music" / "GHS" / "RiffHouse" / "SongImports"


def slugify(text: str) -> str:
    """Mirror RiffHouseEngine.cpp's anonymous-namespace `slugify()` exactly:
    lowercase, non-alphanumeric runs collapse to a single '-', trimmed."""
    out = []
    for ch in text.lower():
        out.append(ch if ch.isalnum() else "-")
    slug = "".join(out)
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug.strip("-")


# ---------------------------------------------------------------------------
# dependency checks
# ---------------------------------------------------------------------------

def _missing(*module_names: str) -> List[str]:
    return [m for m in module_names if importlib.util.find_spec(m) is None]


def check_dependencies() -> None:
    missing = _missing("torch", "demucs", "basic_pitch", "librosa", "soundfile", "numpy")
    if not missing:
        return
    print("riffhouse_import.py: missing Python package(s): " + ", ".join(missing), file=sys.stderr)
    print(file=sys.stderr)
    print("Install everything this tool needs with:", file=sys.stderr)
    print(file=sys.stderr)
    print('    pip install "demucs>=4.0.1" "basic-pitch>=0.4.0" torch librosa soundfile numpy', file=sys.stderr)
    print(file=sys.stderr)
    print("(or, if you use uv: `uv run tools/riffhouse_import.py ...` will pick up", file=sys.stderr)
    print(" the dependencies declared in this file's PEP 723 header automatically.)", file=sys.stderr)
    sys.exit(1)


def pick_device(requested: str) -> str:
    requested = requested.lower()
    if requested in ("cpu", "mps", "cuda"):
        return requested
    if requested != "auto":
        print(f"riffhouse_import.py: unknown --device '{requested}', falling back to auto-detect.", file=sys.stderr)

    import torch  # safe: check_dependencies() already ran

    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


# ---------------------------------------------------------------------------
# stage 1: Demucs stem separation
# ---------------------------------------------------------------------------

def run_demucs(input_file: Path, device: str, work_dir: Path) -> Path:
    """
    Shells out to `python -m demucs`. Demucs writes
      <work_dir>/<model>/<track-name>/<stem>.wav
    for each of htdemucs_6s's 6 sources. Returns that per-track directory.
    """
    print(f"==> [1/4] Separating stems with Demucs ({DEMUCS_MODEL}, device={device})")
    print("    This downloads the model on first run and can take a few minutes for a full song.")
    cmd = [
        sys.executable, "-m", "demucs",
        "-n", DEMUCS_MODEL,
        "-d", device,
        "-o", str(work_dir),
        str(input_file),
    ]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(file=sys.stderr)
        print("riffhouse_import.py: Demucs failed (see output above).", file=sys.stderr)
        print("If the error mentions a missing module, install Demucs with:", file=sys.stderr)
        print('    pip install "demucs>=4.0.1"', file=sys.stderr)
        sys.exit(1)

    track_dir = work_dir / DEMUCS_MODEL / input_file.stem
    if not track_dir.is_dir():
        # Demucs occasionally normalizes the track folder name; fall back to
        # whatever single directory it actually created.
        model_dir = work_dir / DEMUCS_MODEL
        candidates = [p for p in model_dir.iterdir() if p.is_dir()] if model_dir.is_dir() else []
        if len(candidates) == 1:
            track_dir = candidates[0]
        else:
            print(f"riffhouse_import.py: couldn't find Demucs output under {model_dir}", file=sys.stderr)
            sys.exit(1)

    print(f"    Stems written to {track_dir}")
    return track_dir


# ---------------------------------------------------------------------------
# stage 2: BPM estimate
# ---------------------------------------------------------------------------

def _autocorrelation_bpm_fallback(y, sr: int) -> float:
    """Minimal onset-autocorrelation tempo estimate, used only if librosa is
    unavailable. Same idea as RiffHouseEngine.cpp's own fallback: a rectified
    frame-energy derivative, autocorrelated across 50-200 BPM lags."""
    import numpy as np

    hop, frame = 512, 1024
    n = len(y)
    if n < frame * 4:
        return 120.0
    n_frames = max(1, (n - frame) // hop)
    rms = np.empty(n_frames, dtype=np.float64)
    for i in range(n_frames):
        start = i * hop
        chunk = y[start:start + frame]
        rms[i] = float(np.sqrt(np.mean(chunk.astype(np.float64) ** 2) + 1e-12))

    onset = np.diff(rms, prepend=rms[0])
    onset[onset < 0] = 0.0

    sr_frames = sr / hop
    best_bpm, best_score = 120.0, -1.0
    for bpm in range(50, 201):
        lag = int(round(60.0 * sr_frames / bpm))
        if lag <= 0 or lag >= len(onset):
            continue
        score = float(np.dot(onset[:-lag], onset[lag:]))
        if score > best_score:
            best_score, best_bpm = score, float(bpm)
    return best_bpm


def estimate_bpm(input_file: Path) -> float:
    print("==> [2/4] Estimating song tempo")
    try:
        import librosa
        import numpy as np

        y, sr = librosa.load(str(input_file), sr=22050, mono=True)

        tempo = None
        try:
            # librosa >= 0.10 moved tempo estimation to feature.rhythm.tempo
            tempo = librosa.feature.rhythm.tempo(y=y, sr=sr)
        except AttributeError:
            try:
                tempo = librosa.beat.tempo(y=y, sr=sr)  # older librosa
            except AttributeError:
                _, tempo = librosa.beat.beat_track(y=y, sr=sr)
                tempo = [tempo]

        bpm = float(np.asarray(tempo).reshape(-1)[0])
        if not (30.0 <= bpm <= 300.0):
            raise ValueError("implausible tempo estimate")
    except Exception as exc:  # pragma: no cover - defensive fallback path
        print(f"    librosa tempo estimate failed ({exc}); using the built-in fallback estimator.")
        import soundfile as sf
        y, sr = sf.read(str(input_file), always_2d=False)
        if getattr(y, "ndim", 1) > 1:
            y = y.mean(axis=1)
        bpm = _autocorrelation_bpm_fallback(y, sr)

    print(f"    Estimated tempo: {bpm:.1f} BPM")
    return bpm


# ---------------------------------------------------------------------------
# stage 3: Basic Pitch transcription
# ---------------------------------------------------------------------------

def transcribe_stem_to_midi(stem_wav: Path, out_mid: Path, bpm: float) -> bool:
    from basic_pitch.inference import predict

    try:
        _, midi_data, _ = predict(str(stem_wav), midi_tempo=bpm)
    except Exception as exc:
        print(f"    Basic Pitch failed on {stem_wav.name}: {exc}", file=sys.stderr)
        return False

    if len(midi_data.instruments) == 0 or sum(len(i.notes) for i in midi_data.instruments) == 0:
        print(f"    {stem_wav.name}: no notes detected, skipping MIDI output for this stem.")
        return False

    out_mid.parent.mkdir(parents=True, exist_ok=True)
    midi_data.write(str(out_mid))
    return True


# ---------------------------------------------------------------------------
# main pipeline
# ---------------------------------------------------------------------------

def build_song_folder(
    input_file: Path,
    title: str,
    output_dir: Path,
    device: str,
    skip_stems: set,
    keep_temp: bool,
) -> Path:
    slug = slugify(title) or "song"
    song_folder = output_dir / slug
    stems_dir = song_folder / "stems"
    midi_dir = song_folder / "midi"
    stems_dir.mkdir(parents=True, exist_ok=True)
    midi_dir.mkdir(parents=True, exist_ok=True)

    work_dir = Path(tempfile.mkdtemp(prefix="riffhouse_demucs_")) if not keep_temp \
        else song_folder / "_demucs_raw"
    if keep_temp:
        work_dir.mkdir(parents=True, exist_ok=True)

    try:
        track_dir = run_demucs(input_file, device, work_dir)
        bpm = estimate_bpm(input_file)

        kept_stems = [s for s in ALL_STEMS if s not in skip_stems]
        unknown = skip_stems - set(ALL_STEMS)
        if unknown:
            print(f"    Note: ignoring unknown --skip-stems entries: {', '.join(sorted(unknown))}")

        print(f"==> [3/4] Copying {len(kept_stems)} stem(s) into {stems_dir}")
        stems_rel: Dict[str, str] = {}
        for stem in kept_stems:
            src = track_dir / f"{stem}.wav"
            if not src.is_file():
                print(f"    Warning: Demucs did not produce {src.name}, skipping it.")
                continue
            dst = stems_dir / f"{stem}.wav"
            shutil.copyfile(src, dst)
            stems_rel[stem] = f"stems/{stem}.wav"

        print("==> [4/4] Transcribing pitched stems to MIDI with Basic Pitch")
        midi_rel: Dict[str, str] = {}
        to_transcribe = [s for s in PITCHED_STEMS if s in stems_rel]
        if not to_transcribe:
            print("    No pitched stems available to transcribe (all skipped or missing).")
        for stem in to_transcribe:
            print(f"    - {stem}...")
            dst_mid = midi_dir / f"{stem}.mid"
            if transcribe_stem_to_midi(stems_dir / f"{stem}.wav", dst_mid, bpm):
                midi_rel[stem] = f"midi/{stem}.mid"

        song_json = {
            "title": title,
            "bpm": round(bpm, 2),
            "stems": stems_rel,
            "midi": midi_rel,
        }
        (song_folder / "song.json").write_text(json.dumps(song_json, indent=2) + "\n")
        return song_folder
    finally:
        if not keep_temp and work_dir.exists():
            shutil.rmtree(work_dir, ignore_errors=True)


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="riffhouse_import.py",
        description="Build a GHS FX Companion Riff House song folder (Demucs stems + "
                    "Basic Pitch MIDI) from a single audio file.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Example:\n"
            '  python3 tools/riffhouse_import.py ~/Music/suno/midnight_drive.mp3 \\\n'
            '      --title "Midnight Drive" --device auto\n'
            "\n"
            "Output structure:\n"
            "  <output-dir>/midnight-drive/\n"
            "      song.json\n"
            "      stems/{vocals,drums,bass,guitar,piano,other}.wav\n"
            "      midi/{vocals,bass,guitar,piano}.mid\n"
            "\n"
            "Then in the plugin: Riff House -> Import -> song folder -> pick that folder.\n"
        ),
    )
    parser.add_argument("input_file", type=Path, help="Path to the source mp3/wav/etc.")
    parser.add_argument("--title", type=str, default=None,
                        help="Song title stored in song.json (default: input filename without extension).")
    parser.add_argument("--output-dir", type=Path, default=None,
                        help="Parent folder the song's own folder is created under "
                             f"(default: {default_output_dir()}).")
    parser.add_argument("--device", choices=["auto", "cpu", "mps", "cuda"], default="auto",
                        help="Compute device for Demucs/Basic Pitch (default: auto-detect, "
                             "preferring CUDA, then Apple MPS, then CPU).")
    parser.add_argument("--skip-stems", type=str, default="",
                        help="Comma-separated stem names to leave out of the output entirely, "
                             "e.g. 'other' or 'drums,other'.")
    parser.add_argument("--keep-temp", action="store_true",
                        help="Keep Demucs' raw separated-stem output (in <song-folder>/_demucs_raw) "
                             "instead of deleting it after copying the stems you want.")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)

    input_file = args.input_file.expanduser().resolve()
    if not input_file.is_file():
        print(f"riffhouse_import.py: input file not found: {input_file}", file=sys.stderr)
        return 1

    title = args.title or input_file.stem
    output_dir = (args.output_dir.expanduser() if args.output_dir else default_output_dir())
    skip_stems = {s.strip().lower() for s in args.skip_stems.split(",") if s.strip()}

    check_dependencies()
    device = pick_device(args.device)

    print(f"riffhouse_import.py: importing '{title}' from {input_file.name}")
    song_folder = build_song_folder(
        input_file=input_file,
        title=title,
        output_dir=output_dir,
        device=device,
        skip_stems=skip_stems,
        keep_temp=args.keep_temp,
    )

    print()
    print(f"Done. Song folder: {song_folder}")
    print("Open it in GHS FX Companion via Riff House -> Import -> song folder.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
