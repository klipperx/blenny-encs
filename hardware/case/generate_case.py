#!/usr/bin/env python3
"""Generate snap-fit T-Display S3 Touch badge STLs.

Worn landscape: USB-C on the right, lanyard on top, 6 mm battery well on the back.
"""

from __future__ import annotations

from pathlib import Path

import trimesh
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union
from trimesh.creation import extrude_polygon

OUT = Path(__file__).resolve().parent

# --- board ---
PCB_L = 64.5
PCB_W = 25.5
STACK_H = 8.3
CLEAR = 0.22
SCREEN_L = 47.8
SCREEN_W = 23.4
SCREEN_FROM_USB = 14.2
USB_W = 12.6

# --- case (X = board length, Y = board width; USB at +X, lanyard at +Y) ---
WALL = 2.15
FACE = 1.7
FLOOR = 1.3
LIP_H = 1.8
CORNER = 6.0
LANYARD_R = 2.8
BAT_WELL_H = 6.0  # 0.6 cm clearance for a ~5 mm pouch

CAV_X = PCB_L + 2 * CLEAR
CAV_Y = PCB_W + 2 * CLEAR
BODY_X = CAV_X + 2 * WALL + 0.8
BODY_Y = CAV_Y + 2 * WALL
INNER_H = STACK_H + 0.35
# Well sits away from USB so the JST bay stays open.
BAT_WELL_X = CAV_X - 20.0
BAT_WELL_Y = CAV_Y - 1.6


def rr(w: float, h: float, r: float, cx: float = 0.0, cy: float = 0.0) -> Polygon:
    r = min(r, w / 2 - 0.05, h / 2 - 0.05)
    return box(cx - w / 2 + r, cy - h / 2 + r, cx + w / 2 - r, cy + h / 2 - r).buffer(r)


def body_profile() -> Polygon:
    body = rr(BODY_X, BODY_Y, CORNER, 0, 0)
    tab_cy = BODY_Y / 2 + 6.4
    tab = Point(0, tab_cy).buffer(7.2)
    stem = box(-6.4, BODY_Y / 2 - 2.4, 6.4, tab_cy)
    hole = Point(0, tab_cy + 0.4).buffer(LANYARD_R)
    return unary_union([body, tab, stem]).buffer(0.15).difference(hole)


def cavity() -> Polygon:
    return rr(CAV_X, CAV_Y, 1.4, -0.1, 0)


def screen_hole() -> Polygon:
    cx = BODY_X / 2 - WALL - 0.4 - SCREEN_FROM_USB - SCREEN_L / 2
    return rr(SCREEN_L, SCREEN_W, 1.0, cx, 0)


def usb_slot(extra: float = 0.0) -> Polygon:
    return box(
        BODY_X / 2 - WALL - 2.2,
        -USB_W / 2 - extra,
        BODY_X / 2 + 4.0,
        USB_W / 2 + extra,
    )


def button_notches() -> Polygon:
    """GPIO14 and BOOT, through the front face, near USB on the long edges."""
    x = BODY_X / 2 - WALL - 8.6
    w, d = 10.0, 5.0
    top = rr(w, d, 2.4, x, BODY_Y / 2 - d / 2 + 1.1)
    bot = rr(w, d, 2.4, x, -BODY_Y / 2 + d / 2 - 1.1)
    return unary_union([top, bot])


def jst_cutout() -> Polygon:
    """Finger access to the rear 1.25 mm battery connector, near USB."""
    cx = BODY_X / 2 - WALL - 10.0
    return rr(20.0, 16.0, 4.0, cx, 0)


def battery_well() -> Polygon:
    return rr(BAT_WELL_X, BAT_WELL_Y, 2.8, -6.4, 0)


def snap_windows() -> Polygon:
    xs = (-16.0, 16.0)
    holes = []
    for x in xs:
        holes.append(box(x - 2.1, -BODY_Y / 2 - 1, x + 2.1, -BODY_Y / 2 + WALL + 0.15))
        holes.append(box(x - 2.1, BODY_Y / 2 - WALL - 0.15, x + 2.1, BODY_Y / 2 + 1))
    return unary_union(holes)


def extrude(poly: Polygon, h: float, z0: float = 0.0) -> trimesh.Trimesh:
    if poly.is_empty or poly.area < 1e-4:
        return trimesh.Trimesh()
    if poly.geom_type == "MultiPolygon":
        meshes = [extrude(p, h, z0) for p in poly.geoms if p.area > 1e-4]
        return trimesh.util.concatenate(meshes)
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


