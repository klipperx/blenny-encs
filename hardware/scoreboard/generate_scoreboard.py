#!/usr/bin/env python3
"""Vintage baseball scoreboard carrier for the T-Display S3.

LilyGO is the center screen. HOME / VISITOR are 2-digit 7-seg wells.
2xAA pack on the back. Lanyard hole in the pediment.
"""

from __future__ import annotations

from pathlib import Path

import trimesh
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union
from trimesh.creation import extrude_polygon

OUT = Path(__file__).resolve().parent

# Scoreboard face (mm). Origin at the visual center of the main panel.
PANEL_W = 118.0
PANEL_H = 68.0
CORNER = 2.2
CORNICE_H = 14.0
LANYARD_R = 3.2

# LilyGO window (active glass) and USB to the right.
SCREEN_W = 48.0
SCREEN_H = 23.6
USB_NOTCH_W = 12.0
USB_NOTCH_H = 8.5

# 0.56" 2-digit 7-seg body
DIGIT_W = 25.4
DIGIT_H = 19.0
DIGIT_Y = -22.0
HOME_X = -28.0
VIS_X = 28.0

# 2xAA holder, back
AA_L = 57.5
AA_W = 32.0
AA_H = 16.5

FACE = 2.4
BACK_SHELL = 2.0
CAVITY = 9.5


def rr(w: float, h: float, r: float, cx: float = 0.0, cy: float = 0.0) -> Polygon:
    r = min(r, w / 2 - 0.05, h / 2 - 0.05)
    return box(cx - w / 2 + r, cy - h / 2 + r, cx + w / 2 - r, cy + h / 2 + r).buffer(r)


def outline() -> Polygon:
    """Old municipal scoreboard: wide panel, stepped cornice, lanyard in the peak."""
    panel = rr(PANEL_W, PANEL_H, CORNER, 0, 0)
    # Cornice / roof
    y0 = PANEL_H / 2 - 0.2
    roof = Polygon(
        [
            (-PANEL_W / 2 - 6, y0),
            (PANEL_W / 2 + 6, y0),
            (PANEL_W / 2 + 6, y0 + 5),
            (18, y0 + CORNICE_H),
            (6.5, y0 + CORNICE_H + 6),
            (6.5, y0 + CORNICE_H + 11),
            (-6.5, y0 + CORNICE_H + 11),
            (-6.5, y0 + CORNICE_H + 6),
            (-18, y0 + CORNICE_H),
            (-PANEL_W / 2 - 6, y0 + 5),
        ]
    ).buffer(0.6)
    hole = Point(0, y0 + CORNICE_H + 8.2).buffer(LANYARD_R)
    # Decorative "stadium lights" along the cornice — shallow circles, not holes
    return unary_union([panel, roof]).difference(hole)


def screen_window() -> Polygon:
    return rr(SCREEN_W, SCREEN_H, 0.6, 0, 8.5)


def usb_notch() -> Polygon:
    return box(PANEL_W / 2 - 1, 8.5 - USB_NOTCH_H / 2, PANEL_W / 2 + 10, 8.5 + USB_NOTCH_H / 2)


def digit_window(cx: float) -> Polygon:
    return rr(DIGIT_W, DIGIT_H, 0.5, cx, DIGIT_Y)


def label_bar(cx: float, text_w: float) -> Polygon:
    return rr(text_w, 4.2, 0.4, cx, DIGIT_Y + DIGIT_H / 2 + 4.4)


def stadium_lights() -> Polygon:
    y = PANEL_H / 2 + 3.6
    xs = [-48, -36, -24, 24, 36, 48]
    return unary_union([Point(x, y).buffer(1.7) for x in xs])


def extrude(poly: Polygon, h: float, z0: float = 0.0) -> trimesh.Trimesh:
    if poly.is_empty or poly.area < 1e-4:
        return trimesh.Trimesh()
    if poly.geom_type == "MultiPolygon":
        return combine([extrude(p, h, z0) for p in poly.geoms if p.area > 1e-4])
    mesh = extrude_polygon(poly, height=h)
    mesh.apply_translation((0, 0, z0))
    return mesh


