"""Under-desk box for the Desk Controller V3 board (58 x 72 mm, two RJ50 jacks) with the ESP32 plugged in.

Coordinates follow the PCB: x to the right (antenna end at x < 0, USB end at the right),
y from the jack edge (y = 0) to the back, z up from the outside of the base plate.
EasyEDA's coordinates map across as x = x, y = y_easyeda + 14.
The base (with the two screw flanges) goes against the underside of the desk; the lid faces the floor.

The PCB is screwed down: four M3 x 8 socket screws go through its holes and cut their own thread into blind
holes in the risers. The holes stop 0.6 mm short of the base's outside face, so nothing pokes out against the desk.
The lid is made by lid.py.

Run:  python enclosure.py   ->  writes desk_controller_box.stl
"""
import math
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import Manifold

OUT = Path(__file__).parent
SEG = 48  # circle resolution

# ---- PCB and parts (mm) ----
PCB_W = 58.0             # x
PCB_D = 72.0             # y (jack edge to back)
PCB_T = 1.6
PCB_HOLES = [(3.5, 3.5), (54.5, 3.5), (3.5, 68.5), (54.5, 68.5)]   # 3.2mm holes
ESP_LEFT = 4.0 - 9.0     # pin 1 at x = 4, antenna end 9 mm further left
HEADER_H = 8.5           # female header height (C319202)
ESP_STACK = 1.6 + 3.5    # ESP board + tallest parts on it
JACK_CX = (18.7, 38.7)   # RJ50 jack centres (shielded CAX RJ58-10P10C, C53059462)
JACK_W = 16.0            # jack body incl. shield
JACK_H = 13.8            # jack height above the PCB (allowance; the window is open to the top)
JACK_PROUD = 0.5         # the jack fronts stick out past the PCB edge by about this much

# ---- enclosure ----
WALL = 2.5
BASE = 2.5
LID = 2.5
RISER_H = 5.0            # room for through-hole leads under the PCB
GAP = 0.5                # clearance PCB edge -> wall (sides and back)
FRONT_GAP = 0.8          # front: room for the jacks sticking out
PCB_BOTTOM = BASE + RISER_H
PCB_TOP = PCB_BOTTOM + PCB_T
TOP_CLEAR = 1.8
INNER_TOP = PCB_TOP + HEADER_H + ESP_STACK + TOP_CLEAR      # underside of the lid

IN_X = (ESP_LEFT - 2.5, PCB_W + 7.5)   # room for the antenna on the left, lid posts on the right
IN_Y = (-FRONT_GAP, PCB_D + GAP)
OUT_X = (IN_X[0] - WALL, IN_X[1] + WALL)
OUT_Y = (IN_Y[0] - WALL, IN_Y[1] + WALL)

# PCB risers: a post the board sits on and is screwed to
RISER_R = 3.0
PILOT_D = 3.0            # blind hole an M3 screw threads into. If your printer makes holes undersize, try 3.1
PILOT_FLOOR = 0.6        # solid plastic left under the hole, on the desk side

# lid screws: posts in the enclosure corners with captive M3 nuts
POST_R = 4.5
POSTS = [(IN_X[0] + 2.5, 3.5), (IN_X[0] + 2.5, PCB_D - 2.5),
         (IN_X[1] - 2.5, 3.5), (IN_X[1] - 2.5, PCB_D - 2.5)]
NUT_AF = 5.8             # M3 nut 5.5 across flats + clearance
NUT_T = 2.8              # M3 nut 2.4 thick + clearance
NUT_DEPTH = 5.0          # nut slot this far below the top of the post
M3_CLEAR = 3.4

FLANGE_LEN = 14.0
FLANGE_W = 18.0
FLANGE_T = 3.5
WOOD_SCREW = 4.5         # shank clearance for a ~4 mm wood screw
WOOD_HEAD = 8.5          # countersink diameter


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(x, y, z0, z1, r, seg=SEG):
    return Manifold.cylinder(z1 - z0, r, r, seg).translate([x, y, z0])


