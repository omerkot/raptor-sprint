#!/bin/bash
# Build the Android version: dist/android/raptor-sprint.apk (64-bit). Needs the Android SDK with
# its NDK, a platform and build tools ($ANDROID_HOME, default ~/Library/Android/sdk), and Python
# with the cryptography package for signing. raylib comes prebuilt in third_party/raylib/android.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="$(ls -d "$SDK"/ndk/* | tail -1)"
TOOLS="$(ls -d "$SDK"/build-tools/* | tail -1)"
PLATFORM="$(ls -d "$SDK"/platforms/* | tail -1)"
CC="$(ls -d "$NDK"/toolchains/llvm/prebuilt/*/bin | tail -1)/aarch64-linux-android26-clang"
GLUE="$NDK/sources/android/native_app_glue"
OUT="$ROOT/build/android"
PKG="$ROOT/dist/android"
rm -rf "$OUT" "$PKG"
mkdir -p "$OUT/lib/arm64-v8a" "$PKG"

"$CC" -O2 -fPIC -w -c "$GLUE/android_native_app_glue.c" -o "$OUT/glue.o"
"$CC++" -std=c++17 -O2 -fPIC -Wall -Wno-missing-braces -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 \
	"-I$ROOT/third_party/raylib/include" "-I$GLUE" "$ROOT"/src/*.cpp "$OUT/glue.o" \
	"$ROOT/third_party/raylib/android/libraylib.a" \
	-shared -static-libstdc++ -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384 -Wl,--gc-sections -s \
	-landroid -llog -lEGL -lGLESv2 -lOpenSLES -lm -ldl \
	-o "$OUT/lib/arm64-v8a/libmain.so"

"$TOOLS/aapt2" compile --dir "$ROOT/android/res" -o "$OUT/res.zip"
"$TOOLS/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM/android.jar" --manifest "$ROOT/android/AndroidManifest.xml" \
	-A "$ROOT/assets" ${ANDROID_DEBUGGABLE:+--debug-mode} "$OUT/res.zip"
(cd "$OUT" && zip -q base.apk lib/arm64-v8a/libmain.so)
"$TOOLS/zipalign" -f -p 4 "$OUT/base.apk" "$OUT/aligned.apk"
python3 "$ROOT/scripts/sign-apk.py" "$OUT/aligned.apk" "$PKG/raptor-sprint.apk"
echo "built $PKG/raptor-sprint.apk"
