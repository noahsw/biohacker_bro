"""Plain 3MF (Bambu Studio opens it with its own printer and filament settings).
Usage: python3 write_plain_3mf.py PARTS_NPZ OUT.3mf BED_X,BED_Y key[@x,y[,rotdeg][!flip]] ...
Each key becomes one object, dropped to the bed; @x,y is its centre on the plate (default: bed centre)."""
import sys, zipfile, numpy as np
npz, out, bed = sys.argv[1], sys.argv[2], [float(v) for v in sys.argv[3].split(",")]
P = np.load(npz)
objs, items = [], []
for i, spec in enumerate(sys.argv[4:], start=1):
    flip = spec.endswith("!flip"); spec = spec.replace("!flip", "")
    key, _, pos = spec.partition("@")
    v, t = P[key + "_v"].astype(float), P[key + "_t"]
    if flip:
        v = v * [1, -1, -1]
    rot = 0.0
    if pos:
        vals = [float(x) for x in pos.split(",")]
        cx, cy = vals[0], vals[1]; rot = vals[2] if len(vals) > 2 else 0.0
    else:
        cx, cy = bed[0] / 2, bed[1] / 2
    a = np.radians(rot); c, s = np.cos(a), np.sin(a)
    v = v @ np.array([[c, s, 0], [-s, c, 0], [0, 0, 1]])
    v[:, 2] -= v[:, 2].min()
    v[:, :2] -= (v[:, :2].max(0) + v[:, :2].min(0)) / 2
    mn, mx = v.min(0) + [cx, cy, 0], v.max(0) + [cx, cy, 0]
    assert mn[0] >= 0 and mn[1] >= 0 and mx[0] <= bed[0] and mx[1] <= bed[1], f"{key} off the bed: {mn} {mx}"
    name = key
    L = [f'  <object id="{i}" name="{name}" type="model">', '   <mesh>', '    <vertices>']
    L += [f'     <vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in v]
    L += ['    </vertices>', '    <triangles>']
    L += [f'     <triangle v1="{p}" v2="{q}" v3="{r}"/>' for p, q, r in t]
    L += ['    </triangles>', '   </mesh>', '  </object>']
    objs += L
    items.append(f'  <item objectid="{i}" transform="1 0 0 0 1 0 0 0 1 {cx:.3f} {cy:.3f} 0"/>')
    print(f"  {key}: {np.round(v.max(0)-v.min(0),1)} at {cx:g},{cy:g}")
doc = ['<?xml version="1.0" encoding="UTF-8"?>',
       '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
       ' <resources>'] + objs + [' </resources>', ' <build>'] + items + [' </build>', '</model>']
ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
      '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", ct); z.writestr("_rels/.rels", rels); z.writestr("3D/3dmodel.model", "\n".join(doc) + "\n")
print("wrote", out)
