#!/usr/bin/env python3
"""Rebuild the totem print files (and optionally the preview renders).

    python3 build.py              # everything: v1, v2, stand -> out/<set>/
    python3 build.py v2           # just one set (v1, v2 or stand)
    python3 build.py v1 --renders # also draw the PNG previews into out/<set>/renders/
    python3 build.py v1 POLE_D=25.4   # pass size overrides to that set's build script

Output goes to totem_3d/out/, which git ignores. Slice and tune in Bambu Studio, then put the
final 3MFs you actually print in releases/ (see releases/README.md)."""
import sys, os, subprocess, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
args = sys.argv[1:]
renders = "--renders" in args
overrides = [a for a in args if "=" in a]
sets = [a for a in args if a in ("v1", "v2", "stand")] or ["v1", "v2", "stand"]
if overrides and len(sets) != 1:
    sys.exit("Size overrides (key=value) need exactly one set, e.g.: python3 build.py v1 POLE_D=25.4")
if overrides and sets == ["v2"]:
    sys.exit("v2 takes no key=value overrides: its sizes are constants near the top of v2/build_round.py")


def run(*cmd):
    print("$", os.path.relpath(cmd[0], HERE), *cmd[1:]); subprocess.run([PY, *cmd], check=True)


def src(*p):
    return os.path.join(HERE, *p)


for s in sets:
    t0 = time.time()
    out = src("out", s); os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp()
    if s == "v1":
        run(src("v1", "export_v1.py"), out, *overrides)
        if renders:
            npz = os.path.join(tmp, "parts.npz"); run(src("v1", "build_modular.py"), npz, *overrides)
            os.makedirs(os.path.join(out, "renders"), exist_ok=True)
            run(src("v1", "render_modular.py"), npz, os.path.join(out, "renders"))
    elif s == "v2":
        npz = os.path.join(out, "round.npz")
        run(src("v2", "build_round.py"), npz, *overrides)
        run(src("v2", "export_round.py"), npz, out)
        if renders:
            os.makedirs(os.path.join(out, "renders"), exist_ok=True)
            run(src("v2", "render_round.py"), npz, os.path.join(out, "renders"))
    else:
        npz = os.path.join(out, "stand_parts.npz")
        run(src("stand", "build_stand.py"), npz, *overrides)
        run(src("stand", "export_stand.py"), npz, out)
        if renders:
            os.makedirs(os.path.join(out, "renders"), exist_ok=True)
            run(src("stand", "render_stand.py"), npz, os.path.join(out, "renders"))
    print(f"== {s} done in {time.time() - t0:.0f} s -> {os.path.relpath(out, HERE)}/\n")
