"""Modular 4-panel heart rate totem: every printed part is its own file, so a misprint costs one part.

Parts (assembled coordinates, mm; z=0 is the top of the base plate, the axis is the pole):
  base      base plate + skin rim + pegs for the grid quarters + nut bosses; screws come up from below
  core      cage tube the panels wrap round: ledge, wire windows, nut tabs, bayonet collar for the cap
  grid      ONE black pixel-grid quarter (print 4): 16 cells across, seam fin on its leading edge
  skin      white diffuser tube, 2 walls
  cap       twist-off bayonet cap with click detent; its rim covers the top 7 mm border
  adapter   bolt-on pole adapter: split 31.8 mm sleeve for the quick-release clamp (one per pole)
  cradle    battery cradle for the 6,000 mAh pack, ports up; a strap or band cinches its fingers
  board     board plate (stands up, antenna edge down) + 4 printed spacers
Fasteners: M3 flat-head screws from below the base into hex nuts sitting in pockets.

Usage: python3 build_modular.py OUT_NPZ [key=value ...]   e.g. EDGE_L=5 EDGE_R=5 FACE_T=0.47 SKIN_T=0.84 HOLE_X=60 HOLE_Y=52 POLE_D=19.2
"""
import sys, numpy as np, manifold3d as m

P = dict(EDGE_L=5.0, EDGE_R=5.0, EDGE_B=5.0, EDGE_T=5.0, FACE_T=0.47, SKIN_T=0.6, HOLE_X=60.0, HOLE_Y=52.0,
         POLE_D=19.2, PIN_D=6.1, BAT_W=66.0, BAT_T=13.0, SEG=256)
for a in sys.argv[2:]:
    k, v = a.split("="); P[k] = float(v)
OUT = sys.argv[1]
SEG = int(P["SEG"])
COLS, ROWS, NP = 64, 16, 4
PITCH = 10.0                                    # LED pitch
PANEL_W = P["EDGE_L"] + 15 * PITCH + P["EDGE_R"]   # edge to edge, from his edge-to-LED measurements
PANEL_H = P["EDGE_B"] + 15 * PITCH + P["EDGE_T"]
R_MID = NP * PANEL_W / (2 * np.pi)              # mid-thickness of the panels when they meet edge to edge
R_BACK = R_MID - P["FACE_T"] / 2                # panel back
R_GI = R_MID + P["FACE_T"] / 2                  # grid fins touch the panel's face between LEDs
def s2a(sarc): return np.degrees(sarc / R_MID)  # arc length along a panel -> angle
FIN_D = 5.6                                     # fin depth, as in the test tile he liked
R_GO = R_GI + FIN_D
R_SI = R_GO + 0.25
SKIN_T = P["SKIN_T"]                             # 0.6 = one 0.6 mm vase-mode line (his pick); 0.84 = two walls, fallback
R_SO = R_SI + SKIN_T
R_RI = R_SI + 0.84 + 0.3                        # base/cap rim inner: fits the 0.6 and the 0.84 skin
R_RO = R_RI + 1.6
R_CO = R_BACK - 0.8                             # core: panels float 0.8 mm off it and spring out against the grid
R_CI = R_CO - 1.6
BORDER = 7.0
H = PANEL_H + 2 * BORDER
HT = H - 0.4                                    # grid quarters and skin: 0.4 short so the cap seats on the core
FIN, HALF_FIN, SEAM_GAP = 0.8, 0.6, 0.1                 # each quarter carries a 0.6 half-fin at both seams
SEAM_FIN = 2 * HALF_FIN + SEAM_GAP
CAP_NOTCH_W, CAP_NOTCH_D = 4.0, 1.3              # relief over the 0603 capacitor between LED rows
ROW = 0.8
BASE_T = 2.6
CAP_T = 2.0