def combine(parts: list[trimesh.Trimesh]) -> trimesh.Trimesh:
    parts = [p for p in parts if len(p.faces)]
    mesh = trimesh.util.concatenate(parts)
    mesh.merge_vertices()
    mesh.update_faces(mesh.unique_faces())
    mesh.remove_unreferenced_vertices()
    return mesh


def fascia() -> trimesh.Trimesh:
    """Front stadium face: screen + two digit wells, USB notch, lanyard."""
    body = outline()
    windows = unary_union(
        [
            screen_window(),
            digit_window(HOME_X),
            digit_window(VIS_X),
            usb_notch(),
        ]
    )
    face = body.difference(windows)
    frame = (
        body.buffer(-1.0)
        .difference(body.buffer(-3.4))
        .difference(windows.buffer(0.9))
        .difference(usb_notch())
    )
    lights = stadium_lights()
    return combine(
        [
            extrude(face, FACE, 0),
            extrude(frame, 6.5, FACE),
            extrude(lights, 0.9, FACE),
        ]
    )


def back_shell() -> trimesh.Trimesh:
    """Back cover with 2xAA pocket and USB exit."""
    body = outline()
    usb = usb_notch()
    plate = body.difference(usb)
    # Inner rim that seats into the fascia
    inner = body.buffer(-2.2).difference(usb)
    rim = body.buffer(-0.4).difference(inner).difference(usb)
    # AA pocket, opening toward -Z (outside of back)
    aa = rr(AA_L + 1.2, AA_W + 1.2, 1.0, 0, -22.0)
    pocket_floor = plate.difference(aa)
    walls = aa.buffer(2.0).difference(aa)
    walls = walls.intersection(body)
    return combine(
        [
            extrude(pocket_floor, BACK_SHELL, 0),
            extrude(aa.buffer(2.0).difference(aa).intersection(body), AA_H, 0),
            extrude(rim, 4.0, BACK_SHELL),
        ]
    )


def write_stl(mesh: trimesh.Trimesh, path: Path) -> None:
    mesh.export(path, file_type="stl")
    e = mesh.bounding_box.extents
    print(f"wrote {path.name:32} {e[0]:5.1f} x {e[1]:5.1f} x {e[2]:5.1f} mm  {len(mesh.faces)} tris")


def svg_path(poly: Polygon) -> str:
    def ring(coords) -> str:
        return "M " + " ".join(f"{x:.2f},{y:.2f}" for x, y in coords) + " Z"

    if poly.geom_type == "MultiPolygon":
        return " ".join(svg_path(p) for p in poly.geoms)
    d = ring(poly.exterior.coords)
    for hole in poly.interiors:
        d += " " + ring(hole.coords)
    return d


def write_preview() -> None:
    body = outline()
    bits = unary_union(
        [screen_window(), digit_window(HOME_X), digit_window(VIS_X), usb_notch(), stadium_lights()]
    )
    home = label_bar(HOME_X, 28)
    vis = label_bar(VIS_X, 36)
    minx, miny, maxx, maxy = body.bounds
    pad = 6
    w, h = maxx - minx + 2 * pad, maxy - miny + 2 * pad
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.1f} {h:.1f}" width="640">
  <rect width="100%" height="100%" fill="#1a1a14"/>
  <g transform="translate({pad - minx:.1f} {maxy + pad:.1f}) scale(1,-1)">
    <path d="{svg_path(body)}" fill="#2f4f2f" fill-rule="evenodd"/>
    <path d="{svg_path(bits)}" fill="#0d120d"/>
    <path d="{svg_path(home)}" fill="#cfc6a6"/>
    <path d="{svg_path(vis)}" fill="#cfc6a6"/>
  </g>
  <text x="50%" y="62%" text-anchor="middle" fill="#cfc6a6" font-family="Georgia, serif" font-size="11">SCREEN</text>
  <text x="32%" y="78%" text-anchor="middle" fill="#1a1a14" font-family="Georgia, serif" font-size="9">HOME</text>
  <text x="68%" y="78%" text-anchor="middle" fill="#1a1a14" font-family="Georgia, serif" font-size="9">VISITOR</text>
