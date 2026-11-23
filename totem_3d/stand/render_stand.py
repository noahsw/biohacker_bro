"""Renders of the home stand. Usage: python3 render_stand.py PARTS_NPZ OUT_DIR"""
import sys, os, numpy as np, manifold3d as m
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
from render3d import Camera, render, autocrop, Annot
P = np.load(sys.argv[1]); OUT = sys.argv[2]
R_OUT, H_TRAY, Z_LID, FLOOR, R_BORE, Z_TOP, POLE_D, RIB_D = P["params"]
BLACK, GREY, SILVER, SAND, WHITE = (58, 58, 63), (90, 90, 96), (190, 192, 198), (214, 188, 140), (236, 236, 230)
def man(k): return m.Manifold(m.Mesh(vert_properties=P[k + "_v"].astype(np.float32), tri_verts=P[k + "_t"].astype(np.uint32)))
def tomesh(mf):
    mm = mf.to_mesh(); return np.asarray(mm.vert_properties)[:, :3].astype(float), np.asarray(mm.tri_verts)
def part(mf, rgb, off=(0, 0, 0), shine=0.0):
    V, T = tomesh(mf); return dict(V=V + np.array(off), T=T, rgb=rgb, closed=True, shine=shine)
def sector(r0, r1, a0, a1, z0, z1, n=48):
    ts = np.radians(np.linspace(a0, a1, n))
    pts = [(r1 * np.cos(t), r1 * np.sin(t)) for t in ts] + [(r0 * np.cos(t), r0 * np.sin(t)) for t in ts[::-1]]
    return m.Manifold.extrude(m.CrossSection([pts]), z1 - z0).translate([0, 0, z0])
tray, lid, tower, bush = (man(k) for k in ("tray", "lid", "tower", "bushing"))
YAW = -60
cut = sector(0, 400, YAW - 45, YAW + 45, -5, 400)
pole = m.Manifold.cylinder(330, POLE_D / 2, POLE_D / 2, 64).translate([0, 0, FLOOR])
bags = m.Manifold()
for i in range(8):
    a0 = 22.5 + 45 * i
    bags += sector(52, 143, a0 + 3, a0 + 42, FLOOR + 0.5, Z_LID - 6, 24)

# ---------- cutaway ----------
cam = Camera(YAW, 24, 1500, target=(0, 0, 150), fov=22, W=1600, H=1500)
sc = [part(tray - cut, BLACK), part(lid - cut, GREY), part(tower - cut, BLACK), part(bush - cut, (70, 70, 76)),
      part(bags - cut, SAND), part(pole - cut.translate([0, 0, 0]) if False else pole, SILVER, shine=0.5)]
img = autocrop(render(sc, cam)); cam2 = cam
img = render(sc, cam); a = Annot(img, cam)
pa = lambda r, ang, z: (r * np.cos(np.radians(ang)), r * np.sin(np.radians(ang)), z)
a.label(pa(POLE_D / 2, YAW, 300), (1100, 170), "Pole", "bottom section, foot down")
a.label(pa(R_BORE - 2, YAW + 15, 170), (1100, 420), "Bushing", "crush ribs grip the pole")
a.label(pa(30, YAW + 70, 140), (230, 330), "Socket tower", "4 M3x16 into nuts", anchor="r")
a.label(pa(100, YAW - 25, Z_LID - 15), (230, 760), "Ballast", "8 bays, ~2 L of sand", anchor="r")
a.label(pa(120, YAW + 75, H_TRAY), (1200, 700), "Lid", "held by the tower")
a.label(pa(R_OUT, YAW + 60, 12), (1150, 1250), "Tray", "300 mm across, felt pads under")
autocrop(img).save(os.path.join(OUT, "Stand cutaway.png"))

# ---------- exploded ----------
cam = Camera(YAW, 22, 1800, target=(0, 0, 230), fov=24, W=1600, H=1800)
dz = dict(lid=70, tower=150, bush=330)
sc = [part(tray, BLACK), part(lid, GREY, (0, 0, dz["lid"])), part(tower, BLACK, (0, 0, dz["tower"])),
      part(bush, (70, 70, 76), (0, 0, dz["bush"])), part(bags, SAND)]
img = render(sc, cam); a = Annot(img, cam)
a.label(pa(0, 0, Z_TOP + dz["bush"] + 2), (1150, 200), "4  Bushing", "drops into the tower")
a.label(pa(56, YAW + 170, H_TRAY + dz["tower"] + 6), (1150, 560), "3  Tower", "4 M3x16 down through lid")
a.label(pa(140, YAW + 200, Z_LID + 3 + dz["lid"]), (1150, 950), "2  Lid", "")
a.label(pa(R_OUT, YAW - 30, 15), (250, 1250), "1  Tray + ballast", "nuts drop in the hub", anchor="r")
autocrop(img).save(os.path.join(OUT, "Stand exploded.png"))

# ---------- whole totem on the stand, for scale ----------
L_POLE = 846.0
ptall = m.Manifold.cylinder(L_POLE, POLE_D / 2, 9.6, 48).translate([0, 0, FLOOR])
head = m.Manifold.cylinder(174, 110, 110, 128).translate([0, 0, FLOOR + L_POLE + 15])
cam = Camera(YAW, 12, 4200, target=(0, 0, 520), fov=18, W=1000, H=1500)
sc = [part(tray, BLACK), part(lid, GREY), part(tower, BLACK), part(ptall, SILVER, shine=0.5), part(head, WHITE)]
img = render(sc, cam); a = Annot(img, cam)
a.label((0, 0, FLOOR + L_POLE + 15 + 87), (760, 160), "Totem", "~1.7 kg up here")
a.label((0, 0, 300), (760, 700), "Pole", "collapsed, ~33 in")
a.label((R_OUT * 0.5, -R_OUT * 0.8, 20), (760, 1300), "Stand", "11.8 in wide")
autocrop(img).save(os.path.join(OUT, "Stand with totem.png"))
print("ok")
