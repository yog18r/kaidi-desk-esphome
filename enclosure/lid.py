"""Raised, vented lid for the box made by enclosure.py.

- a flat vented plate with a 5 mm frame (lip) underneath that sits on the box walls, giving room for the ESP32;
- a solid boss at each corner post, so the M3 screws go through into the captive nuts in the box;
- a tongue that drops into each front jack notch to hold the RJ50 jacks down;
- short rim strips inside the left, right and back walls to line the lid up;
- raised DESK and HANDSET labels on the front edge.

It uses the same coordinates as enclosure.py and prints plate-down (lip, tongues and bosses pointing up), no supports.
Screws: M3 x 14 socket head (M3 x 12 also works), sitting in 3.3 mm deep counterbores so the heads are just below
the lid surface. M3 x 16 is too long.

Run:  python lid.py   ->  writes desk_controller_lid.stl
"""
from manifold3d import CrossSection, FillRule
from matplotlib.font_manager import FontProperties
from matplotlib.textpath import TextPath

from enclosure import (IN_X, IN_Y, OUT_X, OUT_Y, POSTS, POST_R, M3_CLEAR, INNER_TOP, LID, PCB_TOP,
                       JACK_CX, JACK_W, NUT_DEPTH, NUT_T, box, cyl, save)

# Raised labels on the lid's front edge, centred over each jack (RJ1 DESK at x 18.7, RJ2 HANDSET at x 38.7).
# The box hangs under the desk upside down (base against the desk), so the text is turned to read correctly
# in place: text "right" runs along -x and text "up" along -z.
LABELS = [(JACK_CX[0], "DESK"), (JACK_CX[1], "HANDSET")]
LABEL_CAP = 3.5          # capital letter height (mm)
LABEL_MAX_W = 18.0       # max label width (jack centres are 20 mm apart)
LABEL_RAISE = 0.8        # how far the letters stand out from the face

RAISE = 5.0              # height of the lip under the plate (extra room inside)
JACK_TOP_H = 13.8        # jack height above the PCB top; change it if your jacks differ
JACK_PRESS_GAP = 0.2     # tongue stops this far above the jack top (no crushing)
NOTCH_M = 0.6            # same margin enclosure.py uses for the notch either side of the jack
TONGUE_CLEAR = 0.3       # side clearance between tongue and notch
RIM_T = 1.2              # locating rim thickness
RIM_H = 2.0              # how far the rim drops inside the box walls
RIM_CLEAR = 0.3          # gap between rim and the box inner wall
CBORE_D = 6.2            # counterbore for an M3 socket head (5.5 mm)
CBORE_DEPTH = 3.3        # socket head is 3.0 mm tall: sits just below the surface

Z_SEAT = INNER_TOP                  # top of the box walls, where the lip sits
Z_PLATE = Z_SEAT + RAISE            # underside of the plate
Z_TOP = Z_PLATE + LID               # outside top of the lid


def label(text, cx, zc, y_face):
    """Raised text on the front face (y = y_face, facing -y), centred at (cx, zc)."""
    prop = FontProperties(family="DejaVu Sans", weight="bold")
    em = LABEL_CAP / 0.73                                    # DejaVu cap height is ~0.73 em
    path = TextPath((0, 0), text, size=em, prop=prop)
    polys = [p.tolist() for p in path.to_polygons(closed_only=True)]
    cs = CrossSection(polys, FillRule.EvenOdd)
    (x0, y0, x1, y1) = cs.bounds()
    s = min(1.0, LABEL_MAX_W / (x1 - x0))
    cs = cs.translate([-(x0 + x1) / 2, -(y0 + y1) / 2]).scale([s, s])
    solid = cs.extrude(LABEL_RAISE + 0.01)
    # (u, v, w) -> (x, y, z) = (cx - u, y_face + 0.01 - w, zc - v)
    return solid.transform([[-1, 0, 0, cx], [0, 0, -1, y_face + 0.01], [0, -1, 0, zc]])


