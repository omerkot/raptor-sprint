#!/bin/bash
# Build Raptor Sprint for the Mac (OpenGL 3.3) or the handheld (aarch64, SDL2 + OpenGL ES 2.0).
# Output: build/<target>/raptorsprint plus assets/. raylib comes prebuilt
# (third_party/raylib/<target>/libraylib.a), and SDL2 for the handheld in third_party/sdl2. The
# handheld build needs an aarch64 Linux cross compiler: $CROSS is its prefix, by default
# aarch64-unknown-linux-gnu- found on the PATH.
# Usage: scripts/build.sh mac|device
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET=${1:-mac}
OUT="$ROOT/build/$TARGET"
mkdir -p "$OUT"
SRC=("$ROOT"/src/*.cpp)
COMMON=(-std=c++17 -O2 -Wall -Wno-missing-braces "-I$ROOT/third_party/raylib/include")
if [ "$TARGET" = mac ]; then
	# (Without a minimum version, the program would only run on the macOS it was built on.)
	clang++ "${COMMON[@]}" -mmacosx-version-min=12.0 -DGRAPHICS_API_OPENGL_33 "${SRC[@]}" "$ROOT/third_party/raylib/mac/libraylib.a" \
		-framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio -framework AudioToolbox \
		-o "$OUT/raptorsprint"
else
	X="${CROSS:-aarch64-unknown-linux-gnu-}"
	"${X}g++" "${COMMON[@]}" -DGRAPHICS_API_OPENGL_ES2 -DPLATFORM_DESKTOP_SDL "-I$ROOT/third_party/sdl2/include" "${SRC[@]}" "$ROOT/third_party/raylib/device/libraylib.a" \
		"$ROOT/third_party/sdl2/lib/libSDL2.so" \
		-static-libstdc++ -static-libgcc -Wl,--unresolved-symbols=ignore-in-shared-libs -ldl -lpthread -lm \
		-o "$OUT/raptorsprint"
	"${X}strip" "$OUT/raptorsprint"
fi
rm -rf "$OUT/assets"
cp -R "$ROOT/assets" "$OUT/assets"
echo "built $OUT/raptorsprint"