def cyl(r, h, z0=0.0, seg=SEG): return m.Manifold.cylinder(h, r, r, seg).translate([0, 0, z0])
def tube(ri, ro, h, z0=0.0, seg=SEG): return cyl(ro, h, z0, seg) - cyl(ri, h + 2, z0 - 1, seg)
def box(x0, x1, y0, y1, z0, z1): return m.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])
def revolve(pts, seg=SEG): return m.Manifold.revolve(m.CrossSection([pts]), seg)
def rz(man, a): return man.rotate([0, 0, a])
def at(r, a): return [r * np.cos(np.radians(a)), r * np.sin(np.radians(a)), 0]
def hexpocket(z_top, depth, af=5.75):          # M3 nut pocket, opening at z_top
    return m.Manifold.cylinder(depth + 1, af / np.sqrt(3), af / np.sqrt(3), 6).translate([0, 0, z_top - depth])
def csink(z_bot, length):                      # M3 flat-head from below: cone at z_bot + shank
    return m.Manifold.cylinder(1.9, 3.3, 1.65, 32).translate([0, 0, z_bot - 0.2]) + cyl(1.7, length, z_bot - 1, 24)

def sector(r0, r1, a0, a1, z0, z1, n=24):
    ts = np.radians(np.linspace(a0, a1, n))
    pts = [(r1 * np.cos(t), r1 * np.sin(t)) for t in ts] + [(r0 * np.cos(t), r0 * np.sin(t)) for t in ts[::-1]]
    return m.Manifold.extrude(m.CrossSection([pts]), z1 - z0).translate([0, 0, z0])

def window(w, z0, z1, r0, r1):
    """Radial window at angle 0, pointed (45 deg) top and bottom so it prints without support."""
    hw = w / 2; pts = []
    for x in (r0, r1):
        pts += [(x, -hw, z0 + hw), (x, 0, z0), (x, hw, z0 + hw), (x, hw, z1 - hw), (x, 0, z1), (x, -hw, z1 - hw)]
    return m.Manifold.hull_points(pts)

def halfspace(a, off, side):
    """Points whose distance along the left-normal of the ray at angle a is >= off (side=+1) or <= off (side=-1)."""
    b = box(-400, 400, off, 400, -50, 400) if side > 0 else box(-400, 400, -400, off, -50, 400)
    return rz(b, a)

parts = {}

# ---------------- grid quarter (seam fin at angle 0, covers 0..90 deg) ----------------
g = m.Manifold()
FIN_S = [0.0] + [P["EDGE_L"] + PITCH * j + PITCH / 2 for j in range(15)] + [PANEL_W]   # fins between LED columns
for k, sf in enumerate(FIN_S):
    t = SEAM_FIN if k in (0, len(FIN_S) - 1) else FIN
    g += rz(box(R_GI - 0.03, R_GO + 0.03, -t / 2, t / 2, 0, HT), s2a(sf))
g = g ^ sector(R_GI - 1, R_GO + 1, -3, 93, -1, H + 1, 64)
ring = m.Manifold()
for k in range(ROWS - 1):
    zr = BORDER + P["EDGE_B"] + PITCH * k + PITCH / 2
    ring += tube(R_GI, R_GO, ROW, zr - ROW / 2)
ring += tube(R_GI, R_GO, BORDER + ROW / 2, 0)                    # bottom band
ring += tube(R_BACK + 0.2, R_GO, BORDER, 0)                      # lip under the panel's bottom edge
ring += tube(R_GI, R_GO, HT - (H - BORDER - ROW / 2), H - BORDER - ROW / 2)  # top band
g += ring
# capacitor reliefs: the walls between LED rows (and both bands at the panel edges) sit over the
# little 104 capacitors, so notch their back edge at every LED column
notch_z = [BORDER + P["EDGE_B"] + PITCH * k + PITCH / 2 for k in range(ROWS - 1)]
notches = m.Manifold()
for j in range(16):
    for p in range(NP):
        a = 90 * p + s2a(P["EDGE_L"] + PITCH * j)
        for zc in notch_z:
            notches += rz(box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, zc - ROW / 2 - 0.2, zc + ROW / 2 + 0.2), a)
        notches += rz(box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, BORDER - 0.2, BORDER + 1.4), a)
        notches += rz(box(R_GI - 0.5, R_GI + CAP_NOTCH_D, -CAP_NOTCH_W / 2, CAP_NOTCH_W / 2, BORDER + PANEL_H - 1.4, BORDER + PANEL_H + 0.2), a)
