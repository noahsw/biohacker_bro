"""Totem v2 (round): six 16x16 panels round a drum, pole up a center tube, thumb nut on top.

Noah switched from the oval to a round drum on Nov 22: no tight bends, readable from every side.
Assembled coordinates (mm): z=0 is the top of the base plate, the pole axis is x=y=0. Panel p covers
s = 160p .. 160p+160 along the band, running clockwise from +y (seam at +y). Halves split at x=0
(both on a panel seam) and are the same part turned 180 deg (print 2 of each).

Parts
  spine        PETG. Center tube (pole runs up it) + floor plate + battery tray, one piece.
  bushing_half PETG. Bottom halves round the thin pole tube; the ROCKBROS clamp squeezes their skirt (print 2)
  thumb_nut    PETG. Screws onto the pole's 3/4"-5 Acme tip and clamps cap + head to the pole
  base_half    bottom plate half: open lattice, skin rim, grid pegs, board posts, screw holes (print 2)
  core_half    open-frame wall the panels wrap round, magnet bosses, joint flanges at x=0 (print 2)
  cap_half     lattice lid half: rim over grid and skin, hub for the nut, battery fins (print 2)
  grid_sector  pixel grid for one panel, 60 deg of the drum (print 6)
  skin_third   white diffuser, 120 deg (print 3)
  ft_top, ft_bottom  fit tests for the pole top + nut and the bushing + clamp
Usage: python3 build_round.py OUT_NPZ
"""
import sys, numpy as np, manifold3d as m

OUT = sys.argv[1]
SEG = 96
# ---------- panel stack (same as v1) ----------
PITCH, EDGE, FACE_T = 10.0, 5.0, 0.47
PW = 2 * EDGE + 15 * PITCH                      # 160
LX = 0.0                                        # round: the 'stadium' helpers become circles
RM = 6 * PW / (2 * np.pi)                       # 152.79: panel mid-thickness
R_BACK, R_GI = RM - FACE_T / 2, RM + FACE_T / 2
FIN_D = 5.6
R_GO = R_GI + FIN_D
R_SI = R_GO + 0.25
SKIN_T = 0.6
R_SO = R_SI + SKIN_T
R_RI = R_SI + 0.84 + 0.3
R_RO = R_RI + 1.6
R_CO = R_BACK - 0.8
R_CI = R_CO - 1.6
BORDER = 7.0
H = PW + 2 * BORDER                             # 174: core height, cap sits on it
HT = H - 0.4
FIN, HALF_FIN, SEAM_GAP = 0.8, 0.6, 0.1
SEAM_FIN = 2 * HALF_FIN + SEAM_GAP
ROW = 0.8
CAP_NOTCH_W, CAP_NOTCH_D = 4.0, 1.3
BASE_T, CAP_T = 2.2, 2.0
PERIM = 2 * 2 * LX + 2 * np.pi * RM             # 960 = 6 panels

# ---------- pole tip (his measurements, Nov 19) ----------
COLLAR_D, COLLAR_L, SEC_D, THREAD_L = 25.1, 25.0, 28.5, 24.4
NUT_Z = H + CAP_T                               # nut bears on the cap pad here
COLLAR_TOP = H - 1.0                            # collar top 1 mm under the tube top, so the nut clamps plastic, not the pole
SHOULDER = COLLAR_TOP - COLLAR_L                # top face of the 28.5 section: the head rests on it
R_TOP = COLLAR_D / 2 + 0.3                      # bore round the collar
R_MIDB = SEC_D / 2 + 0.35                       # bore round the 28.5 section
R_LOW = 15.7                                    # 31.4 bore below: room for the lip under the 28.5 section
R_TUBE = 18.1
POLE_THIN = 19.2

def cyl(r, h, z0=0.0, seg=SEG): return m.Manifold.cylinder(h, r, r, seg).translate([0, 0, z0])
def tube(ri, ro, h, z0=0.0, seg=SEG): return cyl(ro, h, z0, seg) - cyl(ri, h + 2, z0 - 1, seg)
def box(x0, x1, y0, y1, z0, z1): return m.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])
def revolve(pts, seg=SEG): return m.Manifold.revolve(m.CrossSection([pts]), seg)
def rz(man, a): return man.rotate([0, 0, a])
def hexpocket(z_top, depth, af=5.75):
    return m.Manifold.cylinder(depth + 1, af / np.sqrt(3), af / np.sqrt(3), 6).translate([0, 0, z_top - depth])
