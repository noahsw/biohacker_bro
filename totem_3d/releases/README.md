# Final print files

This is the one place 3MFs are committed. Everything else is rebuilt by `../build.py` and ignored by git.

Put the 3MFs you actually printed here, after slicing and tuning them in Bambu Studio and
saving them with their settings (walls, infill, supports, filament). One folder per version:

```
releases/
  v1/   Dreamstate 2026: 4-panel modular ring
  v2/   6-panel round drum
```

Name them like the generated files (`A2L - Core (black).3mf`) so it's clear which script
made each one. When a file changes, commit the new one over the old; git keeps the history.

If this folder grows past a few tens of MB, attach the files to a GitHub Release
(tag `totem-v1`, `totem-v2`) instead and keep a note here saying where they are.