g -= notches
# cut through the middle of both seam fins, so each quarter ends on a half-fin and no row wall hangs free
q = g ^ halfspace(0, SEAM_GAP / 2, +1) ^ halfspace(90, -SEAM_GAP / 2, -1) ^ cyl(R_GO + 1, H + 2, -1)
for a in (22.5, 67.5):                                           # peg holes: pegs on the base locate each quarter
    q -= cyl(1.75, 4.2, -1, 24).translate(at((R_GI + R_GO) / 2, a))
parts["grid"] = q

# ---------------- skin ----------------
parts["skin"] = tube(R_SI, R_SO, HT)

# ---------------- core ----------------
core = tube(R_CI, R_CO, H)
core += tube(R_CI, R_BACK, BORDER)                               # ledge the panels stand on
PAD = s2a(36.0)                                  # plug pads sit ~36 mm in from a panel edge (photo)
WIN_W = 32.0
for s in range(NP):
    for c in (PAD, 90.0 - PAD):                                  # pad windows both sides: works either way up
        for z0, z1 in ((18.0, 76.0), (94.0, 152.0)):             # split: DIN plugs low, DOUT high, solid band between
            core -= rz(window(WIN_W, z0, z1, R_CI - 1, R_BACK + 1), 90 * s + c)
# (no middle window: the 12 mm posts it left flexed under the nozzle and printed weak)
CORE_SCREWS = [60 * k for k in range(6)]
R_TAB = R_CI - 6.0
for a in CORE_SCREWS:
    tab = box(R_CI - 12, R_CI + 0.5, -6, 6, 0, 8) ^ cyl(R_CI + 0.5, 20, -1)
    tab = rz(tab, a)
    core += tab
    core -= hexpocket(8.0, 2.6).translate(at(R_TAB, a))
    core -= cyl(1.7, 20, -1, 24).translate(at(R_TAB, a))
# bayonet collar (inside the top of the core), chamfered underneath so it prints without support
COL_RI = R_CI - 4.0
core += revolve([(R_CI + 0.5, H - 18.5), (R_CI + 0.5, H), (COL_RI, H), (COL_RI, H - 14)])   # collar, 45-deg underside
LUG_ANGS = (10.0, 130.0, 250.0)
TURN = 23.0
RUN_Z0, RUN_Z1 = H - 12.0, H - 7.0
SLOT_R = R_CI - 1.5
for a in LUG_ANGS:
    core -= sector(COL_RI - 1, SLOT_R, a - 7, a + 7, RUN_Z0, H + 1)
    core -= sector(COL_RI - 1, SLOT_R, a - TURN - 7, a + 7, RUN_Z0, RUN_Z1)                     # run: turn clockwise (from above) to lock
    core -= sector(COL_RI - 1, COL_RI + 0.9, a + 58.5, a + 61.5, H - 9.0, H + 1)
    core -= sector(COL_RI - 1, COL_RI + 0.9, a + 60 - TURN - 1.5, a + 60 - TURN + 1.5, H - 9.0, H - 3.0)
parts["core"] = core

# ---------------- cap ----------------
cap = cyl(R_RO, CAP_T, H)
cap += tube(R_RI, R_RO, BORDER, H - BORDER)
for k in range(96):
    cap += rz(box(R_RO - 0.1, R_RO + 0.7, -0.7, 0.7, H - BORDER + 1.0, H + CAP_T), 360.0 * k / 96)
SK_RO = COL_RI - 0.4
cap += tube(SK_RO - 1.6, SK_RO, 13, H - 13)
for a in LUG_ANGS:
    al = a - TURN                                                # lugs drawn in the locked position
    cap += sector(SK_RO - 0.5, SLOT_R - 0.4, al - 5, al + 5, RUN_Z0 + 0.6, RUN_Z1 - 0.5, 10)
    cap += m.Manifold.sphere(1.2, 24).scale([1, 1, 2.0]).translate([*at(SK_RO - 0.4, al + 60)[:2], H - 6.0])
