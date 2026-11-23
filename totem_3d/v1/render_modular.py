"""Assembly cutaway and exploded view of the modular 4-panel totem. Usage: python3 render_modular.py PARTS_NPZ OUT_DIR"""
import sys, os, numpy as np, manifold3d as m
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
from render3d import Camera, render, autocrop, Annot
from PIL import Image
P = np.load(sys.argv[1]); OUT = sys.argv[2]
R_BACK, R_GI, R_GO, R_SI, R_SO, R_CO, H, BORDER, PW, PH, EL, EB, R_MID, BASE_T = P["params"]
BLACK, WHITE, PANEL, LED = (58, 58, 63), (236, 236, 230), (30, 30, 34), (255, 70, 70)

def man(k):
    return m.Manifold(m.Mesh(vert_properties=P[k + "_v"].astype(np.float32), tri_verts=P[k + "_t"].astype(np.uint32)))
def tomesh(mf):
    mm = mf.to_mesh(); return np.asarray(mm.vert_properties)[:, :3].astype(float), np.asarray(mm.tri_verts)
def part(mf, rgb, off=(0, 0, 0), shine=0.0):
    V, T = tomesh(mf); return dict(V=V + off, T=T, rgb=rgb, closed=True, shine=shine)
def sector(r0, r1, a0, a1, z0, z1, n=48):
    ts = np.radians(np.linspace(a0, a1, n))
    pts = [(r1 * np.cos(t), r1 * np.sin(t)) for t in ts] + [(r0 * np.cos(t), r0 * np.sin(t)) for t in ts[::-1]]
    return m.Manifold.extrude(m.CrossSection([pts]), z1 - z0).translate([0, 0, z0])

def panel(q):
    a0 = 90 * q; da = np.degrees(PW / R_MID)
    sheet = sector(R_BACK, R_GI, a0 + 0.05, a0 + da - 0.05, BORDER, BORDER + PH, 64)
    leds = m.Manifold()
    for i in range(16):
        a = np.radians(a0 + np.degrees((EL + 10 * i) / R_MID))
        for j in range(16):
            z = BORDER + EB + 10 * j
            b = m.Manifold.cube([1.0, 2.2, 2.2], True).translate([R_GI + 0.5, 0, 0]).rotate([0, 0, np.degrees(a)]).translate([0, 0, z])
            leds += b
    return sheet, leds

parts = {k: man(k) for k in ("base", "core", "cap", "skin", "grid", "adapter", "cradle537", "board_asm")}
grid = [parts["grid"].rotate([0, 0, 90 * q]) for q in range(4)]
pole = m.Manifold.cylinder(260, 9.6, 9.6, 48).translate([0, 0, -BASE_T - 260])
battery = m.Manifold.cube([26.4, 160, 84.9]).translate([-13.2, -80, 11.6])   # Anker 537 on its long edge
board = m.Manifold.cube([1.6, 66, 58]).translate([-57.5 + 1.5 + 12, -33, 17])
pnl = [panel(q) for q in range(4)]

# ---------- assembly cutaway ----------
YAW = -55
cut = sector(0, 400, YAW - 50 + 180, YAW + 40 + 180, -1, H + 1)      # window facing the camera
cutg = sector(0, 400, YAW - 50 + 180, YAW + 40 + 180, H * 0.55, H + 1)
sc = []
sc.append(part(parts["skin"] - cut, WHITE))
for g in grid: sc.append(part(g - cutg, BLACK))
for sheet, leds in pnl:
    sc.append(part(sheet, PANEL)); sc.append(part(leds, LED, shine=0.3))
