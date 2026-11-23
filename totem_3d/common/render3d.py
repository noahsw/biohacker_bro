"""Tiny painter's-algorithm renderer for assembly-guide figures (numpy + Pillow).
Parts are (verts Nx3, tris Mx3, rgb, closed). Closed meshes get back-face culling."""
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT_B = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
FONT_R = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"


def load_font(path, size):
    """Label font for renders. Inter if it's installed, else DejaVu, else Pillow's built-in font,
    so renders work on a Mac or Windows box without extra fonts (labels just look a little different)."""
    for p in (path, path.replace("Inter-Bold.otf", "Inter-SemiBold.otf"), "DejaVuSans-Bold.ttf" if "Bold" in path else "DejaVuSans.ttf",
              "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if "Bold" in path else "/System/Library/Fonts/Supplemental/Arial.ttf",
              "arialbd.ttf" if "Bold" in path else "arial.ttf"):
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            pass
    return ImageFont.load_default(size)

class Camera:
    def __init__(self, yaw, elev, dist, target=(0, 0, 80), fov=30, W=1600, H=1600, scale=1.0, ortho=False):
        y, e = np.radians(yaw), np.radians(elev)
        self.t = np.array(target, float)
        f = -np.array([np.cos(e) * np.cos(y), np.cos(e) * np.sin(y), np.sin(e)])   # looking toward target
        self.pos = self.t - f * dist
        up = np.array([0, 0, 1.0]) if abs(f[2]) < 0.99 else np.array([np.cos(y), np.sin(y), 0.0])
        r = np.cross(f, up); r /= np.linalg.norm(r)
        u = np.cross(r, f)
        self.R = np.stack([r, u, f])
        self.W, self.H = W, H
        self.fpx = scale * (H / 2) / np.tan(np.radians(fov) / 2)
        self.ortho, self.dist = ortho, dist

    def cam(self, X):
        return (np.asarray(X, float) - self.pos) @ self.R.T

    def proj(self, X):
        C = self.cam(X)
        d = C[..., 2] if not self.ortho else np.full(C.shape[:-1], self.dist)
        x = self.W / 2 + self.fpx * C[..., 0] / d
        y = self.H / 2 - self.fpx * C[..., 1] / d
        return np.stack([x, y], -1)

def cyl_mesh(r, z0, z1, seg=64, caps=True, a0=0.0, a1=2 * np.pi, center=(0, 0)):
    a = np.linspace(a0, a1, seg + 1)
    full = abs(a1 - a0 - 2 * np.pi) < 1e-6
    V, T = [], []
    for ang in a:
        V += [[center[0] + r * np.cos(ang), center[1] + r * np.sin(ang), z0], [center[0] + r * np.cos(ang), center[1] + r * np.sin(ang), z1]]
    for i in range(seg):
        p, q = 2 * i, 2 * i + 2
        T += [[p, q, q + 1], [p, q + 1, p + 1]]
    if caps and full:
        cb, ct = len(V), len(V) + 1
        V += [[center[0], center[1], z0], [center[0], center[1], z1]]
        for i in range(seg):
            T += [[cb, 2 * i + 2, 2 * i], [ct, 2 * i + 1, 2 * i + 3]]
    return np.array(V, float), np.array(T, int)

def box_mesh(x0, x1, y0, y1, z0, z1):
    V = np.array([[x, y, z] for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)], float)
    T = np.array([[0, 2, 3], [0, 3, 1], [4, 5, 7], [4, 7, 6], [0, 1, 5], [0, 5, 4],
                  [2, 6, 7], [2, 7, 3], [0, 4, 6], [0, 6, 2], [1, 3, 7], [1, 7, 5]], int)
    return V, T

def rotz(V, deg, about=(0, 0)):
    a = np.radians(deg); c, s = np.cos(a), np.sin(a)
    V = V.copy(); x, y = V[:, 0] - about[0], V[:, 1] - about[1]
    V[:, 0], V[:, 1] = about[0] + c * x - s * y, about[1] + s * x + c * y
    return V