def csink(z_bot, length):                       # M3 flat head from below
    return m.Manifold.cylinder(1.9, 3.3, 1.65, 32).translate([0, 0, z_bot - 0.2]) + cyl(1.7, length, z_bot - 1, 24)

def stadium2d(r, n=480):
    if r <= 0: return m.CrossSection()
    c = m.CrossSection.circle(r, n)
    return m.CrossSection.batch_hull([c.translate([LX, 0]), c.translate([-LX, 0])])
def stad(r, h, z0=0.0): return m.Manifold.extrude(stadium2d(r), h).translate([0, 0, z0])
def sring(r0, r1, h, z0=0.0): return m.Manifold.extrude(stadium2d(r1) - stadium2d(r0), h).translate([0, 0, z0])

def frame(s):
    """Arc length s along the panel band (0 at x=0 on +y, running toward +x) -> (cx, cy, phi deg).
    A point at 'radius' r is (cx, cy) + r*(cos phi, sin phi)."""
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
    """local: x = radius, y = against the running direction, z = z."""
    cx, cy, phi = frame(s)
    return local.rotate([0, 0, phi]).translate([cx, cy, 0])
def point(s, r, z=0.0):
    cx, cy, phi = frame(s); a = np.radians(phi)
    return np.array([cx + r * np.cos(a), cy + r * np.sin(a), z])
def window(w, z0, z1, r0, r1):
    hw = w / 2; pts = []
    for x in (r0, r1):
        pts += [(x, -hw, z0 + hw), (x, 0, z0), (x, hw, z0 + hw), (x, hw, z1 - hw), (x, 0, z1), (x, -hw, z1 - hw)]
    return m.Manifold.hull_points(pts)
def half(man, side=+1, gap=0.05):
    return man ^ (box(gap, 400, -200, 200, -100, 400) if side > 0 else box(-400, -gap, -200, 200, -100, 400))
def flip180(man): return man.rotate([0, 0, 180])

parts = {}

# =============== grid: build the whole band, then cut it into panels ===============
band = m.Manifold()
for p in range(6):
    for k in range(17):
        sf = 160 * p + (0 if k == 0 else (PW if k == 16 else EDGE + PITCH * (k - 1) + PITCH / 2))
        if k == 16: continue                    # next panel's k=0 is the same seam
        t = SEAM_FIN if k == 0 else FIN
        band += place(sf, box(R_GI - 0.03, R_GO + 0.03, -t / 2, t / 2, 0, HT))
rows = m.Manifold()
for k in range(15):
    zr = BORDER + EDGE + PITCH * k + PITCH / 2
    rows += sring(R_GI, R_GO, ROW, zr - ROW / 2)
rows += sring(R_GI, R_GO, BORDER + ROW / 2, 0)
rows += sring(R_BACK + 0.2, R_GO, BORDER, 0)                       # lip under the panel's bottom edge
rows += sring(R_GI, R_GO, HT - (H - BORDER - ROW / 2), H - BORDER - ROW / 2)
band += rows
notch = m.Manifold()
nz = [BORDER + EDGE + PITCH * k + PITCH / 2 for k in range(15)]
nb = box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, 0, 1)
for p in range(6):
    for j in range(16):
        s = 160 * p + EDGE + PITCH * j
        loc = m.Manifold()
        for zc in nz:
            loc += box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, zc - ROW / 2 - 0.2, zc + ROW / 2 + 0.2)
        loc += box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, BORDER - 0.2, BORDER + 1.4)
        loc += box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, BORDER + PW - 1.4, BORDER + PW + 0.2)
        notch += place(s, loc)
band -= notch
PEG_S = (40.0, 120.0)                                              # panel-relative peg positions
R_PEG = (R_GI + R_GO) / 2
def peg_holes(p):
    h = m.Manifold()
    for ps in PEG_S:
        h += cyl(1.75, 4.2, -1, 24).translate(point(160 * p + ps, R_PEG))
    return h
g = SEAM_GAP / 2
def wedge(phi1, phi2, gap=0.05, z0=-50, z1=400):
    """Region between the rays at phi1 and phi2 (deg, phi2 - phi1 < 180), each side pulled in by gap."""
    a = box(-500, 500, gap, 500, z0, z1).rotate([0, 0, phi1])
    b = box(-500, 500, -500, -gap, z0, z1).rotate([0, 0, phi2])
    return a ^ b