parts["cap"] = cap

# ---------------- base ----------------
base = cyl(R_RO, BASE_T, -BASE_T)
base += tube(R_RI, R_RO, BORDER)                                 # rim round the bottom of the skin
for k in range(8):                                               # pegs for the grid quarters
    a = 22.5 + 45 * k
    base += m.Manifold.cylinder(2.6, 1.55, 1.3, 24).translate(at((R_GI + R_GO) / 2, a))
ADP_R, ADP_ANGS = 27.0, (45, 135, 225, 315)
for a in ADP_ANGS:                                               # adapter screws: nut bosses on top
    base += cyl(5.0, 6.0, 0, 32).translate(at(ADP_R, a))        # 6 mm tall so an M3x16 fills the nut
    base -= hexpocket(6.0, 2.6).translate(at(ADP_R, a))
    base -= cyl(1.7, 10, -BASE_T - 1, 24).translate(at(ADP_R, a))
base += tube(4.2, 9.0, 1.6, 0, 48)                               # thicker hub round the pin hole
base += tube(33.5, 36.0, 8.0, 0, 128)                            # stiff ring just outside the adapter screws
base += tube(8.5, 12.0, 8.0, 0, 64)                              # and a stiff collar round the hub
for sgn in (1, -1):                                              # ribs tying hub, bosses and ring together
    base += box(*sorted((sgn * 11.0, sgn * 34.0)), -1.2, 1.2, 0, 8)
    for ang in ADP_ANGS:
        base += rz(box(11.0, 34.0, -1.2, 1.2, 0, 6), ang)
for a in ADP_ANGS:                                               # re-cut the nut pockets the ribs crossed
    base -= hexpocket(6.0, 2.6).translate(at(ADP_R, a))
    base -= cyl(1.7, 10, -BASE_T - 1, 24).translate(at(ADP_R, a))
for sgn in (1, -1):                                              # stiffening ribs, clear of the cradle and board
    base += box(-1.2, 1.2, *sorted((sgn * 9.0, sgn * (R_CI - 1.0))), 0, 8)
base -= cyl(3.6, 10, -BASE_T - 1, 32)                            # pole tip pin passes up into the core
for a in CORE_SCREWS:
    base -= csink(-BASE_T, 12).translate(at(R_TAB, a))
CRADLE_X, CRADLE_FOOT = 50.0, 47.0
BOARD_X, BOARD_FOOT = -57.5, 52.0
for y in (-CRADLE_FOOT, CRADLE_FOOT):
    base -= csink(-BASE_T, 12).translate([CRADLE_X, y, 0])
for y in (-BOARD_FOOT, BOARD_FOOT):
    base -= csink(-BASE_T, 12).translate([BOARD_X, y, 0])
for (x, y) in [(-15, 40), (-15, -40), (5, 40), (5, -40), (-15, 60), (-15, -60), (5, 60), (5, -60)]:
    base -= cyl(1.7, 10, -BASE_T - 1, 24).translate([x, y, 0])
parts["base"] = base

# ---------------- pole adapter (assembled: flange under the base, sleeve hanging down) ----------------
def adapter(bore, pin):
    FL_R, FL_T, SL = 36.0, 7.0, 75.0                             # 7 mm flange: this joint takes the sway
    a = cyl(FL_R, FL_T, -BASE_T - FL_T, 128) + cyl(15.9, SL, -BASE_T - FL_T - SL, 128)
    a -= cyl(bore / 2 + 0.4, SL + FL_T + 2, -BASE_T - FL_T - SL - 1, 96)
    for k in range(3):                                           # crush ribs
        rib = box(bore / 2 - 0.25, bore / 2 + 1, -0.8, 0.8, -BASE_T - FL_T - SL, -BASE_T)
        a += rz(rib, 120 * k + 60)
    for sgn in (0, 180):                                         # slits so the clamp squeezes the sleeve
        a -= rz(box(bore / 2 - 2, 20, -0.8, 0.8, -BASE_T - FL_T - SL - 1, -BASE_T - FL_T - SL + 42), sgn + 90)
    for ang in (0, 90, 180, 270):                                # gussets between sleeve and flange
        tri = m.CrossSection([[(15.5, 0.0), (15.5, -22.0), (33.0, 0.0)]])
        g = m.Manifold.extrude(tri, 4.0).translate([0, 0, -2.0]).rotate([90, 0, 0])
        a += rz(g.translate([0, 0, -BASE_T - FL_T]), ang)
    for ang in ADP_ANGS:                                         # flat heads from below, countersunk in the flange
        hole = m.Manifold.cylinder(1.9, 3.3, 1.65, 32).translate([0, 0, -BASE_T - FL_T - 0.1]) + cyl(1.7, FL_T + 2, -BASE_T - FL_T - 1, 24)
        a -= hole.translate(at(ADP_R, ang))
    return a
