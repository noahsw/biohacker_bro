"""v1 threaded pole adapter, revision b (Nov 22).
Why: on v1 the 31.8 mm sleeve needed pliers to get the 31.8 mm ROCKBROS clamp over it, and tightening made PETG crack.
Changes from build_modular.adapter_thread(): sleeve 31.4 mm with a lead-in so the open clamp slides on by hand;
round stress-relief holes at the top of the two slits (sharp slit ends are where a squeezed sleeve splits);
gussets and flange unchanged. Usage: python3 build_adapter_thread_v1b.py OUT_3MF"""
import sys, os, subprocess, tempfile, numpy as np, manifold3d as m
SEG = 128
BASE_T = 2.6
ADP_R, ADP_ANGS = 27.0, (45, 135, 225, 315)
def cyl(r, h, z0=0.0, seg=SEG): return m.Manifold.cylinder(h, r, r, seg).translate([0, 0, z0])
def box(x0, x1, y0, y1, z0, z1): return m.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])
def rz(man, a): return man.rotate([0, 0, a])
def at(r, a): return [r * np.cos(np.radians(a)), r * np.sin(np.radians(a)), 0]
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
    return m.Manifold.extrude(m.CrossSection([pts]), h, int(turns * 90) + 1, 360.0 * turns)
T_TOP, T_LEN, C_LEN = 1.0, 25.5, 24.5
C_R = 25.1 / 2 + 0.3
SLEEVE_R = 31.4 / 2
FL_R, FL_T = 36.0, 7.0
z0 = -BASE_T
depth = T_TOP + T_LEN + C_LEN
SL = depth - FL_T
SLIT_TOP = z0 - 29.0
a = cyl(FL_R, FL_T, z0 - FL_T) + cyl(SLEEVE_R, SL + 0.5, z0 - depth)
zt = z0 - T_TOP - T_LEN
a -= acme_male(0.45, T_LEN + ACME_P).translate([0, 0, zt - ACME_P])
a -= cyl(C_R, C_LEN + 1, z0 - depth - 1)
a -= m.Manifold.cylinder(1.2, C_R + 1.0, C_R, SEG).translate([0, 0, z0 - depth - 0.01])          # lead-in for the collar
chamfer = cyl(SLEEVE_R + 2, 1.5, z0 - depth - 0.01) - m.Manifold.cylinder(1.5, SLEEVE_R - 1.5, SLEEVE_R, SEG).translate([0, 0, z0 - depth - 0.01])
a -= chamfer                                                                                         # lead-in for the clamp
for sgn in (0, 180):
    a -= rz(box(C_R - 1, 20, -0.8, 0.8, z0 - depth - 1, SLIT_TOP), sgn + 90)
    a -= rz(m.Manifold.cylinder(12, 1.6, 1.6, 48).rotate([0, 90, 0]).translate([C_R - 1, 0, SLIT_TOP]), sgn + 90)   # relief hole
for ang in (0, 90, 180, 270):
    tri = m.CrossSection([[(15.5, 0.5), (15.5, -22.0), (33.0, 0.0), (33.0, 0.5)]])
    g = m.Manifold.extrude(tri, 4.0).translate([0, 0, -2.0]).rotate([90, 0, 0])
    a += rz(g.translate([0, 0, z0 - FL_T]), ang)
for ang in ADP_ANGS:
    hole = m.Manifold.cylinder(1.9, 3.3, 1.65, 32).translate([0, 0, z0 - FL_T - 0.1]) + cyl(1.7, FL_T + 2, z0 - FL_T - 1, 24)
    a -= hole.translate(at(ADP_R, ang))
tip = acme_male(0.0, 24.4, crest_extra=0.0).translate([0, 0, zt]) + cyl(25.1 / 2, 25.0, zt - 25.0)
print("overlap with nominal tip (mm3):", round((a ^ tip).volume(), 3), " volume cm3:", round(a.volume() / 1000, 1), "genus", a.genus())
# print orientation: flange down (as v1)
a = a.rotate([180, 0, 0]); bb = a.bounding_box(); a = a.translate([0, 0, -bb[2]])
mm = a.to_mesh()
tmp = os.path.join(tempfile.mkdtemp(), "a.npz")
np.savez(tmp, adp_v=np.asarray(mm.vert_properties)[:, :3], adp_t=np.asarray(mm.tri_verts))
writer = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common", "write_plain_3mf.py")
subprocess.run([sys.executable, writer, tmp, sys.argv[1], "330,320", "adp@165,160"], check=True)
