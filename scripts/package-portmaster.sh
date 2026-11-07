#!/bin/bash
# The PortMaster version: dist/portmaster/raptorsprint is the port's folder as it goes in their
# repository, and dist/portmaster/raptorsprint.zip the same files laid out for installing on a device.
# With "test", it's Raptor Sprint Test in dist/portmaster-test/raptorsprinttest, which installs
# beside the game and keeps its own saves.
# (Builds for the handhelds, so set CROSS if you need it: see scripts/build.sh.)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" device

NAME=raptorsprint
TITLE="Raptor Sprint"
OUT="$ROOT/dist/portmaster"
if [ "$1" = test ]; then
	NAME=raptorsprinttest
	TITLE="Raptor Sprint Test"
	OUT="$ROOT/dist/portmaster-test"
fi
PORT="$OUT/$NAME"
GAME="$PORT/$NAME"
rm -rf "$OUT"
mkdir -p "$GAME/licenses"
cp "$ROOT/packaging/portmaster/port.json" "$ROOT/packaging/portmaster/gameinfo.xml" "$ROOT/packaging/portmaster/README.md" \
	"$ROOT/packaging/portmaster/screenshot.png" "$ROOT/packaging/portmaster/cover.png" "$PORT/"
cp "$ROOT/packaging/portmaster/Raptor Sprint.sh" "$PORT/$TITLE.sh"
if [ "$NAME" != raptorsprint ]; then
	sed -i.bak -e "s/raptorsprint/$NAME/g" -e "s/Raptor Sprint/$TITLE/g" "$PORT/port.json" "$PORT/gameinfo.xml" "$PORT/README.md"
	sed -i.bak "s|/ports/raptorsprint\$|/ports/$NAME|" "$PORT/$TITLE.sh"
	rm "$PORT"/*.bak
fi
cp "$ROOT/build/device/raptorsprint" "$GAME/raptorsprint.aarch64"
cp -R "$ROOT/assets" "$GAME/assets"
cp "$ROOT/LICENSE" "$GAME/licenses/LICENSE.raptorsprint.txt"
cp "$ROOT/LICENSE-ASSETS" "$GAME/licenses/LICENSE.raptorsprint-assets.txt"
cp "$ROOT/third_party/raylib/LICENSE" "$GAME/licenses/LICENSE.raylib.txt"
cp "$ROOT/assets/fonts/Nunito-OFL.txt" "$GAME/licenses/LICENSE.nunito.txt"

# In the zip, everything but the launcher sits inside the game's folder.
STAGE="$OUT/zip"
mkdir -p "$STAGE"
cp -R "$GAME" "$STAGE/$NAME"
cp "$PORT/$TITLE.sh" "$STAGE/"
cp "$PORT/port.json" "$PORT/gameinfo.xml" "$PORT/screenshot.png" "$PORT/cover.png" "$STAGE/$NAME/"
cp "$PORT/README.md" "$STAGE/$NAME/$NAME.md"
(cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$TITLE.sh" "$NAME" -x "*.DS_Store")
rm -rf "$STAGE"
ls -la "$OUT/$NAME.zip"