parts["adapter"] = adapter(P["POLE_D"], True)
parts["adapter_1in"] = adapter(25.4, False)

# ---------------- threaded pole adapter: 3/4"-5 Acme tip (mop/paint-roller thread) ----------------
# His pole (Nov 19): thread 24.4 long; silver collar 25.1 OD x 25 tall below it; then a 28.5 OD section with bolts.
# Test nut 2 (0.45 mm radial clearance, crest widened 0.4) fit perfectly; nut 1 (0.25) didn't.
ACME_P = 25.4 / 5
ACME_R = 25.4 * 0.75 / 2
def acme_male(clear, h, crest_extra=0.4, n=360):
    depth = ACME_P / 2
    crest = 0.3707 * ACME_P
    flank = depth * np.tan(np.radians(14.5))
    rmaj, rmin = ACME_R + clear, ACME_R - depth + clear
    cw = crest + crest_extra
    rw = ACME_P - cw - 2 * flank
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
    return m.Manifold.extrude(m.CrossSection([pts]), h, int(turns * 90) + 1, 360.0 * turns)   # right-hand

T_TOP = 1.0                     # solid flange skin above the thread
T_LEN = 25.5                    # thread socket depth (his thread is 24.4)
C_LEN = 24.5                    # collar socket (his collar is 25; stops 0.5 short of the bolt section)
C_R = 25.1 / 2 + 0.3
def adapter_thread():
    FL_R, FL_T = 36.0, 7.0
    z0 = -BASE_T
    depth = T_TOP + T_LEN + C_LEN
    SL = depth - FL_T
    a = cyl(FL_R, FL_T, z0 - FL_T, 128) + cyl(15.9, SL + 0.5, z0 - depth, 128)
    zt = z0 - T_TOP - T_LEN                                      # shoulder the collar seats against
    a -= acme_male(0.45, T_LEN + ACME_P).translate([0, 0, zt - ACME_P])
    a -= cyl(C_R, C_LEN + 1, z0 - depth - 1, 128)
    a -= m.Manifold.cylinder(1.2, C_R + 1.0, C_R, 128).translate([0, 0, z0 - depth - 0.01])   # lead-in
    for sgn in (0, 180):                                         # slits so the clamp squeezes the sleeve on the collar
        a -= rz(box(C_R - 1, 20, -0.8, 0.8, z0 - depth - 1, z0 - 29.0), sgn + 90)
    for ang in (0, 90, 180, 270):                                # gussets between sleeve and flange
        tri = m.CrossSection([[(15.5, 0.5), (15.5, -22.0), (33.0, 0.0), (33.0, 0.5)]])
        g = m.Manifold.extrude(tri, 4.0).translate([0, 0, -2.0]).rotate([90, 0, 0])
        a += rz(g.translate([0, 0, z0 - FL_T]), ang)
    for ang in ADP_ANGS:                                         # flat heads from below, countersunk in the flange
        hole = m.Manifold.cylinder(1.9, 3.3, 1.65, 32).translate([0, 0, z0 - FL_T - 0.1]) + cyl(1.7, FL_T + 2, z0 - FL_T - 1, 24)
        a -= hole.translate(at(ADP_R, ang))
    # fit check: his tip at nominal size, screwed up to the shoulder
    tip = acme_male(0.0, 24.4, crest_extra=0.0).translate([0, 0, zt]) + cyl(25.1 / 2, 25.0, zt - 25.0, 128)
    print("  threaded adapter vs nominal tip overlap (mm3):", round((a ^ tip).volume(), 3))
    return a
