#!/bin/bash
# The files for a release, in dist/release: a zip for each handheld system, the Mac and Windows, and
# the Android app. (Windows builds in a container, so Docker must be running.)
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

"$ROOT/scripts/package-mac-app.sh" "$VERSION"
cp "$ROOT/dist/mac-app/raptor-sprint-$VERSION-mac-app.zip" "$OUT/raptor-sprint-$VERSION-mac.zip"
"$ROOT/scripts/package-windows.sh" "$VERSION"
cp "$ROOT/dist/windows/raptor-sprint-$VERSION-windows.zip" "$OUT/"

"$ROOT/scripts/build-android.sh"
cp "$ROOT/dist/android/raptor-sprint.apk" "$OUT/raptor-sprint-$VERSION-android.apk"
ls -la "$OUT"
