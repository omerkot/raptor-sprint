#!/bin/bash
# Build the browser version into dist/web: public/ is the whole site, served by the Worker set out
# in wrangler.jsonc. To put it online: cd dist/web && npx wrangler deploy
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" web
OUT="$ROOT/dist/web"
rm -rf "$OUT"
mkdir -p "$OUT/public"
B="$ROOT/build/web"
cp "$B/index.html" "$B/index.js" "$B/index.wasm" "$B/index.data" "$ROOT/packaging/web/manifest.webmanifest" "$OUT/public/"
cp "$ROOT/assets/fonts/Nunito-OFL.txt" "$OUT/public/"
for size in 180 192 512; do
	sips -z $size $size "$ROOT/packaging/mac/icon.png" --out "$OUT/public/icon-$size.png" >/dev/null
done
cp "$ROOT/packaging/web/wrangler.jsonc" "$OUT/"
du -sh "$OUT/public"
echo "built $OUT"
