#!/bin/bash
# The Windows version (x86-64): dist/windows/Raptor Sprint, and a zip of it. Builds in a container, so
# it needs Docker. Unzip anywhere that can be written to: saves go next to the program.
# Usage: scripts/package-windows.sh [VERSION]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=${1:-$(sed -n 's/.*versionName="\([^"]*\)".*/\1/p' "$ROOT/android/AndroidManifest.xml")}
IMAGE=raptorsprint-mingw
docker image inspect "$IMAGE" > /dev/null 2>&1 || docker build -q -t "$IMAGE" "$ROOT/packaging/windows" > /dev/null
docker run --rm -v "$ROOT":/src "$IMAGE" scripts/build.sh windows
OUT="$ROOT/dist/windows"
GAME="$OUT/Raptor Sprint"
rm -rf "$OUT"
mkdir -p "$GAME/licenses"
cp "$ROOT/build/windows/raptorsprint.exe" "$GAME/Raptor Sprint.exe"
cp -R "$ROOT/assets" "$GAME/assets"
cp "$ROOT/LICENSE" "$ROOT/LICENSE-ASSETS" "$GAME/licenses/"
cp "$ROOT/third_party/raylib/LICENSE" "$GAME/licenses/raylib.txt"
find "$OUT" -name .DS_Store -delete
(cd "$OUT" && zip -qry "raptor-sprint-$VERSION-windows.zip" "Raptor Sprint")
echo "built $GAME"
