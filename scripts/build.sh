#!/bin/bash
# Build Raptor Sprint for the Mac or Windows (OpenGL 3.3) or the handheld (aarch64, SDL2 + OpenGL ES 2.0).
# Output: build/<target>/raptorsprint plus assets/. raylib comes prebuilt
# (third_party/raylib/<target>/libraylib.a), and SDL2 for the handheld in third_party/sdl2. The
# handheld build needs an aarch64 Linux cross compiler: $CROSS is its prefix, by default
# aarch64-unknown-linux-gnu- found on the PATH.
# The Windows build needs MinGW-w64: scripts/package-windows.sh runs this in a container that has it.
# The web build needs Emscripten ($EMSDK, default ~/Library/emsdk, with a Python 3.10 or later in
# $EMSDK_PYTHON if the system's is older).
# Usage: scripts/build.sh mac|windows|device|web
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
elif [ "$TARGET" = windows ]; then
	X="${CROSS:-x86_64-w64-mingw32-}"
	"${X}windres" "$ROOT/packaging/windows/icon.rc" -O coff -o "$OUT/icon.o"
	"${X}g++-posix" "${COMMON[@]}" -DGRAPHICS_API_OPENGL_33 "${SRC[@]}" "$OUT/icon.o" "$ROOT/third_party/raylib/windows/libraylib.a" \
		-static -static-libgcc -static-libstdc++ -mwindows -lopengl32 -lgdi32 -lwinmm \
		-o "$OUT/raptorsprint.exe"
	"${X}strip" "$OUT/raptorsprint.exe"
elif [ "$TARGET" = web ]; then
	EMSDK="${EMSDK:-$HOME/Library/emsdk}"
	if [ -z "${EMSDK_PYTHON:-}" ] && [ -x "$HOME/Library/python312/bin/python3" ]; then export EMSDK_PYTHON="$HOME/Library/python312/bin/python3"; fi
	source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
	# (Asyncify lets the game's own loop hand each frame back to the browser.)
	em++ "${COMMON[@]}" -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2 "${SRC[@]}" "$ROOT/third_party/raylib/web/libraylib.a" \
		-sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sENVIRONMENT=web \
		-sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT=1 -sASYNCIFY_STACK_SIZE=65536 \
		-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=2097152 \
		-sFORCE_FILESYSTEM=1 -lidbfs.js -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPF32,addRunDependency,removeRunDependency \
		--preload-file "$ROOT/assets@/assets" --shell-file "$ROOT/packaging/web/index.html" -o "$OUT/index.html"
	echo "built $OUT/index.html"
	exit 0
else
	X="${CROSS:-aarch64-unknown-linux-gnu-}"
	"${X}g++" "${COMMON[@]}" -DGRAPHICS_API_OPENGL_ES2 -DPLATFORM_DESKTOP_SDL "-I$ROOT/third_party/sdl2/include" "${SRC[@]}" "$ROOT/third_party/raylib/device/libraylib.a" \
		"$ROOT/third_party/sdl2/lib/libSDL2.so" \
		-Wl,--unresolved-symbols=ignore-in-shared-libs -ldl -lpthread -lm \
		-o "$OUT/raptorsprint"
	"${X}strip" "$OUT/raptorsprint"
fi
rm -rf "$OUT/assets"
cp -R "$ROOT/assets" "$OUT/assets"
echo "built $OUT/raptorsprint"