def make_raised_lid():
    # plate
    lid = box(OUT_X[0], OUT_X[1], OUT_Y[0], OUT_Y[1], Z_PLATE, Z_TOP)

    # lip: a frame the same footprint as the box walls
    frame = box(OUT_X[0], OUT_X[1], OUT_Y[0], OUT_Y[1], Z_SEAT, Z_PLATE + 0.01)
    frame -= box(IN_X[0], IN_X[1], IN_Y[0], IN_Y[1], Z_SEAT - 1, Z_PLATE + 1)
    lid += frame

    # corner bosses over the box posts, full lip height
    for x, y in POSTS:
        lid += cyl(x, y, Z_SEAT, Z_PLATE + 0.01, POST_R)

    # tongues down into the jack notches, pressing on the jack tops (wall thickness only)
    tongue_bottom = PCB_TOP + JACK_TOP_H + JACK_PRESS_GAP
    half = JACK_W / 2 + NOTCH_M - TONGUE_CLEAR
    for cx in JACK_CX:
        lid += box(cx - half, cx + half, OUT_Y[0], IN_Y[0], tongue_bottom, Z_SEAT + 0.01)

    # locating rim strips just inside the left, right and back walls (kept clear of the corner posts)
    y_a, y_b = POSTS[0][1] + POST_R + 0.5, POSTS[1][1] - POST_R - 0.5
    x_a, x_b = POSTS[0][0] + POST_R + 0.5, POSTS[2][0] - POST_R - 0.5
    zr = Z_SEAT - RIM_H
    lx = IN_X[0] + RIM_CLEAR
    rx = IN_X[1] - RIM_CLEAR
    by = IN_Y[1] - RIM_CLEAR
    lid += box(lx, lx + RIM_T, y_a, y_b, zr, Z_SEAT + 0.01)
    lid += box(rx - RIM_T, rx, y_a, y_b, zr, Z_SEAT + 0.01)
    lid += box(x_a, x_b, by - RIM_T, by, zr, Z_SEAT + 0.01)

    # screw holes through plate and bosses, counterbored so the socket heads sit flush
    for x, y in POSTS:
        lid -= cyl(x, y, Z_SEAT - 1, Z_TOP + 1, M3_CLEAR / 2)
        lid -= cyl(x, y, Z_TOP - CBORE_DEPTH, Z_TOP + 1, CBORE_D / 2)

    # vent slots across the plate (2.5 mm wide, 5 mm pitch), clear of the bosses and the frame
    x0, x1 = POSTS[0][0] + POST_R + 2.0, POSTS[2][0] - POST_R - 2.0
    y0, y1 = IN_Y[0] + 4.0, IN_Y[1] - 4.0
    n = int((x1 - x0 + 2.5) // 5.0)
    start = x0 + ((x1 - x0) - (n * 5.0 - 2.5)) / 2
    for i in range(n):
        sx = start + i * 5.0
        lid -= box(sx, sx + 2.5, y0, y1, Z_PLATE - 1, Z_TOP + 1)

    # jack labels on the front edge
    for cx, text in LABELS:
        lid += label(text, cx, (Z_SEAT + Z_TOP) / 2, OUT_Y[0])

    # flip so the plate is on the print bed
    lid = lid.rotate([180, 0, 0])
    zmin = lid.bounding_box()[2]
    return lid.translate([0, 0, -zmin]), tongue_bottom, n


if __name__ == "__main__":
    lid, tongue_bottom, n = make_raised_lid()
    save(lid, "desk_controller_lid.stl")
    nut_top = INNER_TOP - NUT_DEPTH
    seat = Z_TOP - CBORE_DEPTH
    print(f"room above PCB: {Z_PLATE - PCB_TOP:.1f} mm; "
          f"tongues end {tongue_bottom - PCB_TOP:.1f} mm above the PCB; {n} vent slots; "
          f"screw seat to nut top {seat - nut_top:.1f} mm, to nut bottom {seat - nut_top + NUT_T:.1f} mm, "
          f"to end of hole {seat - (nut_top - NUT_T - 4):.1f} mm")
