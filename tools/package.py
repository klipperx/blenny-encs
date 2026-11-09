#!/usr/bin/env python3
"""Copy firmware + LittleFS pieces into dist/ and zip them for a release."""

from __future__ import annotations

import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"

VARIANTS = {
    "cst328": "t-display-s3-touch",
    "cst816": "t-display-s3-touch-cst816",
    "scoreboard": "t-display-s3-scoreboard",
}

BOOT_APP0 = (
    Path.home()
    / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
)

FILES = (
    ("bootloader.bin", "bootloader.bin"),
    ("partitions.bin", "partitions.bin"),
    ("firmware.bin", "firmware.bin"),
)


def littlefs_bin(env: str | None = None) -> Path:
    if env:
        path = ROOT / ".pio" / "build" / env / "littlefs.bin"
        if path.exists():
            return path
        raise SystemExit(f"missing {path} — run: pio run -e {env} -t buildfs")
    for name in VARIANTS.values():
        path = ROOT / ".pio" / "build" / name / "littlefs.bin"
        if path.exists():
            return path
    raise SystemExit("no littlefs.bin — run: pio run -t buildfs")


def package_one(name: str, env: str, fs: Path) -> Path:
    build = ROOT / ".pio" / "build" / env
    out_dir = DIST / f"blenny-{name}"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    for src_name, dest_name in FILES:
        src = build / src_name
        if not src.exists():
            raise SystemExit(f"missing {src} — run: pio run -e {env}")
        shutil.copy2(src, out_dir / dest_name)
    if not BOOT_APP0.exists():
        raise SystemExit(f"missing {BOOT_APP0}")
    shutil.copy2(BOOT_APP0, out_dir / "boot_app0.bin")
    shutil.copy2(fs, out_dir / "littlefs.bin")
    zpath = DIST / f"blenny-{name}.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(out_dir.iterdir()):
            zf.write(p, p.name)
    print(f"wrote {zpath} ({zpath.stat().st_size} bytes)")
    print(f"  littlefs from {fs}")
    return zpath


def main() -> None:
    DIST.mkdir(exist_ok=True)
    wanted = sys.argv[1:] or list(VARIANTS)
    for name in wanted:
        env = VARIANTS.get(name)
        if not env:
            raise SystemExit(f"unknown variant {name}; try: {' '.join(VARIANTS)}")
        # Prefer this env's FS; fall back to any built FS for shared roster images.
        try:
            fs = littlefs_bin(env)
        except SystemExit:
            fs = littlefs_bin(None)
        package_one(name, env, fs)


if __name__ == "__main__":
    main()
