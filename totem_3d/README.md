# Totem 3D print files

Source for every printed part of the heart rate totem (the firmware is in `../totem/`).
The Python scripts here are the real source; the 3MFs are generated from them, so a design
change is a code change and a rebuild, never a hand edit in the slicer.

- **Printer:** Bambu Lab A2L only (bed 330 x 320 mm). Every file is laid out for that bed.
- **Slicer:** Bambu Studio. The generated 3MFs are plain geometry; Bambu Studio fills in its
  own printer and filament settings when you open them.
- **Final files** you tuned and printed go in [`releases/`](releases/README.md). That's the
  only place 3MFs are committed.

## Setup (once)

Python 3.10 or newer.

```
cd totem_3d
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
```

`manifold3d` is the geometry kernel (all the booleans); `numpy` holds the meshes; `pillow`
is only used for the preview renders.

## Rebuild

```
python3 build.py                 # all three sets: v1, v2, stand  (about a minute)
python3 build.py v1              # one set
python3 build.py v2 --renders    # also draw PNG previews into out/v2/renders/
python3 build.py v1 POLE_D=25.4  # pass size overrides to that set's build script
```

Output lands in `out/v1/`, `out/v2/`, `out/stand/` (git ignores `out/`). Each run rebuilds
every file in the set from scratch, with the same names as the files you've been printing.

## What makes what

Each set is a `build_*.py` that makes all the parts as one `.npz` of meshes (in assembled
position), and an `export_*.py` that turns each part to its print orientation and writes the
A2L plates with `common/write_plain_3mf.py`.

### v1: 4-panel modular ring (Dreamstate 2026)

`v1/build_modular.py` makes the parts; `v1/export_v1.py OUT_DIR` writes all 14 files.

| File | Part key(s) | Made by |
|---|---|---|
| `A2L - Base (black)` | `base` | build_modular.py |
| `A2L - Core (black)` | `core` | build_modular.py |
| `A2L - Cap (black)` | `cap` (upside down) | build_modular.py |
| `A2L - Grid quarter fit test (black, print 1 first)` | `grid` | build_modular.py |
| `A2L - Grid quarters x4 (black)` | `grid` x4 | build_modular.py |
| `A2L - Skin 0.6 mm (white, spiral vase)` | `skin` | build_modular.py |
| `A2L - Skin 0.84 mm fallback (white)` | `skin` | build_modular.py `SKIN_T=0.84` |
| `A2L - Battery cradle Anker 537 (black)` | `cradle537` | build_modular.py |
| `A2L - Board plate and spacers (black)` | `board`, `spacers` | build_modular.py |
| `A2L - Small parts (black) - REDCAMP adapter, cradle, board plate` | `adapter`, `cradle`, `board`, `spacers` | build_modular.py |
| `A2L - Pole adapter 1 inch (black)` | `adapter_1in` | build_modular.py |
| `A2L - Pole adapter threaded (black PETG)` | `adapter_thread` (original, replaced by v1b) | build_modular.py |
| `A2L - Pole adapter threaded v1b (black PETG)` | | build_adapter_thread_v1b.py |
| `A2L - Pole thread test nuts (black)` | `nut_snug`, `nut_loose` | build_thread_test.py |

Sizes you can override (`key=value`, defaults in the `P = dict(...)` at the top of
`build_modular.py`): panel edges `EDGE_L/R/B/T`, `FACE_T`, `SKIN_T`, board hole spacing
`HOLE_X`/`HOLE_Y`, `POLE_D`, `PIN_D`, `BAT_W`, `BAT_T`, mesh resolution `SEG`.
`v1/render_modular.py` draws the assembly and exploded previews.

### v2: 6-panel round drum

`v2/build_round.py` makes the parts; `v2/export_round.py PARTS_NPZ OUT_DIR` writes all 11
files (fit tests 1 and 2, spine, thumb nut spare, base/core/cap halves, grid sectors, skin
thirds). Sizes are constants near the top of `build_round.py`, not command-line overrides.
`v2/render_round.py` draws the previews.

### Home stand

`stand/build_stand.py` makes the parts (overrides: `POLE_D`, `RIB_D`, `CAP_RIB`, `R_OUT`,
`H_TRAY`, `TOWER_H`; set `CAP_RIB` from the fit ring the pole's foot cap fits, or `0` for no
cap); `stand/export_stand.py PARTS_NPZ OUT_DIR` writes the 4 files (fit rings, tray, lid,
tower + bushing). `stand/render_stand.py` draws the previews.

### Running one script by hand

From `totem_3d/`:

```
python3 v1/build_modular.py out/parts.npz POLE_D=25.4
python3 common/write_plain_3mf.py out/parts.npz out/adapter.3mf 330,320 'adapter!flip'
```

`write_plain_3mf.py` takes `key[@x,y[,rotation]][!flip]` per object: `@x,y` is where its
centre sits on the bed, `!flip` prints it upside down. It refuses to write a part that hangs
off the bed.

## Print settings

The settings per part (walls, infill, PETG parts, vase mode for the skin, brims on the grid
quarters) are in each version's print notes and in the build guide; they live in the 3MFs
you save from Bambu Studio, which is why those go in `releases/`.

## Checked

On a clean copy with a fresh venv, `python3 build.py` rebuilt all 29 files (14 v1, 11 v2,
4 stand), and each one's geometry is identical to the file it replaces.

## Not here

The build guide PDF, wiring diagrams, show/animation previews and the retired 3-panel ring
designs are made by other scripts that aren't needed to make print files; they stay out of
the repo to keep it to what rebuilds the parts.
