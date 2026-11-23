"""Home stand for the totem: ballast tray + lid + socket tower + pole bushing + fit-test rings.
Assembled coordinates in mm, z=0 is the floor side of the tray, the axis is the pole.
Usage: python3 build_stand.py OUT_NPZ [POLE_D=28.4 RIB_D=28.4 R_OUT=150 ...]
  POLE_D  outside diameter of the pole's bottom section (1.12 in = 28.4)
  RIB_D   (no foot cap) diameter across the bushing's crush ribs
  CAP_RIB diameter across the crush ribs that grip the rubber foot cap (0 = no cap: grip the tube with RIB_D)
          With a cap, the bore clears the cap all the way down and only the bottom 25 mm grips it.
"""
import sys, numpy as np, manifold3d as m
P = dict(POLE_D=28.4, RIB_D=28.4, CAP_RIB=30.0, R_OUT=150.0, H_TRAY=40.0, TOWER_H=120.0, SEG=192)
for a in sys.argv[2:]:
    k, v = a.split("="); P[k] = float(v)
OUT, SEG = sys.argv[1], int(P["SEG"])
R_OUT, H_TRAY = P["R_OUT"], P["H_TRAY"]
FLOOR, WALL, LID_T = 2.4, 3.0, 3.0
Z_LID = H_TRAY - LID_T                     # ribs, hub and ledge top; lid sits here
R_LEDGE_IN, R_LEDGE_OUT = R_OUT - WALL - 2.0, R_OUT - WALL   # 2 mm ledge carries the lid's rim
R_HUB, R_BORE = 50.0, 22.3                 # hub and tower share the socket bore
R_BOLT, N_RIBS = 40.0, 8
FLANGE_R, FLANGE_T, R_COL = 56.0, 6.0, 30.0
Z_TOP = H_TRAY + P["TOWER_H"]              # top of the tower
B_OR = R_BORE - 0.3                        # bushing outside: 0.3 mm slip in the bore
CAP = P["CAP_RIB"] > 0
B_IR = (P["CAP_RIB"] + 1.2) / 2 if CAP else P["POLE_D"] / 2 + 0.4   # bore clears the cap (or tube); ribs grip
LIP_R, LIP_T = 27.0, 2.0

def cyl(r, h, z0=0.0, seg=SEG): return m.Manifold.cylinder(h, r, r, seg).translate([0, 0, z0])
def tube(ri, ro, h, z0=0.0, seg=SEG): return cyl(ro, h, z0, seg) - cyl(ri, h + 2, z0 - 1, seg)
def at(r, a): return [r * np.cos(np.radians(a)), r * np.sin(np.radians(a)), 0]
def box(x0, x1, y0, y1, z0, z1): return m.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def ribs(rib_d, h, z0, n=6, w=2.0):
    """Crush ribs on the inside of the bushing bore, reaching in to rib_d/2."""
    out = m.Manifold()
    for i in range(n):
        out += box(rib_d / 2, B_IR + 0.6, -w / 2, w / 2, z0, z0 + h).rotate([0, 0, 360 * i / n])
    return out

# ---------- tray ----------
tray = cyl(R_OUT, H_TRAY) - cyl(R_LEDGE_IN, H_TRAY, FLOOR) - cyl(R_LEDGE_OUT, LID_T + 1, Z_LID)
tray += tube(R_BORE, R_HUB, Z_LID - FLOOR, FLOOR)
for i in range(N_RIBS):
    tray += box(R_HUB - 1, R_LEDGE_IN + 0.5, -1.2, 1.2, FLOOR, Z_LID).rotate([0, 0, 22.5 + 45 * i])
for a in (0, 180):                          # finger notches to lift the lid
    tray -= box(R_OUT - WALL - 3, R_OUT + 1, -12, 12, Z_LID - 1.0, H_TRAY + 1).rotate([0, 0, a])
tray -= cyl(R_BORE, H_TRAY, FLOOR)          # socket continues down into the hub
for a in (0, 90, 180, 270):                 # M3 nuts drop in from the top, screws come down through tower + lid
    tray -= m.Manifold.cylinder(3.6, 5.75 / np.sqrt(3), 5.75 / np.sqrt(3), 6).translate(at(R_BOLT, a)).translate([0, 0, Z_LID - 3.0])
    tray -= cyl(1.7, 12, Z_LID - 12, 24).translate(at(R_BOLT, a))
for i in range(8):                          # felt-pad locators under the rim, 1 in pads
    tray -= cyl(13.2, 0.6, -0.01, 64).translate(at(R_OUT - 15, 45 * i))

