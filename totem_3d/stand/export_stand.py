"""Write the home stand print files for the Bambu A2L (bed 330 x 320).
Usage: python3 export_stand.py PARTS_NPZ OUT_DIR   (PARTS_NPZ from build_stand.py)"""
import sys, os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
WRITER = os.path.join(HERE, "..", "common", "write_plain_3mf.py")
NPZ, OUT = sys.argv[1:3]
os.makedirs(OUT, exist_ok=True)


def w(name, *items):
    subprocess.run([sys.executable, WRITER, NPZ, os.path.join(OUT, name + ".3mf"), "330,320", *items], check=True)


w("A2L - Foot cap fit test rings (black, print first)",
  *[f"fit{i + 1}@{x},140" for i, x in enumerate((100, 145, 190, 235))],
  *[f"fit{i + 5}@{x},190" for i, x in enumerate((122, 167, 212))])
w("A2L - Stand tray (black)", "tray@165,160")
w("A2L - Stand lid (black)", "lid@165,160")
w("A2L - Stand tower and bushing (black)", "tower@110,160", "bushing@230,160!flip")
print("stand print files in", OUT)