def rot(man, deg): return man.rotate([0, 0, deg])

# =============== grid sectors and skin ===============
grid_sector = (band ^ wedge(30, 90, g)) - peg_holes(0)               # panel 0: phi 90 -> 30
parts["grid_sector"] = grid_sector
skin = sring(R_SI, R_SO, HT)
parts["skin_third"] = skin ^ wedge(-30, 90, 0.05)                    # seams at panel seams (phi 90, -30, 210)
parts["skin_third_084"] = sring(R_SI, R_SI + 0.84, HT) ^ wedge(-30, 90, 0.05)

# =============== spine (PETG): tube + floor plate + battery tray ===============
A_L, A_H, A_T = 161.0, 85.9, 27.4                 # Anker 537 + 1 mm foam/felt
PLATE_T = 5.0
BAT_Y0 = R_TUBE + 0.3
BAT_Y1 = BAT_Y0 + A_T
BAT_X = A_L / 2
BAT_Z0 = PLATE_T
BAT_Z1 = BAT_Z0 + A_H
TRAY_W = 2.2
LIP_Y1 = R_TUBE + 0.3 + 27.4 + 2.2
PLATE_X, PLATE_YN = BAT_X + TRAY_W, -46.3
KEY_W, KEY_R = 5.0, 17.6                          # anti-twist keys on the bushing: slots in the tube's bottom
BUSH_TOP = 42.0

prof = [(R_LOW, 0.0), (R_TUBE, 0.0), (R_TUBE, H), (R_TOP, H), (R_TOP, SHOULDER), (R_MIDB, SHOULDER),
        (R_MIDB, SHOULDER - 11.0), (R_LOW, SHOULDER - 11.0 - (R_LOW - R_MIDB))]
spine = revolve(prof, 128)
plate = m.Manifold.extrude(m.CrossSection.square([2 * PLATE_X, LIP_Y1 - PLATE_YN]).translate([-PLATE_X, PLATE_YN]).offset(0), PLATE_T)
plate = plate ^ stad(R_CI - 0.7, PLATE_T)                          # round the corners into the core
spine += plate
for sx in (1, -1):                                                 # battery tray
    spine += box(*sorted((sx * BAT_X, sx * (BAT_X + TRAY_W))), BAT_Y0 - 2, LIP_Y1, PLATE_T, PLATE_T + (10.0 if sx > 0 else 40.0))
spine += box(-PLATE_X, PLATE_X, BAT_Y1, LIP_Y1, PLATE_T, PLATE_T + 30)   # outer lip behind the pack
for sx in (1, -1):                                                 # key bosses + gussets along +-x
    spine += box(*sorted((sx * 14, sx * (KEY_R + 3.2))), -6, 6, 0, BUSH_TOP + 2)
    tri = m.CrossSection([[(16.0, 0.0), (55.0, 0.0), (16.0, 60.0)]])
    gus = m.Manifold.extrude(tri, 4.0).translate([0, 0, -2.0]).rotate([90, 0, 0])
    spine += gus.translate([0, 0, PLATE_T]) if sx > 0 else rz(gus, 180).translate([0, 0, PLATE_T])
web = box(-1.6, 1.6, PLATE_YN, -16.0, PLATE_T, 48) + box(-15, 15, PLATE_YN, PLATE_YN + 2.0, PLATE_T, 48)
spine += web                                                       # backs the -y wall where it has no tabs
spine -= revolve([(0, -1), (R_LOW, -1), (R_LOW, SHOULDER - 11.0 - (R_LOW - R_MIDB)), (R_MIDB, SHOULDER - 11.0),
                  (R_MIDB, SHOULDER), (R_TOP, SHOULDER), (R_TOP, H + 1), (0, H + 1)], 128)
for sx in (1, -1):
    spine -= box(*sorted((sx * 12, sx * (KEY_R + 0.3))), -KEY_W / 2 - 0.25, KEY_W / 2 + 0.25, -1, BUSH_TOP + 0.4)