# ---------- lid ----------
lid = tube(R_BORE + 0.7, R_LEDGE_OUT - 0.3, LID_T, Z_LID)
for a in (0, 90, 180, 270):
    lid -= cyl(1.75, 10, Z_LID - 1, 24).translate(at(R_BOLT, a))

# ---------- tower ----------
zf = H_TRAY
tower = cyl(FLANGE_R, FLANGE_T, zf) + cyl(R_COL, P["TOWER_H"], zf)
tower += cyl(R_COL + 2, 1.5, Z_TOP - 1.5)   # small bead round the top
gus = m.Manifold.extrude(m.CrossSection([[(R_COL - 1, 0), (FLANGE_R - 1, 0), (R_COL - 1, 45)]]), 5).translate([0, 0, -2.5])
gus = gus.rotate([90, 0, 0]).translate([0, 0, zf + FLANGE_T])
for a in (45, 135, 225, 315):
    tower += gus.rotate([0, 0, a])
tower -= cyl(R_BORE, P["TOWER_H"] + 10, zf - 5)
for a in (0, 90, 180, 270):
    tower -= cyl(1.75, 20, zf - 1, 24).translate(at(R_BOLT, a))

# ---------- bushing (prints lip down) ----------
L_B = Z_TOP - FLOOR
bush = tube(B_IR, B_OR, L_B, FLOOR) + tube(B_IR, LIP_R, LIP_T, Z_TOP)
bush -= m.Manifold.cylinder(4, B_IR, B_IR + 2.5, SEG).translate([0, 0, Z_TOP + LIP_T - 3.99])   # lead-in at the mouth
bush += ribs(P["CAP_RIB"], 25, FLOOR + 2) if CAP else ribs(P["RIB_D"], L_B - 6, FLOOR + 2)
bush = bush ^ cyl(LIP_R + 1, L_B + LIP_T, FLOOR)   # trim
bush -= m.Manifold.cylinder(4, B_IR, B_IR + 2.5, SEG).translate([0, 0, Z_TOP + LIP_T - 3.99])
bush -= m.Manifold.cylinder(2.5, B_IR + 2, B_IR, SEG).translate([0, 0, FLOOR - 0.01])          # lead-in at the bottom too

# ---------- fit test rings: ring k has k bumps; the capped foot should go in with a firm push ----------
FIT = [29.0, 29.5, 30.0, 30.5, 31.0, 31.5, 32.0] if CAP else list(np.round(P["POLE_D"] + np.array([-0.4, -0.2, 0.0, 0.2, 0.4]), 2))
fits = []
for k, d in enumerate(FIT, start=1):
    ri = (d + 1.2) / 2 if CAP else B_IR
    rr = m.Manifold()
    for i in range(6):
        rr += box(d / 2, ri + 0.6, -1.0, 1.0, 0, 14).rotate([0, 0, 60 * i])
    r = (tube(ri, ri + 3.0, 14, 0) + rr) ^ cyl(ri + 3.0, 14, 0)
    r -= m.Manifold.cylinder(3, ri, ri + 2, SEG).translate([0, 0, 11.01])
    for j in range(k):
        r += cyl(1.4, 14, 0, 24).translate(at(ri + 3.0, 90 + 13 * j))
    fits.append((f"fit{k}", r, float(d)))

def mesh(mf):
    mm = mf.to_mesh(); return np.asarray(mm.vert_properties)[:, :3].astype(np.float32), np.asarray(mm.tri_verts).astype(np.int32)
out = {}
parts = {"tray": tray, "lid": lid, "tower": tower, "bushing": bush}
parts.update({k: r for k, r, d in fits})
for k, mf in parts.items():
    v, t = mesh(mf); out[k + "_v"], out[k + "_t"] = v, t
    lo, hi = v.min(0), v.max(0)
    print(f"{k:8s} vol {mf.volume()/1000:7.1f} cm3  size {np.round(hi-lo,1)}  genus {mf.genus()}")
out["params"] = np.array([R_OUT, H_TRAY, Z_LID, FLOOR, R_BORE, Z_TOP, P["POLE_D"], P["RIB_D"]]); out["cap_rib"] = np.array([P["CAP_RIB"]])
out["fit_d"] = np.array([d for k, r, d in fits])
# ballast cavity volume
cav = cyl(R_LEDGE_IN, Z_LID - FLOOR, FLOOR) - cyl(R_HUB, Z_LID, 0)
for i in range(N_RIBS):
    cav -= box(R_HUB - 1, R_LEDGE_IN + 0.5, -1.2, 1.2, FLOOR, Z_LID).rotate([0, 0, 22.5 + 45 * i])
print(f"ballast cavity {cav.volume()/1e6:.2f} L in {N_RIBS} bays")
out["cavity_L"] = np.array([cav.volume() / 1e6])
np.savez_compressed(OUT, **out)
