#!/bin/bash
# Build the handheld binary and lay out the ROCKNIX port: dist/rocknix/ports/{Raptor Sprint.sh, raptorsprint/}.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" device
PKG="$ROOT/dist/rocknix/ports"
rm -rf "$ROOT/dist/rocknix"
mkdir -p "$PKG/raptorsprint"
cp "$ROOT/build/device/raptorsprint" "$PKG/raptorsprint/raptorsprint.aarch64"
cp -R "$ROOT/assets" "$PKG/raptorsprint/assets"
cp "$ROOT/packaging/rocknix/Raptor Sprint.sh" "$PKG/"
chmod +x "$PKG/Raptor Sprint.sh" "$PKG/raptorsprint/raptorsprint.aarch64"
echo "packaged $PKG"