def render(parts, cam, bg=(255, 255, 255), amb=0.42, ss=2):
    """Z-buffer rasterizer. parts: list of dicts {V, T, rgb, closed, shine, bias}."""
    r, u, f = cam.R
    L = -0.55 * f + 0.65 * u - 0.5 * r; L /= np.linalg.norm(L)       # light from upper left, in front
    W, H = cam.W * ss, cam.H * ss
    zb = np.full((H, W), np.inf); img = np.zeros((H, W, 3)); img[:] = bg
    for p in parts:
        V, T = p["V"], p["T"]
        if len(T) == 0: continue
        P = V[T]
        n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
        nl = np.linalg.norm(n, axis=1); ok = nl > 1e-9
        P, n = P[ok], n[ok] / nl[ok, None]
        facing = (n * (P.mean(1) - cam.pos)).sum(1)
        if p.get("closed", True):
            keep = facing < 0; P, n = P[keep], n[keep]
        else:
            n = np.where(facing[:, None] > 0, -n, n)
        lam = np.clip(n @ L, 0, 1)
        k = amb + (1 - amb) * lam
        col = np.clip(np.array(p["rgb"], float)[None] * k[:, None] + p.get("shine", 0.0) * lam[:, None] ** 12 * 255, 0, 255)
        S = cam.proj(P.reshape(-1, 3)).reshape(-1, 3, 2) * ss
        Z = cam.cam(P.reshape(-1, 3))[:, 2].reshape(-1, 3) + p.get("bias", 0.0)
        for i in range(len(S)):
            s = S[i]
            x0, x1 = int(max(np.floor(s[:, 0].min()), 0)), int(min(np.ceil(s[:, 0].max()), W - 1))
            y0, y1 = int(max(np.floor(s[:, 1].min()), 0)), int(min(np.ceil(s[:, 1].max()), H - 1))
            if x1 < x0 or y1 < y0: continue
            xs = np.arange(x0, x1 + 1) + 0.5; ys = np.arange(y0, y1 + 1) + 0.5
            X, Y = np.meshgrid(xs, ys)
            (ax, ay), (bx, by), (cx, cy) = s
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9: continue
            w0 = ((by - cy) * (X - cx) + (cx - bx) * (Y - cy)) / den
            w1 = ((cy - ay) * (X - cx) + (ax - cx) * (Y - cy)) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
            if not m.any(): continue
            z = 1.0 / (w0 / Z[i, 0] + w1 / Z[i, 1] + w2 / Z[i, 2])   # perspective-correct
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            m &= z < sub
            sub[m] = z[m]
            img[y0:y1 + 1, x0:x1 + 1][m] = col[i]
    out = Image.fromarray(img.astype(np.uint8))
    return out.resize((cam.W, cam.H), Image.LANCZOS)

def autocrop(img, margin=30, bg=(255, 255, 255)):
    a = np.asarray(img.convert("RGB")).astype(int)
    m = (np.abs(a - np.array(bg)).sum(2) > 12)
    ys, xs = np.where(m)
    if len(xs) == 0: return img
    return img.crop((max(xs.min() - margin, 0), max(ys.min() - margin, 0), min(xs.max() + margin, img.width), min(ys.max() + margin, img.height)))

class Annot:
    """Labels with leader lines, drawn after render at final resolution."""
    def __init__(self, img, cam):
        self.img, self.cam = img, cam
        self.d = ImageDraw.Draw(img)
        self.fb = load_font(FONT_B, 34)
        self.fr = load_font(FONT_R, 27)

    def pt(self, X):
        return tuple(self.cam.proj(np.array(X, float)))

    def label(self, X, text_xy, title, sub=None, color=(20, 20, 20), dot=True, anchor="l"):
        p = self.pt(X) if len(X) == 3 else tuple(X)
        tx, ty = text_xy
        w = max(self.d.textlength(title, font=self.fb), self.d.textlength(sub or "", font=self.fr))
        if anchor == "l" and tx + 10 + w > self.img.width - 15:
            tx = self.img.width - 25 - w
        if anchor == "r" and tx - 10 - w < 15:
            tx = 25 + w
        self.d.line([p, (tx, ty)], fill=(120, 120, 120), width=3)
        if dot:
            self.d.ellipse([p[0] - 8, p[1] - 8, p[0] + 8, p[1] + 8], fill=(223, 234, 0), outline=(20, 20, 20), width=3)
        ox = 10 if anchor == "l" else -10
        a = "l" if anchor == "l" else "r"
        if sub:
            self.d.text((tx + ox, ty - 4), title, font=self.fb, fill=color, anchor=a + "s")
            self.d.text((tx + ox, ty + 6), sub, font=self.fr, fill=(90, 90, 90), anchor=a + "t")
        else:
            self.d.text((tx + ox, ty), title, font=self.fb, fill=color, anchor=a + "m")

    def arrow(self, X0, X1, color=(214, 40, 90), width=8):
        a, b = np.array(self.pt(X0)), np.array(self.pt(X1))
        self._arrow2d(a, b, color, width)

    def _arrow2d(self, a, b, color, width):
        self.d.line([tuple(a), tuple(b)], fill=color, width=width)
        v = b - a; v /= (np.linalg.norm(v) + 1e-9); nrm = np.array([-v[1], v[0]])
        h = 30
        self.d.polygon([tuple(b + v * 6), tuple(b - v * h + nrm * h * 0.55), tuple(b - v * h - nrm * h * 0.55)], fill=color)

    def polyarrow(self, Xs, color=(214, 40, 90), width=7):
        P = [self.pt(X) for X in Xs]
        self.d.line(P, fill=color, width=width, joint="curve")
        self._arrow2d(np.array(P[-2]), np.array(P[-1]), color, width)

    def text(self, xy, s, big=False, color=(20, 20, 20), anchor="la"):
        self.d.text(xy, s, font=self.fb if big else self.fr, fill=color, anchor=anchor)
