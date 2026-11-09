#!/usr/bin/env python3
"""
trio_firmware.py - safe helpers for the ORIGINAL DigiTech TRIO firmware (not TRIO+).

What the official Trio Updater 2.0.2 does (found by static inspection, see docs/TRIO.md):
  * reads an XML manifest (default http://harmandbx.blob.core.windows.net/digi-trio-plus/tp-manifest.xml)
  * for Product "TRIO" (ID 0x42) it offers two components:
        MP  = MCU firmware  Trio_Update.bin  (ARM Cortex-M, 40 KB)
        DSP = DSP firmware  DSP1.ldr         (Analog Devices loader file as ASCII "0xNN" lines)
  * has a "Set Manifest URL..." menu item, so it can be pointed at a local manifest.

Commands
  fetch                    download the stock manifest + original-TRIO MP/DSP, check SHA-256,
                           keep an untouched copy, and write DSP1.bin (raw bytes) next to it.
  ldr2bin IN.ldr OUT.bin   ASCII loader -> raw bytes          (lossless)
  bin2ldr IN.bin OUT.ldr   raw bytes -> ASCII loader (CRLF)   (lossless, byte-identical to stock)
  manifest --dsp FILE.ldr  build a local manifest folder that offers your DSP file plus the
                           STOCK MCU file only, then serve it with  python3 -m http.server

Safety rules baked in
  * The MCU (MP) component is always the stock DigiTech file, byte-checked. This tool will not
    build a manifest with a modified MCU image: a bad MCU is the real way to brick the pedal
    (the updater itself says some MCU firmware "must be programmed directly into the MCU").
  * Stock files are never overwritten.
  * First flash should be the STOCK DSP through your local manifest. That proves the whole path
    with zero change to the pedal before you try anything modified.
"""
import argparse
import hashlib
import shutil
import sys
import urllib.request
from pathlib import Path

MANIFEST_URL = "http://harmandbx.blob.core.windows.net/digi-trio-plus/tp-manifest.xml"
BASE = "http://harmandbx.blob.core.windows.net/digi-trio-plus/OriginalTrio/"
STOCK = {
    # name: (url, sha256, manifest version, release date)   - verified 2026-11-09
    "Trio_Update.bin": (BASE + "Trio_Update.bin",
                        "bab98159a33f4ffe4b62296c6a4edf66aca994488a4d36a028832ef9cc017d22", "3.1.4", "2016-01-08"),
    "DSP1.ldr": (BASE + "DSP1.ldr",
                 "06cc8b655175e8de29db10d2591647d869e656f33ac4cda71fd7a6e059ec5fe4", "3.3.0", "2016-02-16"),
}
DEFAULT_DIR = Path.home() / "Music" / "GHS" / "Trio" / "firmware"


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def ldr_to_bytes(text: str) -> bytes:
    out = bytearray()
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not s:
            continue
        if not (s.startswith("0x") and len(s) == 4):
            raise SystemExit(f"line {i}: expected 0xNN, got {s!r}")
        out.append(int(s, 16))
    return bytes(out)


def bytes_to_ldr(data: bytes) -> str:
    return "".join(f"0x{b:02x}\r\n" for b in data)   # stock file uses lowercase hex + CRLF


def cmd_fetch(a) -> int:
    stock = a.dir / "stock"
    stock.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(MANIFEST_URL, stock / "tp-manifest.xml")
    print(f"manifest -> {stock / 'tp-manifest.xml'}")
    ok = True
    for name, (url, want, ver, _) in STOCK.items():
        dst = stock / name
        if not dst.exists():
            urllib.request.urlretrieve(url, dst)
        got = sha256(dst)
        flag = "OK " if got == want else "MISMATCH"
        ok &= got == want
        print(f"{flag} {name} v{ver} {got}")
    raw = ldr_to_bytes((stock / "DSP1.ldr").read_text())
    (stock / "DSP1.bin").write_bytes(raw)
    print(f"DSP1.bin {len(raw)} bytes (round-trip "
          f"{'identical' if bytes_to_ldr(raw) == (stock / 'DSP1.ldr').read_bytes().decode() else 'DIFFERENT'})")
    if not ok:
        print("A stock file does not match the checksum recorded on 2026-11-09. Do not flash it.")
    return 0 if ok else 1


