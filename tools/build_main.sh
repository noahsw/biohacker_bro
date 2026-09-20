#!/usr/bin/env bash
# Build (and optionally flash) the main biohacker_bro sketch from anywhere,
# including a git worktree.
#
# Why this exists: arduino-cli requires a sketch's folder to be named after its
# .ino, so `biohacker_bro.ino` only builds from a folder called
# `biohacker_bro`. In the main checkout that's true by accident. In a git
# worktree the folder is named after the branch, so the main firmware cannot
# be built at all -- `arduino-cli compile .` fails with "main file missing
# from sketch". The test sketches are unaffected because each already lives in
# a correctly-named subdirectory.
#
# This assembles a correctly-named directory of symlinks in a temp dir and
# builds there, so nothing is copied and edits never drift out of sync.
#
# Usage:
#   tools/build_main.sh                 # compile only
#   tools/build_main.sh /dev/cu.usbmodemXXXX   # compile then upload
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-}"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"

BUILD_ROOT="$(mktemp -d)"
trap 'rm -rf "$BUILD_ROOT"' EXIT
SKETCH="$BUILD_ROOT/biohacker_bro"
mkdir -p "$SKETCH"

# Symlink, don't copy: a stale copy that still compiles is worse than no build.
# secrets.h is itself a symlink out of the repo; -L resolves it so the
# preprocessor finds a real file next to config.h.
for f in "$REPO"/*.ino "$REPO"/*.h "$REPO"/*.cpp; do
  [ -e "$f" ] || continue
  ln -sfn "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" "$SKETCH/$(basename "$f")"
done

if [ ! -r "$SKETCH/secrets.h" ]; then
  echo "error: secrets.h is missing or unreadable." >&2
  echo "       Copy secrets.h.example to secrets.h and fill in the Whoop MAC." >&2
  exit 1
fi

echo "Building biohacker_bro from $REPO"
arduino-cli compile -b "$FQBN" "$SKETCH"

if [ -n "$PORT" ]; then
  echo "Uploading to $PORT"
  arduino-cli upload -b "$FQBN" -p "$PORT" "$SKETCH"
fi
