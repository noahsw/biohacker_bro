"""Renders of the round totem v2. Usage: python3 render_round.py PARTS_NPZ OUT_DIR"""
import sys, os, numpy as np, manifold3d as m
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
from render3d import Camera, render, autocrop, Annot
P = np.load(sys.argv[1]); OUT = sys.argv[2]
(RM, R_BACK, R_GI, R_GO, R_SI, R_SO, R_CO, R_CI, H, BORDER, PW, EDGE, LX, BASE_T, NUT_Z, SHOULDER, COLLAR_TOP,
 BAT_X, BAT_Y0, BAT_Y1, BAT_Z0, BAT_Z1, BOARD_Y, R_RO) = P["params"]
BLACK, WHITE, PANEL, PETG = (58, 58, 63), (236, 236, 230), (30, 30, 34), (40, 40, 46)
PERIM = 4 * LX + 2 * np.pi * RM

def man(k): return m.Manifold(m.Mesh(vert_properties=P[k + "_v"].astype(np.float32), tri_verts=P[k + "_t"].astype(np.uint32)))
def part(mf, rgb, off=(0, 0, 0), shine=0.0):
    mm = mf.to_mesh(); V = np.asarray(mm.vert_properties)[:, :3].astype(float); T = np.asarray(mm.tri_verts)
    return dict(V=V + off, T=T, rgb=rgb, closed=True, shine=shine)
def frame(s):
    s = s % PERIM
    if s <= LX: return s, 0.0, 90.0
    s -= LX
    if s <= np.pi * RM: return LX, 0.0, 90.0 - np.degrees(s / RM)
    s -= np.pi * RM
    if s <= 2 * LX: return LX - s, 0.0, -90.0
    s -= 2 * LX
    if s <= np.pi * RM: return -LX, 0.0, -90.0 - np.degrees(s / RM)
    s -= np.pi * RM
    return -LX + s, 0.0, 90.0
def place(s, local):
    cx, cy, phi = frame(s); return local.rotate([0, 0, phi]).translate([cx, cy, 0])
def flip(mf): return mf.rotate([0, 0, 180])
def stadium2d(r):
    c = m.CrossSection.circle(r, 360); return m.CrossSection.batch_hull([c.translate([LX, 0]), c.translate([-LX, 0])])
def sring(r0, r1, h, z0=0.0): return m.Manifold.extrude(stadium2d(r1) - stadium2d(r0), h).translate([0, 0, z0])
def box(x0, x1, y0, y1, z0, z1): return m.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def hue(t):
    t = t % 1.0; k = np.array([0, 1 / 3, 2 / 3])
    c = np.clip(np.abs(((t + k) % 1.0) * 6 - 3) - 1, 0, 1)
    return tuple(int(40 + 215 * v) for v in c)
sheet = sring(R_BACK, R_GI, PW, BORDER)
leds = {}
for p in range(6):
    for i in range(16):
        s = 160 * p + EDGE + 10 * i
        col = hue(s / PERIM)
        for j in range(16):
            z = BORDER + EDGE + 10 * j
            b = place(s, box(R_GI, R_GI + 2.5, -3.2, 3.2, z - 3.2, z + 3.2))
            leds.setdefault(col, m.Manifold()); leds[col] = leds[col] + b
def led_parts(off=(0, 0, 0), keep=None):
    out = []
    for col, mf in leds.items():
        x = mf if keep is None else mf ^ keep
        if not x.is_empty(): out.append(part(x, col, off, shine=0.3))
    return out

parts = {k: man(k) for k in ("spine", "base_half", "core_half", "cap_half", "grid_sector", "skin_third", "bushing_half", "thumb_nut")}
grids = [parts["grid_sector"].rotate([0, 0, -60 * p]) for p in range(6)]
base = parts["base_half"] + flip(parts["base_half"])
core = parts["core_half"] + flip(parts["core_half"])
cap = parts["cap_half"] + flip(parts["cap_half"])
skin = parts["skin_third"] + parts["skin_third"].rotate([0, 0, -120]) + parts["skin_third"].rotate([0, 0, -240])
bush = parts["bushing_half"] + flip(parts["bushing_half"])
nut = parts["thumb_nut"].rotate([0, 0, 195]).translate([0, 0, NUT_Z])
pole = m.Manifold.cylinder(COLLAR_TOP + 24.4 - SHOULDER + 17, 12.5, 12.5, 48).translate([0, 0, SHOULDER - 17]) + \
       m.Manifold.cylinder(500, 9.6, 9.6, 48).translate([0, 0, SHOULDER - 17 - 500])
battery = box(-BAT_X + 0.5, BAT_X - 0.5, BAT_Y0 + 0.5, BAT_Y1 - 0.5, BAT_Z0 + 0.5, BAT_Z1 - 0.5)
board = box(-34, 34, BOARD_Y - 30, BOARD_Y + 30, 12, 13.6)
clamp = m.Manifold.cylinder(16, 19.5, 19.5, 48).translate([0, 0, -34]) - m.Manifold.cylinder(18, 16.0, 16.0, 48).translate([0, 0, -35])
ALU, BAT, BRD = (160, 160, 168), (60, 100, 165), (30, 120, 80)

