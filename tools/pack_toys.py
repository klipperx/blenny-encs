#!/usr/bin/env python3
"""Compress Silly Animation clips frame by frame for the firmware.

Input: src/toys/<name>.bin, raw 4-bit glow frames (kToyAnimFrameBytes each).
Output: src/toys/<name>.z, embedded via platformio.ini:
  "TZ01" | u16 frames | u16 delay_ms | u32 offsets[frames + 1] | zlib(frame0) | zlib(frame1)...
Offsets are from the start of the file; the badge inflates one frame at a time.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAME_BYTES = (320 * 170 + 1) // 2


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="+", help="clip names under src/toys, e.g. movie1")
    ap.add_argument("--delay", type=int, default=60, help="ms per frame stored in the header")
    args = ap.parse_args()

    for name in args.names:
        raw = (ROOT / "src" / "toys" / f"{name}.bin").read_bytes()
        if len(raw) % FRAME_BYTES:
            raise SystemExit(f"{name}.bin is not a whole number of frames")
        frames = len(raw) // FRAME_BYTES
        blobs = [zlib.compress(raw[i * FRAME_BYTES:(i + 1) * FRAME_BYTES], 9) for i in range(frames)]
        header = 8 + 4 * (frames + 1)
        offsets = [header]
        for b in blobs:
            offsets.append(offsets[-1] + len(b))
        out = b"TZ01" + struct.pack("<HH", frames, args.delay)
        out += struct.pack(f"<{frames + 1}I", *offsets) + b"".join(blobs)
        # Round-trip check before anything ships.
        for i, b in enumerate(blobs):
            assert zlib.decompress(b) == raw[i * FRAME_BYTES:(i + 1) * FRAME_BYTES]
        (ROOT / "src" / "toys" / f"{name}.z").write_bytes(out)
        print(f"{name}: {frames} frames, {len(raw) // 1024} KB -> {len(out) // 1024} KB")


if __name__ == "__main__":
    main()