def cone(x, y, z0, z1, r0, r1, seg=SEG):
    return Manifold.cylinder(z1 - z0, r0, r1, seg).translate([x, y, z0])


def hex_prism(x, y, z0, z1, af):
    r = af / math.sqrt(3)  # circumradius from across-flats
    return Manifold.cylinder(z1 - z0, r, r, 6).translate([x, y, z0])


def make_box():
    shell = box(OUT_X[0], OUT_X[1], OUT_Y[0], OUT_Y[1], 0, INNER_TOP)
    cavity = box(IN_X[0], IN_X[1], IN_Y[0], IN_Y[1], BASE, INNER_TOP + 1)
    body = shell - cavity

    # mounting flanges on the left and right of the base
    yc = (OUT_Y[0] + OUT_Y[1]) / 2
    for side in (-1, 1):
        x0, x1 = (OUT_X[0] - FLANGE_LEN, OUT_X[0]) if side < 0 else (OUT_X[1], OUT_X[1] + FLANGE_LEN)
        flange = box(x0, x1, yc - FLANGE_W / 2, yc + FLANGE_W / 2, 0, FLANGE_T)
        hx = (x0 + x1) / 2
        flange -= cyl(hx, yc, -1, FLANGE_T + 1, WOOD_SCREW / 2)
        # countersink on the side facing away from the desk (top of the flange)
        flange -= cone(hx, yc, FLANGE_T - (WOOD_HEAD - WOOD_SCREW) / 2, FLANGE_T + 0.01,
                       WOOD_SCREW / 2, WOOD_HEAD / 2)
        body += flange

    # PCB risers with blind pilot holes for the M3 board screws (small chamfer at the top to start the screw)
    for x, y in PCB_HOLES:
        body += cyl(x, y, BASE - 0.01, PCB_BOTTOM, RISER_R)
        body -= cyl(x, y, PILOT_FLOOR, PCB_BOTTOM + 1, PILOT_D / 2)
        body -= cone(x, y, PCB_BOTTOM - 0.5, PCB_BOTTOM + 0.01, PILOT_D / 2, PILOT_D / 2 + 0.5)

    # lid posts with captive-nut slots (nut slides in from the inside)
    for x, y in POSTS:
        body += cyl(x, y, BASE - 0.01, INNER_TOP, POST_R)
        body -= cyl(x, y, INNER_TOP - NUT_DEPTH - NUT_T - 4, INNER_TOP + 1, M3_CLEAR / 2)
        nz0 = INNER_TOP - NUT_DEPTH - NUT_T
        body -= hex_prism(x, y, nz0, nz0 + NUT_T, NUT_AF)
        inward = 1 if x < PCB_W / 2 else -1
        sx0, sx1 = (x, x + POST_R + 2) if inward > 0 else (x - POST_R - 2, x)
        body -= box(sx0, sx1, y - NUT_AF / 2, y + NUT_AF / 2, nz0, nz0 + NUT_T)

    # front notches for the two jacks, open to the top (the lid closes over them)
    m = 0.6
    for cx in JACK_CX:
        body -= box(cx - JACK_W / 2 - m, cx + JACK_W / 2 + m, OUT_Y[0] - 1, IN_Y[0] + 1,
                    PCB_TOP - 0.5, INNER_TOP + 1)
    return body


def save(m, name):
    mesh = m.to_mesh()
    tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
    assert tm.is_watertight, name + " is not watertight"
    tm.export(OUT / name)
    ext = tm.bounds[1] - tm.bounds[0]
    print(f"{name}: {ext[0]:.1f} x {ext[1]:.1f} x {ext[2]:.1f} mm, volume {tm.volume / 1000:.1f} cm3")
    return tm


if __name__ == "__main__":
    save(make_box(), "desk_controller_box.stl")
    print(f"inside: {IN_X[1]-IN_X[0]:.1f} x {IN_Y[1]-IN_Y[0]:.1f} x {INNER_TOP-BASE:.1f} mm; "
          f"PCB top at z={PCB_TOP:.1f}, top of the walls at z={INNER_TOP:.1f}")
