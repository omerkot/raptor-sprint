#!/bin/bash
# The files for a release, in dist/release: a zip for each handheld system and the Mac, and the Android app.
# Usage: scripts/package-release.sh 1.0.0
# (Builds for the handhelds too, so set CROSS if you need it: see scripts/build.sh.)
set -e
VERSION=${1:?usage: scripts/package-release.sh VERSION}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/release"
rm -rf "$OUT"
mkdir -p "$OUT"
"$ROOT/scripts/package-rocknix.sh"
"$ROOT/scripts/package-muos.sh"
"$ROOT/scripts/build.sh" mac

licences() {
	mkdir -p "$1/licenses"
	cp "$ROOT/LICENSE" "$ROOT/LICENSE-ASSETS" "$1/licenses/"
	cp "$ROOT/third_party/raylib/LICENSE" "$1/licenses/raylib.txt"
	cp "$ROOT/third_party/sdl2/LICENSE.txt" "$1/licenses/sdl2.txt"
}
licences "$ROOT/dist/rocknix/ports/raptorsprint"
licences "$ROOT/dist/muos/raptorsprint"
(cd "$ROOT/dist/rocknix/ports" && zip -qry "$OUT/raptor-sprint-$VERSION-rocknix.zip" raptorsprint "Raptor Sprint.sh" -x "*.DS_Store")
(cd "$ROOT/dist/muos" && zip -qry "$OUT/raptor-sprint-$VERSION-muos.zip" raptorsprint "Raptor Sprint.sh" -x "*.DS_Store")

# The Mac: only the program and what it needs, none of the saves from testing.
MAC="$ROOT/dist/mac/raptor-sprint"
rm -rf "$ROOT/dist/mac"
mkdir -p "$MAC"
cp "$ROOT/build/mac/raptorsprint" "$MAC/"
cp -R "$ROOT/assets" "$MAC/assets"
licences "$MAC"
(cd "$ROOT/dist/mac" && zip -qry "$OUT/raptor-sprint-$VERSION-mac.zip" raptor-sprint -x "*.DS_Store")

"$ROOT/scripts/build-android.sh"
cp "$ROOT/dist/android/raptor-sprint.apk" "$OUT/raptor-sprint-$VERSION-android.apk"
ls -la "$OUT"