</svg>
"""
    (OUT / "preview.svg").write_text(svg)


def write_kicad_pcb() -> None:
    """KiCad 7 board: edge cuts, lanyard NPTH, silk labels. Opens in 7 and 8."""
    poly = outline()
    # KiCad Y is down; our model is Y up. Flip Y for KiCad.
    coords_k = " ".join(f"(xy {x:.3f} {-y:.3f})" for x, y in poly.exterior.coords)
    hole_y = -(PANEL_H / 2 + CORNICE_H + 8.2)
    silk = f"""
  (gr_text "HOME" (at {HOME_X:.3f} {-(DIGIT_Y + 14.5):.3f} 0)
    (layer "F.SilkS")
    (effects (font (size 2.4 2.4) (thickness 0.3)))
  )
  (gr_text "VISITOR" (at {VIS_X:.3f} {-(DIGIT_Y + 14.5):.3f} 0)
    (layer "F.SilkS")
    (effects (font (size 2.4 2.4) (thickness 0.3)))
  )
  (gr_text "SCREEN" (at 0 {-8.5:.3f} 0)
    (layer "Dwgs.User")
    (effects (font (size 1.8 1.8) (thickness 0.2)))
  )
"""
    pcb = f"""(kicad_pcb (version 20221018) (generator pcbnew)
  (general
    (thickness 1.6)
  )
  (paper "A4")
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (32 "B.Adhes" user "B.Adhesive")
    (33 "F.Adhes" user "F.Adhesive")
    (34 "B.Paste" user)
    (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings")
    (41 "Cmts.User" user "User.Comments")
    (42 "Eco1.User" user "User.Eco1")
    (43 "Eco2.User" user "User.Eco2")
    (44 "Edge.Cuts" user)
    (45 "Margin" user)
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.CrtYd" user "F.Courtyard")
    (48 "B.Fab" user)
    (49 "F.Fab" user)
  )
  (setup
    (pad_to_mask_clearance 0)
  )
  (net 0 "")
  (net 1 "GND")
  (net 2 "VCC_5V")
  (net 3 "VAA")
  (net 4 "HOME_CLK")
  (net 5 "HOME_DIO")
  (net 6 "VIS_CLK")
  (net 7 "VIS_DIO")
  (gr_poly
    (pts
      {coords_k}
    )
    (stroke (width 0.1) (type default))
    (fill none)
    (layer "Edge.Cuts")
  )
  (footprint "lanyard"
    (layer "F.Cu")
    (at 0 {hole_y:.3f})
    (descr "Lanyard hole")
    (attr through_hole exclude_from_pos_files exclude_from_bom)
    (pad "" np_thru_hole circle
      (at 0 0)
      (size 6.5 6.5)
      (drill 6.5)
      (layers "*.Cu" "*.Mask")
    )
  )
{silk}
)
"""
    (OUT / "blenny-scoreboard.kicad_pcb").write_text(pcb)


def main() -> None:
    print(f"panel {PANEL_W} x {PANEL_H} mm + cornice")
    f = fascia()
    b = back_shell()
    write_stl(f, OUT / "scoreboard_fascia.stl")
    write_stl(b, OUT / "scoreboard_back.stl")
    plate = f.copy()
    back = b.copy()
    plate.apply_translation((0, 30, 0))
    back.apply_translation((0, -40, 0))
    write_stl(combine([plate, back]), OUT / "scoreboard_plate.stl")
    write_preview()
    write_kicad_pcb()
    print("wrote preview.svg and blenny-scoreboard.kicad_pcb")


if __name__ == "__main__":
    main()