parts["adapter_thread"] = adapter_thread()

# ---------------- battery cradle (own coordinates: long axis along y, centred at origin) ----------------
BW, BT = P["BAT_W"] + 2.0, P["BAT_T"] + 2.0                      # 1 mm each side for foam tape
WALL, DEPTH = 2.2, 72.0
ox, oy = BT / 2 + WALL, BW / 2 + WALL
cr = box(-ox, ox, -oy, oy, 0, 3 + DEPTH)
cr -= box(-BT / 2, BT / 2, -BW / 2, BW / 2, 3, 3 + DEPTH + 1)
for y in (-BW / 4, 0, BW / 4):                                   # slits: the wide walls become fingers
    cr -= box(-ox - 1, ox + 1, y - 0.8, y + 0.8, 38, 3 + DEPTH + 1)
for z in (52.0, 72.0):                                           # ribs make a channel for the strap
    cr += box(-ox - 1.2, ox + 1.2, -oy - 1.2, oy + 1.2, z, z + 1.6) - box(-ox + 0.1, ox - 0.1, -oy + 0.1, oy - 0.1, z - 1, z + 3)
    for y in (-BW / 4, 0, BW / 4):
        cr -= box(-ox - 2, ox + 2, y - 0.8, y + 0.8, z - 1, z + 3)
for s in (-1, 1):                                                # feet with nut pockets
    foot = box(-ox, ox, *sorted((s * (oy - 1), s * (CRADLE_FOOT + 6))), 0, 8)
    cr += foot
    gus = m.CrossSection([[(0, 0), (12, 0), (0, 30)]])
    gus = m.Manifold.extrude(gus, 3).translate([0, 0, -1.5]).rotate([90, 0, 0]).rotate([0, 0, 90 if s > 0 else -90])
    cr += gus.translate([0, s * oy, 0])
    cr -= hexpocket(8.0, 2.6).translate([0, s * CRADLE_FOOT, 0])
    cr -= cyl(1.7, 20, -1, 24).translate([0, s * CRADLE_FOOT, 0])
parts["cradle"] = cr
cradle_asm = cr.translate([CRADLE_X, 0, 0])

# ---------------- Anker 537 cradle (assembled coords): pack on its long edge, centred on the pole axis ----------------
# Anker 537 PowerCore 24K: 160 x 84.9 x 26.4 mm. Long axis along y, ports at the -y end. 1 mm all round for foam.
A_L, A_H, A_T = 160.0 + 1.0, 84.9 + 1.0, 26.4 + 1.0
AW = 2.2
FZ = 11.05                                                       # floor top: the block clears the base's ribs (8 mm)
ax, ay = A_T / 2, A_L / 2
c5 = box(-ax - AW, ax + AW, -ay - AW, ay + AW, 0, FZ)            # floor block, sits flat on the base
c5 += box(ax, ax + AW, -ay - AW, ay + AW, FZ, FZ + 55) + box(-ax - AW, -ax, -ay - AW, ay + AW, FZ, FZ + 55)  # side walls
c5 += box(-ax - AW, ax + AW, ay, ay + AW, FZ, FZ + 55)           # end wall (far from the ports)
c5 += box(-ax - AW, ax + AW, -ay - AW, -ay, FZ, FZ + 8)           # low lip at the port end
for s in (1, -1):                                                # arms out to the base's cradle holes
    c5 += box(ax + AW - 0.5, CRADLE_X + 6, s * CRADLE_FOOT - 6, s * CRADLE_FOOT + 6, 0, FZ) if s > 0 else \
          box(ax + AW - 0.5, CRADLE_X + 6, s * CRADLE_FOOT - 6, s * CRADLE_FOOT + 6, 0, FZ)
