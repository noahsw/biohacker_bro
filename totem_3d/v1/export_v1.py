"""Write every v1 (4-panel modular) print file for the Bambu A2L (bed 330 x 320).
Usage: python3 export_v1.py OUT_DIR [key=value ...]
Any key=value is passed to build_modular.py (e.g. POLE_D=25.4 HOLE_X=60). It builds the parts itself
(build_modular.py twice, for the 0.6 mm and 0.84 mm skins, plus build_thread_test.py and
build_adapter_thread_v1b.py), then lays each file out exactly as the files that were printed for Dreamstate."""
import sys, os, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WRITER = os.path.join(HERE, "..", "common", "write_plain_3mf.py")
OUT, ARGS = sys.argv[1], sys.argv[2:]
os.makedirs(OUT, exist_ok=True)
tmp = tempfile.mkdtemp()
PY = sys.executable


def run(*cmd):
    subprocess.run([PY, *cmd], check=True)


parts, parts084, nuts = (os.path.join(tmp, n) for n in ("parts.npz", "parts084.npz", "nuts.npz"))
run(os.path.join(HERE, "build_modular.py"), parts, *ARGS)
run(os.path.join(HERE, "build_modular.py"), parts084, *ARGS, "SKIN_T=0.84")
run(os.path.join(HERE, "build_thread_test.py"), nuts)


def w(npz, name, *items):
    run(WRITER, npz, os.path.join(OUT, name + ".3mf"), "330,320", *items)


# Spec per object: key@x,y[,rotation deg][!flip]  (flip = upside down, i.e. printed top face on the bed)
w(parts, "A2L - Base (black)", "base@165,160")
w(parts, "A2L - Core (black)", "core@165,160")
w(parts, "A2L - Cap (black)", "cap@165,160!flip")
w(parts, "A2L - Grid quarter fit test (black, print 1 first)", "grid@165,160,-45")
w(parts, "A2L - Grid quarters x4 (black)", *[f"grid@{x},160,-45" for x in (93, 141, 189, 237)])
w(parts, "A2L - Skin 0.6 mm (white, spiral vase)", "skin@165,160")
w(parts084, "A2L - Skin 0.84 mm fallback (white)", "skin@165,160")
w(parts, "A2L - Battery cradle Anker 537 (black)", "cradle537@165,160")
w(parts, "A2L - Board plate and spacers (black)", "board@140,160", "spacers@220,160,90")
w(parts, "A2L - Small parts (black) - REDCAMP adapter, cradle, board plate",
  "adapter@80,95!flip", "cradle@95,235,90", "board@215,160", "spacers@290,160,90")
w(parts, "A2L - Pole adapter 1 inch (black)", "adapter_1in@165,160!flip")
w(parts, "A2L - Pole adapter threaded (black PETG)", "adapter_thread@165,160!flip")
w(nuts, "A2L - Pole thread test nuts (black)", "nut_snug@145,160", "nut_loose@185,160")
run(os.path.join(HERE, "build_adapter_thread_v1b.py"), os.path.join(OUT, "A2L - Pole adapter threaded v1b (black PETG).3mf"))
print("v1 print files in", OUT)