spine -= m.Manifold.cylinder(1.2, R_LOW + 1.0, R_LOW, 96).translate([0, 0, -0.01])          # lead-in
PLATE_SCREWS = [(sx * x, y) for sx in (1, -1) for x in (30.0, 70.0) for y in (34.0, -34.0)]
for (x, y) in PLATE_SCREWS:
    spine -= hexpocket(PLATE_T, 2.6).rotate([0, 0, 30]).translate([x, y, 0])
    spine -= cyl(1.7, 10, -1, 24).translate([x, y, 0])
parts["spine"] = spine

# =============== bushing halves (PETG) ===============
# Lesson from v1 (Nov 22): a 31.8 mm sleeve needed pliers to get the 31.8 mm ROCKBROS clamp on, and the slit sleeve
# cracked when squeezed. So: skirt 31.4 mm with a lead-in, so the open clamp slides on by hand; two separate solid
# halves (no slits, no thin fingers, no crush ribs) with a 1 mm gap between them, so the clamp closes them onto the
# pole long before they meet: the squeeze goes into the pole, not into crushing plastic.
BI = POLE_THIN / 2 + 0.1                         # 19.4 bore on the 19.2 tube
SKIRT_R = 31.4 / 2
SPLIT = 0.5                                      # each half stops 0.5 short of the centre plane: 1.0 mm gap
bprof = [(BI, -38.0), (SKIRT_R - 1.2, -38.0), (SKIRT_R, -36.8), (SKIRT_R, -BASE_T - 3.0), (23.0, -BASE_T - 3.0), (23.0, -BASE_T),
         (R_LOW - 0.2, -BASE_T), (R_LOW - 0.2, BUSH_TOP - 1.0), (R_LOW - 1.2, BUSH_TOP), (BI, BUSH_TOP)]
bush = revolve(bprof, 128)
for sx in (1, -1):
    bush += box(*sorted((sx * (R_LOW - 1.0), sx * KEY_R)), -KEY_W / 2, KEY_W / 2, -BASE_T, BUSH_TOP)
bush -= m.Manifold.cylinder(1.0, BI + 0.8, BI, 96).translate([0, 0, -38.01])     # lead-in for the pole
bush_half = bush ^ box(SPLIT, 50, -50, 50, -50, 60)
parts["bushing_half"] = bush_half

# =============== thumb nut (PETG) ===============
ACME_P = 25.4 / 5
ACME_R = 25.4 * 0.75 / 2
def acme_male(clear, h, crest_extra=0.4, n=360):
    depth = ACME_P / 2; crest = 0.3707 * ACME_P; flank = depth * np.tan(np.radians(14.5))
    rmaj, rmin = ACME_R + clear, ACME_R - depth + clear
    cw = crest + crest_extra; rw = ACME_P - cw - 2 * flank
    def r_of(u):
        u = u % ACME_P
        if u < cw: return rmaj
        u -= cw
        if u < flank: return rmaj - (rmaj - rmin) * u / flank
        u -= flank
        if u < rw: return rmin
        u -= rw
        return rmin + (rmaj - rmin) * u / flank
    th = np.linspace(0, 2 * np.pi, n, endpoint=False)
    pts = [(r_of(t / (2 * np.pi) * ACME_P) * np.cos(t), r_of(t / (2 * np.pi) * ACME_P) * np.sin(t)) for t in th]
    turns = h / ACME_P
    return m.Manifold.extrude(m.CrossSection([pts]), h, int(turns * 90) + 1, 360.0 * turns)
NUT_H, NUT_DEPTH = 34.0, 26.0
def thumb_nut():
    core = m.Manifold.cylinder(NUT_H - 8, 24, 24, 128) + m.Manifold.cylinder(8, 24, 14, 128).translate([0, 0, NUT_H - 8])
    lobes = m.Manifold()
    for k in range(8):
        lobes += m.Manifold.cylinder(NUT_H - 8, 7.5, 7.5, 48).translate([23, 0, 0]).rotate([0, 0, 45 * k])
        lobes += m.Manifold.cylinder(6, 7.5, 2.0, 48).translate([23, 0, NUT_H - 8]).rotate([0, 0, 45 * k])
    n = core + lobes
    n -= acme_male(0.45, NUT_DEPTH + ACME_P).translate([0, 0, -ACME_P])
    n -= m.Manifold.cylinder(4.0, ACME_R + 0.5, 3.0, 64).translate([0, 0, NUT_DEPTH - 0.01])     # pointed roof: no support
    n -= m.Manifold.cylinder(1.5, ACME_R + 1.6, ACME_R + 0.4, 96).translate([0, 0, -0.01])     # lead-in
    n -= m.Manifold.cylinder(NUT_H + 2, 1.8, 1.8, 24).translate([26.0, 0, -1])          # lanyard hole through one lobe
    return n
