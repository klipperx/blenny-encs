#!/usr/bin/env python3
"""Flash a Blenny badge from dist/ pieces. Uses 115200 / --no-stub because 921600 flakes."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
ESPTOOL = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"

VARIANTS = ("cst328", "cst816", "scoreboard")

PARTS = (
    (0x0, "bootloader.bin"),
    (0x8000, "partitions.bin"),
    (0xE000, "boot_app0.bin"),
    (0x10000, "firmware.bin"),
    (0xC90000, "littlefs.bin"),
)

# App image without LittleFS — keeps NVS (id/nick/met/wifi) and on-badge FS intact.
FIRMWARE_PARTS = tuple(p for p in PARTS if p[1] != "littlefs.bin")

# Roster / facts only — keeps firmware and NVS intact.
LITTLEFS_PARTS = tuple(p for p in PARTS if p[1] == "littlefs.bin")


def esptool() -> list[str]:
    if ESPTOOL.exists():
        return [sys.executable, str(ESPTOOL)]
    return [sys.executable, "-m", "esptool"]


def find_port(explicit: str | None) -> str:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("pip install pyserial") from exc

    if explicit:
        return explicit
    ports = list(list_ports.comports())
    for p in ports:
        blob = f"{p.device} {p.description} {p.hwid}".lower()
        if any(s in blob for s in ("usbmodem", "wch", "esp32", "usbserial", "lilygo", "cdc")):
            return p.device
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        raise SystemExit("no serial ports found")
    print("ports:")
    for p in ports:
        print(f"  {p.device:24} {p.description}")
    raise SystemExit("pass --port")


def load_dir(path: Path) -> Path:
    missing = [name for _, name in PARTS if not (path / name).exists()]
    if missing:
        raise SystemExit(f"{path} is missing {', '.join(missing)}")
    return path


def extract_zip(zpath: Path) -> Path:
    tmp = Path(tempfile.mkdtemp(prefix="blenny-flash-"))
    with zipfile.ZipFile(zpath) as zf:
        zf.extractall(tmp)
    return load_dir(tmp)


def find_image(args: argparse.Namespace) -> Path:
    if args.image:
        path = Path(args.image)
        if path.is_dir():
            return load_dir(path)
        if path.suffix.lower() == ".zip" and path.exists():
            return extract_zip(path)
        raise SystemExit(f"pass a blenny-*.zip or a folder of .bin pieces, not {path}")
    name = args.variant or "cst816"
    if name not in VARIANTS:
        raise SystemExit(f"unknown variant {name}; try: {' '.join(VARIANTS)}")
    folder = DIST / f"blenny-{name}"
    zpath = DIST / f"blenny-{name}.zip"

    # Prefer the newer artifact. A stale dist/ folder previously masked a fresh zip
    # and left badges with an old roster (ERR unknown id …).
    candidates: list[tuple[float, Path, str]] = []
    if folder.exists() and (folder / "littlefs.bin").exists():
        candidates.append(((folder / "littlefs.bin").stat().st_mtime, folder, "dir"))
    if zpath.exists():
        candidates.append((zpath.stat().st_mtime, zpath, "zip"))
    if not candidates:
        raise SystemExit(
            f"missing {folder} and {zpath}\n"
            "build with: pio run && pio run -t buildfs && python3 tools/package.py\n"
            "or download a release zip and pass --image"
        )
    candidates.sort(key=lambda t: t[0], reverse=True)
    _mtime, path, kind = candidates[0]
    if kind == "dir":
        return load_dir(path)
    return extract_zip(path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Flash a Blenny badge")
    parser.add_argument("--variant", choices=VARIANTS, help="board type (default: cst816)")
    parser.add_argument("--image", help="blenny-*.zip or a folder of .bin pieces")
    parser.add_argument("--port", help="USB serial device")
    parser.add_argument("--baud", type=int, default=115200)
    only = parser.add_mutually_exclusive_group()
    only.add_argument(
        "--firmware-only",
        action="store_true",
        help="flash bootloader/partitions/app only; leave LittleFS and NVS alone",
    )
    only.add_argument(
        "--littlefs-only",
        action="store_true",
        help="flash LittleFS only (roster/facts); leave firmware and NVS alone",
    )
    args = parser.parse_args()

    image = find_image(args)
    port = find_port(args.port)
    if args.firmware_only:
        parts = FIRMWARE_PARTS
        kind = "firmware-only"
    elif args.littlefs_only:
        parts = LITTLEFS_PARTS
        kind = "littlefs-only"
    else:
        parts = PARTS
        kind = "full"
    flash_args: list[str] = []
    for offset, name in parts:
        flash_args += [hex(offset), str(image / name)]
    cmd = esptool() + [
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        str(args.baud),
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "--no-stub",
        "write_flash",
        "-z",
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        "16MB",
        *flash_args,
    ]
    print(f"flashing ({kind}) {image} -> {port}")
    print("+", " ".join(cmd))
    subprocess.check_call(cmd)


if __name__ == "__main__":
    main()