sc += [part(parts["core"], BLACK), part(parts["base"], BLACK), part(parts["adapter"], BLACK), part(pole, (150, 150, 155), shine=0.4)]
sc.append(part(parts["cap"].translate([0, 0, 120]), BLACK))
cam = Camera(YAW + 180, 18, 1350, target=(0, 0, 60), W=1500, H=1700)
img = render(sc, cam)
a = Annot(img, cam)
def pa(r, ang, z): return (r * np.cos(np.radians(ang)), r * np.sin(np.radians(ang)), z)
a.label(pa(R_SO, 178, 110), (1180, 420), "White skin", "one tube")
a.label(pa(R_GO, 140, 40), (120, 950), "Grid quarter", "1 of 4, one per panel", anchor="r")
a.label(pa(R_GI, 140, 140), (120, 560), "Panel", "LEDs in the cells", anchor="r")
a.label((0, -110, 293), (1150, 170), "Twist-off cap", "lifted to show")
a.label((0, 0, -60), (1150, 1350), "Pole adapter", "bolts on, one per pole")
img = autocrop(img); img.save(os.path.join(OUT, "modular-assembly.png"))

# ---------- exploded ----------
YAW2 = -90
sc = []
sc.append(part(parts["adapter"], BLACK, (0, 0, -150)))
sc.append(part(pole.translate([0, 0, -150]), (150, 150, 155), shine=0.4))
sc.append(part(parts["base"], BLACK))
sc.append(part(parts["cradle537"], BLACK, (0, 0, 40))); sc.append(part(battery, (70, 110, 170), (0, 0, 40)))
sc.append(part(parts["board_asm"], BLACK, (0, 0, 40))); sc.append(part(board, (40, 140, 90), (0, 0, 40)))
CZ = 230
sc.append(part(parts["core"], BLACK, (0, 0, CZ)))
for q in range(4):
    sheet, leds = pnl[q]
    ang = np.radians(90 * q + 45); d = 85
    off = (d * np.cos(ang), d * np.sin(ang), CZ)
    sc.append(part(sheet, PANEL, off)); sc.append(part(leds, LED, off, shine=0.3))
    off2 = (2.1 * d * np.cos(ang), 2.1 * d * np.sin(ang), CZ)
    sc.append(part(grid[q], BLACK, off2))
sc.append(part(parts["skin"] - sector(0, 400, YAW2 + 120, YAW2 + 240, -1, H + 1), WHITE, (0, 0, CZ + 260)))
sc.append(part(parts["cap"], BLACK, (0, 0, CZ + 340)))
cam = Camera(YAW2 + 180, 20, 2600, target=(0, 0, 250), W=1700, H=2400)
img = render(sc, cam)
a = Annot(img, cam)
def lab(X, xy, t, s=None, anc="l"): a.label(X, xy, t, s, anchor=anc)
lab((0, 0, CZ + 340 + H + 2), (1250, 230), "Cap", "twist off for the battery")
lab((0, R_SO, CZ + 260 + H * 0.8), (1250, 520), "Skin (white)", "slides down over the grid")
lab((0, 0, -150 - 40), (1250, 2120), "Pole adapter", "4 screws, swap per pole")
lab((-100, 0, -1), (100, 1900), "Base", "pegs for the grid quarters", "r")
lab((15, 70, 90), (100, 1600), "Anker cradle", "pack on its edge, centred", "r")
lab((-57, 40, 90), (1300, 1750), "Board plate", "antenna edge down")
lab((R_CO * np.cos(np.radians(95)), R_CO * np.sin(np.radians(95)), CZ + H - 5), (100, 900), "Core", "wire windows, bayonet collar", "r")
ang = np.radians(135); dd = 2.1 * 85
lab((dd * np.cos(ang) + R_GO * np.cos(ang), dd * np.sin(ang) + R_GO * np.sin(ang), CZ + 100), (1350, 1150), "Grid quarters x4", "black, one per panel")
ang = np.radians(45); dd = 85
lab((dd * np.cos(ang) + R_GI * np.cos(ang), dd * np.sin(ang) + R_GI * np.sin(ang), CZ + 60), (100, 1300), "Panels", "LEDs out, edges touching", "r")
img = autocrop(img); img.save(os.path.join(OUT, "modular-exploded.png"))
print("ok")