nut = thumb_nut()
parts["thumb_nut"] = nut
# =============== lattice helper: hex holes, kept solid where a keep-zone says so ===============
def hexholes(rmax, pitch=34.0, rc=15.6, z0=-10.0, h=30.0):
    holes = m.Manifold()
    n = int(rmax / pitch) + 2
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            x = pitch * (i + 0.5 * j); y = pitch * np.sqrt(3) / 2 * j
            if np.hypot(x, y) - rc < rmax:
                holes += m.Manifold.cylinder(h, rc, rc, 6).rotate([0, 0, 30]).translate([x, y, z0])
    return holes ^ cyl(rmax, h + 2, z0 - 1, 256)                    # partial hexes at the edge too
def spokes(angles, w, r0, r1, z0, z1):
    s = m.Manifold()
    for a in angles:
        s += box(r0, r1, -w / 2, w / 2, z0, z1).rotate([0, 0, a])
    return s
SPOKE_A = (0, 60, 120, 180, 240, 300)
PLATE_SCREWS = [(sx * x, y) for sx in (1, -1) for x in (30.0, 70.0) for y in (34.0, -34.0)]
def dots(pts, r, z0=-20, h=60):
    d = m.Manifold()
    for (x, y) in pts: d += cyl(r, h, z0, 32).translate([x, y, 0])
    return d
def c2(pts): return list(pts) + [(-x, -y) for (x, y) in pts]
# lighten the spine's floor plate (the battery still sits on a 5 mm PETG lattice)
skeep = cyl(R_TUBE + 7, 40, -5) + dots(PLATE_SCREWS, 7) + box(-90, 90, -7, 7, -5, 20) + box(-5, 5, -60, 0, -5, 20)
spine -= (hexholes(120, pitch=22.0, rc=9.0, z0=-1, h=PLATE_T + 2) ^ box(-PLATE_X + 5, PLATE_X - 5, PLATE_YN + 5, LIP_Y1 - 5, -2, PLATE_T + 2)) - skeep
parts["spine"] = spine
BOARD_C = (0.0, -95.0); HOLE_X, HOLE_Y = 60.0, 52.0
BOARD_POSTS = [(BOARD_C[0] + sx * HOLE_X / 2, BOARD_C[1] + sy * HOLE_Y / 2) for sx in (1, -1) for sy in (1, -1)]

# =============== base half ===============
base = stad(R_RO, BASE_T, -BASE_T)
CORE_TAB_S = [80.0 + 160 * p for p in range(6)]                    # panel centres
TAB_PTS = [tuple(point(p_s, R_CI - 6.0)[:2]) for p_s in CORE_TAB_S]
keep = (sring(R_CI - 6, R_RO + 1, 60, -20) + cyl(R_TUBE + 8, 60, -20) + dots(PLATE_SCREWS, 7.5) + dots(TAB_PTS, 8)
        + dots(c2(BOARD_POSTS), 7.5)
        + sum((box(sx * 30 - 6, sx * 30 + 6, sy * 63, sy * 127, -20, 40) if sy > 0 else box(sx * 30 - 6, sx * 30 + 6, -127, -63, -20, 40) for sx in (1, -1) for sy in (1, -1)), m.Manifold())
        + spokes(SPOKE_A, 10, 0, R_CI, -20, 40) + box(-5, 5, -200, 200, -20, 40))
base -= (hexholes(R_CI - 6) - keep)
base += sring(R_RI, R_RO, BORDER)                                  # rim that holds the skin and grid feet
for p in range(6):
    for ps in PEG_S:
        base += m.Manifold.cylinder(2.6, 1.55, 1.3, 24).translate(point(160 * p + ps, R_PEG))
base += spokes(SPOKE_A, 3, 100, R_CI - 14, 0, 8)                     # stiffening ribs on top
base -= cyl(R_LOW + 0.3, 20, -BASE_T - 1)
for sx in (1, -1):
    base -= box(*sorted((sx * 12, sx * (KEY_R + 0.3))), -KEY_W / 2 - 0.25, KEY_W / 2 + 0.25, -BASE_T - 1, 2)