# ---------- 1. assembled ----------
sc = [part(skin, WHITE), part(cap, BLACK), part(base, BLACK), part(nut, PETG, shine=0.2), part(pole, ALU, shine=0.4),
      part(bush, PETG), part(clamp, (25, 25, 25), shine=0.3)]
cam = Camera(-35, 3, 1900, target=(0, 0, 40), W=1800, H=1500)
img = render(sc, cam); a = Annot(img, cam)
a.label((0, 0, NUT_Z + 30), (1350, 160), "Thumb nut", "hand-tight, no tools")
a.label((-60, -R_SO, 120), (120, 300), "White skin", "three pieces, seams on panel seams", anchor="r")
a.label((0, -19.5, -26), (1350, 1250), "Bike clamp", "same ROCKBROS as v1")
img = autocrop(img); img.save(os.path.join(OUT, "v2-assembled.png"))

# ---------- 2. lit, skin off ----------
sc = [part(core, BLACK), part(cap, BLACK), part(base, BLACK), part(nut, PETG, shine=0.2), part(pole, ALU, shine=0.4), part(bush, PETG)]
for g in grids: sc.append(part(g, BLACK))
sc.append(part(sheet, PANEL)); sc += led_parts()
cam = Camera(-62, 12, 1900, target=(0, 0, 40), W=1800, H=1500)
img = render(sc, cam); a = Annot(img, cam)
a.label((R_GO * np.cos(np.radians(-40)), R_GO * np.sin(np.radians(-40)), 150), (1450, 250), "Six panels", "each bends only 60 deg")
img = autocrop(img); img.save(os.path.join(OUT, "v2-lit-no-skin.png"))

# ---------- 3. inside: cap lifted, near core half and grids cut away ----------
keep = box(-400, 400, -400, 20, -100, 400)
far = box(-400, 400, 0, 400, -100, 400)
sc = [part(base, BLACK), part(parts["spine"], PETG), part(battery, BAT), part(board, BRD), part(pole, ALU, shine=0.4), part(bush, PETG),
      part(core ^ far, BLACK),
      part(cap.translate([0, 0, 170]), BLACK), part(nut.translate([0, 0, 250]), PETG, shine=0.2)]
cam = Camera(-90, 30, 1900, target=(0, 0, 100), W=1800, H=1700)
img = render(sc, cam); a = Annot(img, cam)
a.label((0, -18, 150), (120, 330), "Spine (PETG)", "tube + battery tray in one", anchor="r")
a.label((-60, BAT_Y0, BAT_Z1 - 10), (120, 760), "Anker 537", "on its edge beside the pole", anchor="r")
a.label((20, BOARD_Y, 14), (1500, 1000), "Board", "on 4 posts, opposite the battery")
a.label((140, 0, H + 172), (1500, 260), "Cap", "lifts straight off")
a.label((0, 0, NUT_Z + 280), (1000, 80), "Thumb nut", "unscrew to open")
img = autocrop(img); img.save(os.path.join(OUT, "v2-inside.png"))

# ---------- 4. pole joint section (x-z plane) ----------
half = box(-400, 400, 0, 400, -400, 400)
sc = [part(parts["spine"] ^ half ^ box(-60, 60, -400, 400, -60, 400), (120, 120, 132)), part((cap ^ half) ^ box(-60, 60, -400, 400, 0, 400), (70, 70, 76)),
      part(nut ^ half, (80, 80, 95)), part(bush ^ half, (150, 150, 165)), part(base ^ half ^ box(-60, 60, -400, 400, -60, 400), BLACK),
      part(clamp ^ half, (25, 25, 25))]
tip = (m.Manifold.cylinder(24.4, 9.5, 9.5, 48).translate([0, 0, COLLAR_TOP]) + m.Manifold.cylinder(25, 12.55, 12.55, 48).translate([0, 0, SHOULDER]) +
       m.Manifold.cylinder(12.6, 14.25, 14.25, 48).translate([0, 0, SHOULDER - 12.6]) + m.Manifold.cylinder(400, 9.6, 9.6, 48).translate([0, 0, SHOULDER - 412.6]))
sc.append(part(tip ^ box(-400, 400, 0, 400, -60, 400), (200, 175, 120), shine=0.3))
cam = Camera(-90, 0, 1500, target=(0, 0, 85), W=2100, H=2100, ortho=True, scale=2.6)
img = render(sc, cam); a = Annot(img, cam)
a.label((0, 0, NUT_Z + 20), (1570, 120), "Thumb nut", "on the pole's mop thread")
a.label((12.5, 0, COLLAR_TOP - 10), (1570, 420), "Silver collar", "snug in the tube top")
a.label((14.3, 0, SHOULDER), (1570, 620), "Head rests here", "on the wider section")
a.label((-15.5, 0, 20), (300, 1350), "Bushing halves", "grip the thin tube", anchor="r")
a.label((19.5, 0, -26), (1570, 1500), "Bike clamp", "squeezes the bushing")
img = autocrop(img); img.save(os.path.join(OUT, "v2-pole-joint-section.png"))
print("ok")
