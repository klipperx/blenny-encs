#!/usr/bin/env python3
"""Pack glow GIFs: crushed blacks, quiet mids, lifted cyan shoulder."""

from __future__ import annotations

import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 320, 170
OUT = os.path.join(ROOT, "src", "toys")
GIFS = [
    ("movie1", os.path.join(ROOT, "assets", "toys", "movie1_glow.gif")),
    ("movie2", os.path.join(ROOT, "assets", "toys", "movie2_glow.gif")),
    ("movie8", os.path.join(ROOT, "assets", "toys", "movie8_f_glow.gif")),
]
FRAME_BYTES = (W * H + 1) // 2


def pack_frame(im_rgb: Image.Image) -> bytes:
    px = im_rgb.load()
    out = bytearray(FRAME_BYTES)
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            cyan = max(0.0, (g * 0.40 + b * 0.60) - r * 0.25)
            n = cyan / 255.0
            if n < 0.07:
                n = 0.0
            else:
                n = (n - 0.07) / 0.93
            if n < 0.45:
                n = n**1.25
            else:
                u = (n - 0.45) / 0.55
                u = u**0.85
                n = 0.45 + u * 0.48
            lvl = int(n * 15.0 + 0.5)
            if lvl > 13:
                lvl = 13
            if lvl < 0:
                lvl = 0
            idx = y * W + x
            if (idx & 1) == 0:
                out[idx >> 1] = (lvl << 4) | (out[idx >> 1] & 0x0F)
            else:
                out[idx >> 1] = (out[idx >> 1] & 0xF0) | lvl
    return bytes(out)


def frames_4bpp(path: str) -> list[bytes]:
    im = Image.open(path)
    out: list[bytes] = []
    i = 0
    while True:
        try:
            im.seek(i)
        except EOFError:
            break
        fr = im.convert("RGB").resize((W, H), Image.Resampling.LANCZOS)
        out.append(pack_frame(fr))
        i += 1
    return out


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    for name, path in GIFS:
        frs = frames_4bpp(path)
        blob = b"".join(frs)
        dest = os.path.join(OUT, f"{name}.bin")
        with open(dest, "wb") as f:
            f.write(blob)
        print(f"{name}: {len(frs)} frames -> {dest} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