for (x, y) in PLATE_SCREWS:
    base -= csink(-BASE_T, 12).translate([x, y, 0])
for p_s in CORE_TAB_S:
    base -= csink(-BASE_T, 12).translate(point(p_s, R_CI - 6.0))
curb = sring(R_CI - 5.0, R_CI - 0.3, 2.5)
for p_s in CORE_TAB_S:
    curb -= place(p_s, box(R_CI - 14, R_CI + 1, -7, 7, -1, 10))
base += curb                                                       # locates the bottom of the core wall
for sgn in (1, -1):                                                # board posts (both sides so the halves stay identical)
    for (x, y) in BOARD_POSTS:
        base += cyl(3.2, 12.0, 0, 32).translate([sgn * x, sgn * y, 0])
for sgn in (1, -1):
    for (x, y) in BOARD_POSTS:
        base -= cyl(1.45, 30, -BASE_T - 1, 24).translate([sgn * x, sgn * y, 0])
        base -= cyl(2.7, 1.8, -BASE_T - 0.01, 32).translate([sgn * x, sgn * y, 0])
parts["base_half"] = half(base)

# =============== core half: open frame ===============
core = sring(R_CI, R_CO, H) + sring(R_CI, R_BACK, BORDER)
WIN_W = 33.0
for p in range(6):
    for c in (20.0, 60.0, 100.0, 140.0):                           # bars at every 40 mm, on the seams and centres
        for z0, z1 in ((14.0, 84.0), (92.0, 158.0)):
            core -= place(160 * p + c, window(WIN_W, z0, z1, R_CI - 1, R_BACK + 1))
for p_s in CORE_TAB_S:
    tab = place(p_s, box(R_CI - 12, R_CI + 0.5, -6, 6, 0, 8)) ^ stad(R_CI + 0.5, 20, -1)
    core += tab
    core -= hexpocket(8.0, 2.6).translate(point(p_s, R_CI - 6.0))
    core -= cyl(1.7, 20, -1, 24).translate(point(p_s, R_CI - 6.0))
MAG_D, MAG_T = 10.3, 3.2
MAG_R = R_CI - 6.5
MAG_A = (45.0, 135.0, 225.0, 315.0)
MAG_PTS = [(MAG_R * np.cos(np.radians(a)), MAG_R * np.sin(np.radians(a))) for a in MAG_A]
for (x, y) in MAG_PTS:                                             # magnet bosses, 45-deg cone underneath (no support)
    boss = cyl(8.0, 12.0, H - 12.0, 48) + m.Manifold.cylinder(8.0, 1.0, 8.0, 48).translate([0, 0, H - 20.0])
    core += boss.translate([x, y, 0]) ^ stad(R_CO, H + 1, -1)
    core -= cyl(MAG_D / 2, MAG_T + 1.2 + 1, H - MAG_T - 1.2, 48).translate([x, y, 0])
FL_T, FL_D, FL_Z0 = 4.0, 12.0, 24.0
FL_BOLTS_Z = (44.0, 100.0, 156.0)
def flange(sx, sy):
    x0, x1 = sorted((sx * 0.05, sx * FL_T))
    y0, y1 = sorted((sy * (R_CI + 0.5), sy * (R_CI - FL_D)))
    f = box(x0, x1, y0, y1, FL_Z0, H)
    f += m.Manifold.hull_points([(x, sy * yy, z) for x in (x0, x1) for (yy, z) in
                                 ((R_CI + 0.5, FL_Z0), (R_CI - FL_D, FL_Z0), (R_CI + 0.5, FL_Z0 - FL_D - 0.5))])
    f = f ^ stad(R_CI + 0.5, H + 1, -1)
    yb = sy * (R_CI - FL_D / 2)
    for zb in FL_BOLTS_Z:
        f -= cyl(1.7, 12, -6, 24).rotate([0, 90, 0]).translate([0, yb, zb])
        if sx * sy > 0:
            f -= (m.Manifold.cylinder(3.6, 5.75 / np.sqrt(3), 5.75 / np.sqrt(3), 6).rotate([0, 90, 0]).rotate([30, 0, 0])
                  .translate([FL_T - 2.6, yb, zb]).mirror([1, 0, 0]) if sx < 0 else
                  m.Manifold.cylinder(3.6, 5.75 / np.sqrt(3), 5.75 / np.sqrt(3), 6).rotate([0, 90, 0]).rotate([30, 0, 0]).translate([FL_T - 2.6, yb, zb]))
        else:
            cs = m.Manifold.cylinder(1.9, 1.65, 3.3, 32).rotate([0, 90, 0]).translate([FL_T - 1.85, yb, zb])
            f -= cs.mirror([1, 0, 0]) if sx < 0 else cs
    return f