STRAP_Y = (-22.0, 22.0)
# reliefs for the base's ribs, hub, ring and nut bosses (all 8 mm tall or less), 0.4 mm clearance
keep = cyl(12.5, 8.45, -0.1, 64) + tube(33.1, 36.4, 8.55, -0.1, 128)
for sgn in (1, -1):
    keep += box(*sorted((sgn * 10.6, sgn * 34.4)), -1.6, 1.6, -0.1, 8.45)
    keep += box(-1.6, 1.6, *sorted((sgn * 8.6, sgn * (R_CI - 0.6))), -0.1, 8.45)
for a in ADP_ANGS:
    keep += rz(box(10.6, 34.4, -1.6, 1.6, -0.1, 6.45), a)
    keep += cyl(5.4, 6.55, -0.1, 32).translate(at(ADP_R, a))
c5 -= keep
for y in STRAP_Y:                                                # strap channels under the pack, out under both walls
    c5 -= box(-ax - AW - 1, ax + AW + 1, y - 8.25, y + 8.25, FZ - 2.0, FZ + 0.01)
CR5_SCREWS = [(5.0, -60.0), (5.0, -40.0), (5.0, 40.0), (5.0, 60.0), (CRADLE_X, -CRADLE_FOOT), (CRADLE_X, CRADLE_FOOT)]
for (x, y) in CR5_SCREWS:                                        # M3x12 from below; nut drops to the pocket floor
    c5 -= hexpocket(FZ, FZ - 6.0).rotate([0, 0, 30]).translate([x, y, 0])
    c5 -= cyl(1.7, 20, -1, 24).translate([x, y, 0])
c5 = c5 ^ cyl(R_CI - 1.0, 200, -1, 128)
parts["cradle537"] = c5
for (x, y) in [(5.0, -60.0), (5.0, -40.0), (5.0, 40.0), (5.0, 60.0)]:
    base -= csink(-BASE_T, 12).translate([x, y, 0])              # the spare holes become countersunk anchors
parts["base"] = base

# ---------------- board plate (own coordinates: plate in the yz plane, board side +x) ----------------
PW, PH, PT = 90.0, 84.0, 3.0
pl = box(0, PT, -PW / 2, PW / 2, 0, PH)
for s in (-1, 1):
    pl += box(-6, PT + 6, *sorted((s * (PW / 2 - 4), s * (BOARD_FOOT + 6))), 0, 8)
    pl -= hexpocket(8.0, 2.6).translate([PT / 2, s * BOARD_FOOT, 0])
    pl -= cyl(1.7, 20, -1, 24).translate([PT / 2, s * BOARD_FOOT, 0])
for yy in (-40, 0, 40):                                          # gussets behind the plate (clear of the board nuts at y +-30)
    gus = m.CrossSection([[(0, 0), (14, 0), (0, 40)]])
    pl += m.Manifold.extrude(gus, 3).translate([0, 0, -1.5]).rotate([90, 0, 0]).rotate([0, 0, 180]).translate([0, yy, 0])
BC_Z = 12.0 + 34.0                                               # board centre height: bottom edge ~12 mm up
d = np.array([P["HOLE_X"] / 2, P["HOLE_Y"] / 2]); u = d / np.linalg.norm(d)
for sx in (-1, 1):
    for sz in (-1, 1):
        c = np.array([sx * d[0], sz * d[1]]); v = np.array([sx * u[0], sz * u[1]])
        p0, p1 = c - v * 1.0, c + v * 1.0                        # short round-ended slots for M2.5: slack on his 60 x 52 mm
        holes = [m.Manifold.cylinder(PT + 2, 1.45, 1.45, 24).rotate([0, 90, 0]).translate([-1, p[0], BC_Z + p[1]]) for p in (p0, p1)]
        pl -= m.Manifold.batch_hull(holes)
pl -= cyl(8, 10, -1, 48).rotate([0, 90, 0]).translate([-2, 0, PH + 3])   # finger notch at the top
parts["board"] = pl
board_asm = pl.translate([BOARD_X - PT / 2, 0, 0])
sp = tube(1.5, 3.5, 12.0, 0, 32)                                  # 3.0 mm bore for M2.5
spacers = m.Manifold()
for i in range(5):
    spacers += sp.translate([i * 10.0, 0, 0])
