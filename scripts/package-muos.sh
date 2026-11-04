#!/bin/bash
# Build the handheld binary and lay out the muOS port: dist/muos/{Raptor Sprint.sh, raptorsprint/}
# (both go in ROMS/Ports).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" device
PKG="$ROOT/dist/muos"
rm -rf "$PKG"
mkdir -p "$PKG/raptorsprint"
cp "$ROOT/build/device/raptorsprint" "$PKG/raptorsprint/raptorsprint.aarch64"
cp -R "$ROOT/assets" "$PKG/raptorsprint/assets"
cp "$ROOT/packaging/muos/Raptor Sprint.sh" "$PKG/"
chmod +x "$PKG/Raptor Sprint.sh" "$PKG/raptorsprint/raptorsprint.aarch64"
echo "packaged $PKG"