parts["core_half"] = half(core) + flange(1, 1) + flange(1, -1)

# =============== cap half: lattice lid ===============
CAP_HOLE = ACME_R + 0.75
cap = stad(R_RO, CAP_T, H)
FIN_Y = (BAT_Y0 + BAT_Y1) / 2
CJ_Y = 100.0
POST_PTS = c2([(20.0, FIN_Y), (64.0, FIN_Y), (20.0, -FIN_Y), (64.0, -FIN_Y)])
ckeep = (sring(R_CI - 4, R_RO + 1, 60, H - 20) + cyl(38.0, 60, H - 20) + dots(MAG_PTS, 8.5, H - 20) + dots(POST_PTS, 7, H - 20)
         + dots([(0, CJ_Y), (0, -CJ_Y)], 11, H - 20)
         + spokes(SPOKE_A, 10, 0, R_CI, H - 20, H + 20) + box(-5, 5, -200, 200, H - 20, H + 20))
cap -= (hexholes(R_CI - 4, z0=H - 10) - ckeep)
cap += sring(R_RI, R_RO, BORDER, H - BORDER)                       # rim over the grid and skin
RIB_H = 8.0
cap += spokes(SPOKE_A, 3, 40, R_CI - 3, H - RIB_H, H)
cap += box(0.05, 3.0, -(R_CI - 14), R_CI - 14, H - RIB_H, H) - cyl(R_TUBE + 3, 20, H - 10)   # edge rib along the split
for sy in (1, -1):                                                 # joint blocks at x=0, bolted to the other half
    cap += box(0.05, FL_T, sy * CJ_Y - 7, sy * CJ_Y + 7, H - RIB_H, H)
LIP = sring(R_CI - 2.0, R_CI - 0.3, 6.0, H - 6.0) - box(-6, 6, -200, 200, 0, 400)
for (x, y) in MAG_PTS:
    LIP -= cyl(9.5, 20, H - 10).translate([x, y, 0])
cap += LIP
for (x, y) in POST_PTS:                                            # posts stop the battery lifting
    if x > 0: cap += cyl(3.5, H - BAT_Z1 - 3.0, BAT_Z1 + 3.0, 32).translate([x, y, 0])
cap -= cyl(CAP_HOLE, 20, H - 10)
for sy in (1, -1):
    yb, zb = sy * CJ_Y, H - RIB_H / 2
    cap -= cyl(1.7, 12, -6, 24).rotate([0, 90, 0]).translate([0, yb, zb])
    if sy > 0:
        cap -= m.Manifold.cylinder(3.6, 5.75 / np.sqrt(3), 5.75 / np.sqrt(3), 6).rotate([0, 90, 0]).rotate([30, 0, 0]).translate([FL_T - 2.6, yb, zb])
    else:
        cap -= m.Manifold.cylinder(1.9, 1.65, 3.3, 32).rotate([0, 90, 0]).translate([FL_T - 1.85, yb, zb])
for (x, y) in MAG_PTS:                                             # cap magnets stand 1.2 proud and drop into the core's pockets
    cap -= cyl(MAG_D / 2, MAG_T - 1.2, H - 0.01, 48).translate([x, y, 0])
parts["cap_half"] = half(cap)

# =============== fit tests ===============
FT_Z0 = SHOULDER - 38.0
ft = revolve([(R_LOW, FT_Z0), (R_TUBE, FT_Z0), (R_TUBE, H), (R_TUBE + 6, H), (R_TUBE + 6, NUT_Z), (CAP_HOLE, NUT_Z), (CAP_HOLE, H),
              (R_TOP, H), (R_TOP, SHOULDER), (R_MIDB, SHOULDER), (R_MIDB, SHOULDER - 11.0),
              (R_LOW, SHOULDER - 11.0 - (R_LOW - R_MIDB))], 128)