parts["spacers"] = spacers

# ---------------- checks ----------------
asm = {"base": base, "core": core, "cap": cap, "skin": parts["skin"], "adapter": parts["adapter"],
       "cradle": cradle_asm, "board": board_asm}
for k in range(NP):
    asm[f"grid{k}"] = rz(q, 90 * k)
names = list(asm)
print("overlaps (cm3, should be ~0):")
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        v = (asm[names[i]] ^ asm[names[j]]).volume() / 1000
        if v > 0.001: print(f"  {names[i]} x {names[j]}: {v:.3f}")
# Anker 537 layout: its cradle replaces the small one
asm5 = {k: v for k, v in asm.items() if k not in ("cradle", "adapter")}
asm5["cradle537"] = parts["cradle537"]
for nm in asm5:
    if nm != "cradle537":
        v = (asm5[nm] ^ parts["cradle537"]).volume() / 1000
        if v > 0.001: print(f"  cradle537 x {nm}: {v:.3f}")
bat5 = box(-A_T / 2 + 0.5, A_T / 2 - 0.5, -A_L / 2 + 0.5, A_L / 2 - 0.5, FZ, FZ + A_H - 1)
plug5 = box(-12, 12, -A_L / 2 - 15, -A_L / 2, FZ + 8, FZ + 75)
for nm, env in (("anker", bat5), ("anker plug", plug5)):
    hits = [(n, round((env ^ asm5[n]).volume() / 1000, 3)) for n in asm5]
    print(f"  {nm} envelope hits: {[h for h in hits if h[1] > 0.001]}")
# battery envelope and board envelope
bat = box(CRADLE_X - P["BAT_T"] / 2, CRADLE_X + P["BAT_T"] / 2, -P["BAT_W"] / 2, P["BAT_W"] / 2, 3.0, 3 + 112)
brd = box(BOARD_X + PT / 2 + 12, BOARD_X + PT / 2 + 12 + 1.6 + 14, -36, 36, 12, 80)
print("  anker vs board envelope:", round((bat5 ^ brd).volume() / 1000, 3))
pin = cyl(3.05, 43, 0, 32)
for nm, env in (("battery", bat), ("board", brd), ("pin", pin)):
    hits = [(n, (env ^ asm[n]).volume() / 1000) for n in names if n not in ("cradle",) or nm != "battery"]
    hits = [h for h in hits if h[1] > 0.001 and not (nm == "board" and h[0] == "board")]
    print(f"  {nm} envelope hits: {hits}")
print(f"panel {PANEL_W:.2f} x {PANEL_H:.2f}  seam LED gap {P['EDGE_L']+P['EDGE_R']:.2f}  R_BACK {R_BACK:.2f}  core OD {2*R_CO:.1f}  grid {2*R_GI:.1f}-{2*R_GO:.1f}  skin OD {2*R_SO:.1f}  base/cap OD {2*R_RO:.1f}  H {H}")

def mesh(man):
    mm = man.to_mesh()
    return np.asarray(mm.vert_properties)[:, :3].astype(np.float32), np.asarray(mm.tri_verts).astype(np.int32)
out = {}
for name, man in list(parts.items()) + [("cradle_asm", cradle_asm), ("board_asm", board_asm)]:
    v, t = mesh(man)
    out[name + "_v"], out[name + "_t"] = v, t
    bb = v.max(0) - v.min(0)
    print(f"{name:12s} tris={len(t):7d} vol={man.volume()/1000:6.1f} cm3 size={np.round(bb,1)} genus={man.genus()} status={man.status()}")
out["params"] = np.array([R_BACK, R_GI, R_GO, R_SI, R_SO, R_CO, H, BORDER, PANEL_W, PANEL_H, P["EDGE_L"], P["EDGE_B"], R_MID, BASE_T])
np.savez_compressed(OUT, **out)