def cmd_ldr2bin(a) -> int:
    a.out.write_bytes(ldr_to_bytes(a.inp.read_text()))
    print(f"{a.out} {a.out.stat().st_size} bytes")
    return 0


def cmd_bin2ldr(a) -> int:
    a.out.write_bytes(bytes_to_ldr(a.inp.read_bytes()).encode())
    print(f"{a.out} {a.out.stat().st_size} bytes")
    return 0


def cmd_manifest(a) -> int:
    stock = a.dir / "stock"
    mcu = stock / "Trio_Update.bin"
    if not mcu.exists() or sha256(mcu) != STOCK["Trio_Update.bin"][1]:
        raise SystemExit("Stock MCU file missing or changed. Run:  trio_firmware.py fetch")
    dsp = a.dsp.resolve()
    ldr_to_bytes(dsp.read_text())          # refuses anything that is not a clean ASCII loader file
    is_stock = sha256(dsp) == STOCK["DSP1.ldr"][1]
    serve = a.dir / ("serve-stock" if is_stock else "serve-custom")
    if serve.exists():
        shutil.rmtree(serve)
    (serve / "OriginalTrio").mkdir(parents=True)
    shutil.copyfile(mcu, serve / "OriginalTrio" / "Trio_Update.bin")
    shutil.copyfile(dsp, serve / "OriginalTrio" / "DSP1.ldr")
    url = f"http://127.0.0.1:{a.port}/OriginalTrio/"
    _, _, mver, mdate = STOCK["Trio_Update.bin"]
    xml = (
        '<Family Name="TRIO" ID="0x63">\r\n'
        '    <Product Name="TRIO" ID="0x42">\r\n'
        '        <Components>\r\n'
        f'            <Component Name="MP" Version="{mver}" ReleaseDate="{mdate}" URL="{url}Trio_Update.bin" />\r\n'
        f'            <Component Name="DSP" Version="{a.dsp_version}" ReleaseDate="{a.date}" URL="{url}DSP1.ldr" />\r\n'
        '        </Components>\r\n'
        '    </Product>\r\n'
        '</Family>\r\n')
    (serve / "tp-manifest.xml").write_text(xml)
    print(f"Local manifest folder: {serve}")
    print(f"  DSP  : {'STOCK (safe pipeline test)' if is_stock else 'CUSTOM'}  {sha256(dsp)}")
    print(f"  MCU  : stock {STOCK['Trio_Update.bin'][1][:16]}... (never modified)")
    print("Next:")
    print(f"  1. cd \"{serve}\" && python3 -m http.server {a.port} --bind 127.0.0.1")
    print("  2. Open Trio Updater > menu > Set Manifest URL... >")
    print(f"     http://127.0.0.1:{a.port}/tp-manifest.xml")
    print("  3. Connect the TRIO by USB, let the updater read the versions, update DSP only.")
    print("  4. Afterwards: Set Manifest URL back to default (or reset) in the updater.")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dir", type=Path, default=DEFAULT_DIR, help=f"working folder (default {DEFAULT_DIR})")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fetch").set_defaults(fn=cmd_fetch)
    s = sub.add_parser("ldr2bin"); s.add_argument("inp", type=Path); s.add_argument("out", type=Path); s.set_defaults(fn=cmd_ldr2bin)
    s = sub.add_parser("bin2ldr"); s.add_argument("inp", type=Path); s.add_argument("out", type=Path); s.set_defaults(fn=cmd_bin2ldr)
    s = sub.add_parser("manifest")
    s.add_argument("--dsp", type=Path, required=True, help="DSP1.ldr to offer (start with the stock one)")
    s.add_argument("--dsp-version", default="3.3.0",
                   help="version string to advertise; the updater may only offer versions newer than the pedal's")
    s.add_argument("--date", default="2016-02-16")
    s.add_argument("--port", type=int, default=8765)
    s.set_defaults(fn=cmd_manifest)
    a = p.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
