"""Write the round v2 print files (A2L bed 330 x 320). Usage: python3 export_round.py PARTS_NPZ OUT_DIR
Each part is turned to its print orientation here, then written with ../common/write_plain_3mf.py."""
import sys, os, subprocess, tempfile, numpy as np
P = dict(np.load(sys.argv[1])); OUT = sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
WRITER = os.path.join(HERE, "..", "common", "write_plain_3mf.py")
def rot(v, axis, deg):
    a = np.radians(deg); c, s = np.cos(a), np.sin(a)
    R = {"x": [[1, 0, 0], [0, c, -s], [0, s, c]], "y": [[c, 0, s], [0, 1, 0], [-s, 0, c]], "z": [[c, -s, 0], [s, c, 0], [0, 0, 1]]}[axis]
    return v @ np.array(R).T
O = {}
def orient(key, new, *steps):
    v = P[key + "_v"].astype(float)
    for ax, d in steps: v = rot(v, ax, d)
    O[new + "_v"], O[new + "_t"] = v, P[key + "_t"]
orient("ft_top", "ft_top", ("x", 180))                 # cap pad on the bed
orient("thumb_nut", "thumb_nut")
orient("ft_bottom", "ft_bottom")
orient("bushing_half", "bushing", ("y", 90))           # flat split face on the bed
orient("spine", "spine")
orient("base_half", "base", ("z", 90))                 # long edge along the bed's 330 mm side
orient("core_half", "core", ("z", 90))
orient("cap_half", "cap", ("x", 180), ("z", 90))       # top face on the bed
orient("grid_sector", "grid", ("z", 30))               # standing up, chord along x
orient("skin_third", "skin", ("z", 60))
orient("skin_third_084", "skin084", ("z", 60))
tmp = os.path.join(tempfile.mkdtemp(), "o.npz"); np.savez(tmp, **O)
def w(name, *items):
    subprocess.run([sys.executable, WRITER, tmp, os.path.join(OUT, name + ".3mf"), "330,320", *items], check=True)
w("FIT TEST 1 - Pole top and thumb nut (black PETG)", "ft_top@110,160", "thumb_nut@220,160")
w("FIT TEST 2 - Bushing halves and tube bottom (black PETG)", "ft_bottom@100,160", "bushing@190,120,90", "bushing@190,200,90")
w("Spine (black PETG)", "spine@165,160")
w("Thumb nut spare (black PETG)", "thumb_nut@165,160")
w("Base half (black, print 2)", "base@165,160")
w("Core half (black, print 2)", "core@165,160")
w("Cap half (black, print 2)", "cap@165,160")
w("Grid sector (black, print 6)", "grid@165,160")
w("Grid sectors x3 (black, print 2)", "grid@165,95", "grid@165,160", "grid@165,225")
w("Skin third 0.6 mm (white, print 3)", "skin@165,160")
w("Skin third 0.84 mm fallback (white, print 3)", "skin084@165,160")
