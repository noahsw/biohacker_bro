"""Test nuts for the pole's threaded tip: 3/4"-5 Acme (US mop / paint-roller standard).
Usage: python3 build_thread_test.py OUT_NPZ
Two nuts: 1 notch = snug (0.25 mm radial clearance), 2 notches = loose (0.45 mm)."""
import sys, numpy as np, manifold3d as m

P = 25.4 / 5                       # pitch 5.08 mm
R_MAJ = 25.4 * 0.75 / 2            # 9.525
DEPTH = P / 2                      # 2.54
CREST = 0.3707 * P                 # 1.883
FLANK = DEPTH * np.tan(np.radians(14.5))

def male_thread(clear, h, crest_extra=0.4, n=720):
    """Solid male Acme thread grown by `clear` radially and widened axially, z from 0 to h."""
    rmaj, rmin = R_MAJ + clear, R_MAJ - DEPTH + clear
    cw = CREST + crest_extra            # wider crest = axial clearance
    rw = P - cw - 2 * FLANK             # root width
    def r_of(u):                        # u: position along one pitch at this angle
        u = u % P
        if u < cw: return rmaj
        u -= cw
        if u < FLANK: return rmaj - (rmaj - rmin) * u / FLANK
        u -= FLANK
        if u < rw: return rmin
        u -= rw
        return rmin + (rmaj - rmin) * u / FLANK
    th = np.linspace(0, 2 * np.pi, n, endpoint=False)
    pts = [(r_of(t / (2 * np.pi) * P) * np.cos(t), r_of(t / (2 * np.pi) * P) * np.sin(t)) for t in th]
    cs = m.CrossSection([pts])
    turns = h / P
    return m.Manifold.extrude(cs, h, int(turns * 120) + 1, 360.0 * turns)   # +twist = right-hand thread

def nut(clear, notches, H=16.0, OD=34.0):
    body = m.Manifold.cylinder(H, OD / 2, OD / 2, 96)
    for k in range(24):                                          # grip ribs
        a = 360 / 24 * k
        body += m.Manifold.cylinder(H, 1.2, 1.2, 16).translate([OD / 2, 0, 0]).rotate([0, 0, a])
    body -= male_thread(clear, H + 2 * P).translate([0, 0, -P])
    for j in range(notches):                                     # notch count = which nut
        body -= m.Manifold.cube([3, 4, 3]).translate([-1.5 + 5 * j - 2.5 * (notches - 1), OD / 2 - 2, H - 2.5])
    body -= m.Manifold.cylinder(1.0, R_MAJ + 1.0, R_MAJ + 0.3, 96).translate([0, 0, H - 1.0])  # lead-in chamfers
    body -= m.Manifold.cylinder(1.0, R_MAJ + 0.3, R_MAJ + 1.0, 96)
    return body

out = {}
for name, c, k in (("nut_snug", 0.25, 1), ("nut_loose", 0.45, 2)):
    b = nut(c, k)
    me = b.to_mesh()
    out[name + "_v"] = me.vert_properties[:, :3]; out[name + "_t"] = me.tri_verts
    print(name, round(b.volume() / 1000, 2), "cm3", b.status(), "genus", b.genus())
np.savez(sys.argv[1], **out)
