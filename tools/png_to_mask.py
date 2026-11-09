#!/usr/bin/env python3
"""Stdlib PNG -> 8-bit alpha C header. No third-party deps."""

import struct
import sys
import zlib
from pathlib import Path


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while pos + 8 <= len(data):
        ln, typ = struct.unpack(">I4s", data[pos : pos + 8])
        chunk = data[pos + 8 : pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat.extend(chunk)
        elif typ == b"IEND":
            break
    raw = zlib.decompress(bytes(idat))
    if bit_depth != 8:
        raise SystemExit(f"unsupported bit depth {bit_depth}")
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    stride = width * ch
    rows = []
    i = 0
    prev = bytes(stride)
    for _ in range(height):
        filt = raw[i]
        cur = bytearray(raw[i + 1 : i + 1 + stride])
        i += 1 + stride
        for x, val in enumerate(cur):
            a = cur[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if filt == 1:
                cur[x] = (val + a) & 255
            elif filt == 2:
                cur[x] = (val + b) & 255
            elif filt == 3:
                cur[x] = (val + ((a + b) // 2)) & 255
            elif filt == 4:
                cur[x] = (val + paeth(a, b, c)) & 255
            elif filt != 0:
                raise SystemExit(f"bad filter {filt}")
        rows.append(bytes(cur))
        prev = bytes(cur)
    pixels = []
    for row in rows:
        line = []
        for x in range(width):
            o = x * ch
            if color_type == 0:
                lum, a = row[o], 255
            elif color_type == 2:
                r, g, b = row[o], row[o + 1], row[o + 2]
                lum, a = (r + g + b) // 3, 255
            elif color_type == 4:
                lum, a = row[o], row[o + 1]
            else:
                r, g, b, a = row[o], row[o + 1], row[o + 2], row[o + 3]
                lum = (r + g + b) // 3
            # Dark ink on light background -> coverage
            cover = (255 - lum) * a // 255
            line.append(cover)
        pixels.append(line)
    return width, height, pixels


def bbox(pixels, thresh=12):
    h, w = len(pixels), len(pixels[0])
    ys = [y for y, row in enumerate(pixels) if any(v > thresh for v in row)]
    xs = [x for x in range(w) if any(pixels[y][x] > thresh for y in range(h))]
    return xs[0], ys[0], xs[-1] + 1, ys[-1] + 1


def scale_nn(pixels, x0, y0, x1, y1, nw, nh):
    src_w, src_h = x1 - x0, y1 - y0
    out = []
    for y in range(nh):
        sy = y0 + y * src_h // nh
        row = pixels[sy]
        out.append([row[x0 + x * src_w // nw] for x in range(nw)])
    return out


def main():
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    max_w = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    max_h = int(sys.argv[4]) if len(sys.argv) > 4 else 130
    w, h, pixels = read_png(src)
    x0, y0, x1, y1 = bbox(pixels)
    src_w, src_h = x1 - x0, y1 - y0
    scale = min(max_w / src_w, max_h / src_h)
    nw = max(1, int(src_w * scale))
    nh = max(1, int(src_h * scale))
    mask = scale_nn(pixels, x0, y0, x1, y1, nw, nh)
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        f"static constexpr int kLogoW = {nw};",
        f"static constexpr int kLogoH = {nh};",
        "static const uint8_t kLogoMask[] = {",
    ]
    flat = [str(v) for row in mask for v in row]
    for i in range(0, len(flat), 24):
        lines.append("  " + ", ".join(flat[i : i + 24]) + ",")
    lines.append("};")
    dst.write_text("\n".join(lines) + "\n")
    print(f"wrote {dst} {nw}x{nh} ({len(flat)} bytes)")


if __name__ == "__main__":
    main()
