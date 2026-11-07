#!/bin/bash
# The Mac version as one app: dist/mac-app/Raptor Sprint.app, and a zip of it to pass around. It isn't
# notarised, so a Mac that receives it has to be told to open it the first time.
# Saves go in ~/Library/Application Support/Raptor Sprint (see save_path in src/core.hpp).
# Usage: scripts/package-mac-app.sh [VERSION]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=${1:-$(sed -n 's/.*versionName="\([^"]*\)".*/\1/p' "$ROOT/android/AndroidManifest.xml")}
OUT="$ROOT/dist/mac-app"
APP="$OUT/Raptor Sprint.app"
"$ROOT/scripts/build.sh" mac
rm -rf "$OUT"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$ROOT/build/mac/raptorsprint" "$APP/Contents/MacOS/"
cp -R "$ROOT/assets" "$APP/Contents/Resources/assets"
mkdir -p "$APP/Contents/Resources/licenses"
cp "$ROOT/LICENSE" "$ROOT/LICENSE-ASSETS" "$APP/Contents/Resources/licenses/"
cp "$ROOT/third_party/raylib/LICENSE" "$APP/Contents/Resources/licenses/raylib.txt"

SET="$OUT/icon.iconset"
mkdir -p "$SET"
for s in 16 32 128 256 512; do
	sips -z $s $s "$ROOT/packaging/mac/icon.png" --out "$SET/icon_${s}x${s}.png" > /dev/null
	sips -z $((s * 2)) $((s * 2)) "$ROOT/packaging/mac/icon.png" --out "$SET/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$SET" -o "$APP/Contents/Resources/icon.icns"
rm -rf "$SET"

sed "s/@VERSION@/$VERSION/g" "$ROOT/packaging/mac/Info.plist" > "$APP/Contents/Info.plist"
find "$APP" -name .DS_Store -delete
xattr -cr "$APP"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
(cd "$OUT" && ditto -c -k --keepParent "Raptor Sprint.app" "raptor-sprint-$VERSION-mac-app.zip")
echo "built $APP"