def front() -> trimesh.Trimesh:
    outline = body_profile()
    cav = cavity()
    window = screen_hole()
    usb = usb_slot()
    buttons = button_notches()
    snaps = snap_windows()

    face = outline.difference(window).difference(usb).difference(buttons)
    wall_ring = outline.difference(cav).difference(usb).difference(buttons)
    wall_mid = wall_ring.difference(snaps)
    ledge = cav.difference(cav.buffer(-1.05)).difference(window)

    z_wall = FACE
    z_mid = FACE + 3.4
    z_top = FACE + 5.6
    return combine(
        [
            extrude(face, FACE, 0.0),
            extrude(ledge, 0.9, z_wall),
            extrude(wall_ring, z_mid - z_wall, z_wall),
            extrude(wall_mid, z_top - z_mid, z_mid),
            extrude(wall_ring, FACE + INNER_H - z_top, z_top),
        ]
    )


def back() -> trimesh.Trimesh:
    """PCB rim on the bed, 6 mm battery well opening up."""
    outline = body_profile()
    usb = usb_slot(0.2)
    jst = jst_cutout()
    well = battery_well()

    rim_outer = cavity().buffer(-0.08)
    rim_inner = rim_outer.buffer(-1.15)
    rim = rim_outer.difference(rim_inner).difference(usb).difference(jst)

    floor = outline.difference(usb).difference(jst)
    walls = outline.difference(well).difference(usb).difference(jst)

    nubs = []
    for y_sign in (-1, 1):
        for x in (-16.0, 16.0):
            y0 = y_sign * (CAV_Y / 2 - 0.15)
            y1 = y_sign * (CAV_Y / 2 + 0.72)
            nub = Polygon(
                [
                    (x - 1.7, y0),
                    (x + 1.7, y0),
                    (x + 0.7, y1),
                    (x - 0.7, y1),
                ]
            )
            nubs.append(extrude(nub, 1.8, 0.15))

    z_floor = LIP_H
    z_well = LIP_H + FLOOR
    return combine(
        [
            extrude(rim, LIP_H, 0.0),
            *nubs,
            extrude(floor, FLOOR, z_floor),
            extrude(walls, BAT_WELL_H, z_well),
        ]
    )


def write_stl(mesh: trimesh.Trimesh, path: Path) -> None:
    mesh.export(path, file_type="stl")
    extents = mesh.bounding_box.extents
    print(
        f"wrote {path.name:28}  {extents[0]:5.1f} x {extents[1]:5.1f} x {extents[2]:5.1f} mm  "
        f"{len(mesh.faces)} tris"
    )


def plate(front_m: trimesh.Trimesh, back_m: trimesh.Trimesh) -> trimesh.Trimesh:
    f = front_m.copy()
    b = back_m.copy()
    gap = 8.0
    f.apply_translation((0, BODY_Y / 2 + gap / 2, 0))
    b.apply_translation((0, -BODY_Y / 2 - gap / 2, 0))
    return combine([f, b])


def svg_path(poly) -> str:
    def ring(coords) -> str:
        pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in coords)
        return f"M {pts} Z"

    if poly.geom_type == "MultiPolygon":
        return " ".join(svg_path(p) for p in poly.geoms)
    d = ring(poly.exterior.coords)
    for hole in poly.interiors:
        d += " " + ring(hole.coords)
    return d


def write_preview() -> None:
    outline = body_profile()
    minx, miny, maxx, maxy = outline.bounds
    pad = 4
    w = maxx - minx + 2 * pad
    h = maxy - miny + 2 * pad
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.1f} {h:.1f}" width="520">
  <rect width="100%" height="100%" fill="#111"/>
  <g transform="translate({pad - minx:.1f} {maxy + pad:.1f}) scale(1,-1)">
    <path d="{svg_path(outline)}" fill="#0aa5a0" fill-rule="evenodd"/>
    <path d="{svg_path(usb_slot())}" fill="#041b1a"/>
    <path d="{svg_path(button_notches())}" fill="#041b1a"/>
    <path d="{svg_path(jst_cutout())}" fill="#062e2c"/>
    <path d="{svg_path(battery_well())}" fill="#0b6e6a" fill-opacity="0.45"/>
  </g>
</svg>
"""
    (OUT / "preview.svg").write_text(svg)


def main() -> None:
    back_h = LIP_H + FLOOR + BAT_WELL_H
    print(
        f"body {BODY_X:.1f} x {BODY_Y:.1f} mm landscape, "
        f"front {FACE + INNER_H:.1f} mm, back {back_h:.1f} mm (well {BAT_WELL_H:.1f})"
    )
    f = front()
    b = back()
    write_stl(f, OUT / "blenny_badge_front.stl")
    write_stl(b, OUT / "blenny_badge_back.stl")
    write_stl(plate(f, b), OUT / "blenny_badge_plate.stl")
    write_preview()
    print("wrote preview.svg")


if __name__ == "__main__":
    main()