parts["ft_top"] = ft
ftb = (spine ^ cyl(R_TUBE + 4.0, BUSH_TOP + 6, -1)) + tube(R_TUBE - 0.5, R_TUBE + 4.0, 3.0, 0)
ftb = ftb ^ cyl(KEY_R + 4, BUSH_TOP + 4, -1)
ftb -= cyl(R_LOW, BUSH_TOP + 10, -2)
for sx in (1, -1):
    ftb -= box(*sorted((sx * 12, sx * (KEY_R + 0.3))), -KEY_W / 2 - 0.25, KEY_W / 2 + 0.25, -1, BUSH_TOP + 0.4)
parts["ft_bottom"] = ftb

# =============== checks ===============
A = {}
for k in ("base_half", "cap_half", "core_half"):
    A[k + "A"] = parts[k]; A[k + "B"] = rot(parts[k], 180)
for i in range(3): A[f"skin{i}"] = rot(parts["skin_third"], -120 * i)
for p in range(6): A[f"grid{p}"] = rot(grid_sector, -60 * p)
A["spine"] = spine
A["bushA"] = bush_half; A["bushB"] = rot(bush_half, 180)
A["nut"] = nut.rotate([0, 0, 195.0]).translate([0, 0, NUT_Z])
names = list(A)
if "--fast" not in sys.argv:
    print("overlaps (cm3, ~0 wanted):")
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            v = (A[names[i]] ^ A[names[j]]).volume() / 1000
            if v > 0.002: print(f"  {names[i]} x {names[j]}: {v:.3f}")
battery = box(-BAT_X + 0.5, BAT_X - 0.5, BAT_Y0 + 0.5, BAT_Y1 - 0.5, BAT_Z0 + 0.5, BAT_Z1 - 0.5)
plug = box(BAT_X, BAT_X + 16, BAT_Y0 + 4, BAT_Y1 - 4, BAT_Z0 + 12, BAT_Z0 + 70)
board = box(BOARD_C[0] - 35, BOARD_C[0] + 35, BOARD_C[1] - 31, BOARD_C[1] + 31, 12.0, 12.0 + 1.6 + 14)
lift = box(-BAT_X + 0.5, BAT_X - 0.5, BAT_Y0 + 0.5, BAT_Y1 - 0.5, BAT_Z1 - 0.5, H + 2)
pole = (acme_male(0.0, THREAD_L, 0.0).translate([0, 0, COLLAR_TOP]) + cyl(COLLAR_D / 2, COLLAR_L, SHOULDER)
        + cyl(SEC_D / 2, 12.6, SHOULDER - 12.6) + cyl(30.5 / 2, 4.0, SHOULDER - 16.6) + cyl(POLE_THIN / 2, 400, SHOULDER - 16.6 - 400))
for nm, env in {"battery": battery, "plug": plug, "board": board, "pole": pole}.items():
    hits = [(n, round((env ^ A[n]).volume() / 1000, 3)) for n in names]
    print(f"  {nm} hits: {[h for h in hits if h[1] > 0.002]}")
cap_off = {k: v for k, v in A.items() if not k.startswith("cap") and k != "nut"}
print("  battery lift-out path hits:", [(n, round((lift ^ v).volume() / 1000, 3)) for n, v in cap_off.items() if (lift ^ v).volume() > 2])
print(f"RM {RM:.2f}  outer diameter {2 * R_RO:.1f}  H {H}  core inner r {R_CI:.2f}  battery y {BAT_Y0:.1f}-{BAT_Y1:.1f}")

def mesh(man):
    mm = man.to_mesh()
    return np.asarray(mm.vert_properties)[:, :3].astype(np.float32), np.asarray(mm.tri_verts).astype(np.int32)
out = {}
for name, man in parts.items():
    v, t = mesh(man); out[name + "_v"], out[name + "_t"] = v, t
    bb = v.max(0) - v.min(0)
    print(f"{name:15s} vol={man.volume() / 1000:6.1f} cm3 size={np.round(bb, 1)} genus={man.genus()} status={man.status()}")
out["params"] = np.array([RM, R_BACK, R_GI, R_GO, R_SI, R_SO, R_CO, R_CI, H, BORDER, PW, EDGE, LX, BASE_T, NUT_Z, SHOULDER, COLLAR_TOP,
                          BAT_X, BAT_Y0, BAT_Y1, BAT_Z0, BAT_Z1, BOARD_C[1], R_RO])
np.savez_compressed(OUT, **out)
