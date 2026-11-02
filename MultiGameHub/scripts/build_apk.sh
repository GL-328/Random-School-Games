#!/usr/bin/env bash
# Builds a signed release APK directly with the Android SDK/NDK command-line
# tools (no Gradle / Google Maven needed). Equivalent to `./gradlew assembleRelease`.
#
# Environment:
#   ANDROID_HOME          Android SDK root (default /opt/android)
#   ANDROID_NDK_ROOT      NDK root (default: newest under $ANDROID_HOME/ndk)
#   BUILD_TOOLS_VERSION   default: newest installed
#   MGH_KEYSTORE_FILE / MGH_KEYSTORE_PASSWORD / MGH_KEY_ALIAS / MGH_KEY_PASSWORD
#                         release signing key. If unset a local debug key is created
#                         (APKs signed with different keys cannot update each other!)
#   OUT_DIR               default: build/android
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_HOME="${ANDROID_HOME:-/opt/android}"
NDK="${ANDROID_NDK_ROOT:-$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)}"
BT_VER="${BUILD_TOOLS_VERSION:-$(ls "$ANDROID_HOME/build-tools" | sort -V | tail -1)}"
BT="$ANDROID_HOME/build-tools/$BT_VER"
COMPILE_SDK="${COMPILE_SDK:-35}"
MIN_SDK=21
TARGET_SDK=35
ANDROID_JAR="$ANDROID_HOME/platforms/android-$COMPILE_SDK/android.jar"
OUT="${OUT_DIR:-$ROOT/build/android}"
PKG="com.gl328.multigamehub"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
IFS=. read -r VMAJ VMIN VPAT <<< "$VERSION"
VCODE=$((VMAJ * 10000 + VMIN * 100 + VPAT))

for f in "$NDK/build/cmake/android.toolchain.cmake" "$BT/aapt2" "$BT/d8" "$BT/apksigner" "$BT/zipalign" "$ANDROID_JAR"; do
  [[ -e "$f" ]] || { echo "missing: $f" >&2; exit 1; }
done
[[ -f "$ROOT/third_party/SDL/CMakeLists.txt" ]] || "$ROOT/scripts/fetch_deps.sh"

echo "== MultiGame Hub $VERSION ($VCODE) | NDK $(basename "$NDK") | build-tools $BT_VER"
mkdir -p "$OUT"
NATIVE="$OUT/native"
STAGE="$OUT/stage"
mkdir -p "$STAGE"
find "$STAGE" -mindepth 1 -delete

# ---- 1. native library (all C++ code + static SDL3)
cmake -S "$ROOT" -B "$NATIVE" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$MIN_SDK -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DMGH_BUILD_TESTS=OFF > "$OUT/cmake-configure.log"
cmake --build "$NATIVE" --target main
LIB="$NATIVE/libmain.so"
echo "libmain.so: $(stat -c %s "$LIB") bytes"

# ---- 2. Java (SDL glue + bridge) -> dex
JAVA_SRC=("$ROOT/android/app/src/main/java" "$ROOT/third_party/SDL/android-project/app/src/main/java")
mkdir -p "$STAGE/classes" "$STAGE/dex"
find "${JAVA_SRC[@]}" -name '*.java' > "$STAGE/sources.txt"
javac -nowarn -Xlint:none -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -encoding UTF-8 \
  -d "$STAGE/classes" @"$STAGE/sources.txt" 2> "$STAGE/javac.log" || { cat "$STAGE/javac.log"; exit 1; }
"$BT/d8" --release --min-api $MIN_SDK --lib "$ANDROID_JAR" --output "$STAGE/dex" \
  $(find "$STAGE/classes" -name '*.class')

# ---- 3. resources + manifest + assets
"$BT/aapt2" compile --dir "$ROOT/android/app/src/main/res" -o "$STAGE/res.zip"
sed "s|<manifest |<manifest package=\"$PKG\" |" "$ROOT/android/app/src/main/AndroidManifest.xml" > "$STAGE/AndroidManifest.xml"
"$BT/aapt2" link -o "$STAGE/base.apk" -I "$ANDROID_JAR" \
  --manifest "$STAGE/AndroidManifest.xml" \
  --min-sdk-version $MIN_SDK --target-sdk-version $TARGET_SDK \
  --version-code $VCODE --version-name "$VERSION" \
  -A "$ROOT/assets" "$STAGE/res.zip"

# ---- 4. add dex + native library (stored uncompressed so it can be mmapped)
python3 - "$STAGE/base.apk" "$STAGE/dex/classes.dex" "$LIB" "$STAGE/unaligned.apk" <<'PY'
import sys, zipfile, shutil
base, dex, lib, out = sys.argv[1:]
shutil.copy(base, out)
with zipfile.ZipFile(out, "a") as z:
    z.write(dex, "classes.dex", compress_type=zipfile.ZIP_DEFLATED)
    z.write(lib, "lib/arm64-v8a/libmain.so", compress_type=zipfile.ZIP_STORED)
PY
# 16 KB page alignment for the .so (required for Android 15+ 16K-page devices).
"$BT/zipalign" -f -P 16 4 "$STAGE/unaligned.apk" "$STAGE/aligned.apk"

# ---- 5. sign
if [[ -z "${MGH_KEYSTORE_FILE:-}" ]]; then
  MGH_KEYSTORE_FILE="$OUT/debug.keystore"
  MGH_KEYSTORE_PASSWORD=android MGH_KEY_ALIAS=mghdebug MGH_KEY_PASSWORD=android
  if [[ ! -f "$MGH_KEYSTORE_FILE" ]]; then
    keytool -genkeypair -keystore "$MGH_KEYSTORE_FILE" -storepass android -keypass android \
      -alias mghdebug -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=MultiGame Hub Debug,O=GL-328,C=GB" > /dev/null 2>&1
  fi
  echo "WARNING: signed with a local debug key ($MGH_KEYSTORE_FILE)"
fi
APK="$OUT/MultiGameHub-$VERSION.apk"
"$BT/apksigner" sign --ks "$MGH_KEYSTORE_FILE" --ks-pass "pass:$MGH_KEYSTORE_PASSWORD" \
  --ks-key-alias "$MGH_KEY_ALIAS" --key-pass "pass:$MGH_KEY_PASSWORD" \
  --min-sdk-version $MIN_SDK --out "$APK" "$STAGE/aligned.apk"

# ---- 6. verify
"$BT/apksigner" verify --min-sdk-version $MIN_SDK "$APK"
"$BT/zipalign" -c -P 16 4 "$APK"
echo "APK: $APK ($(stat -c %s "$APK") bytes)"
sha256sum "$APK" | cut -d' ' -f1 > "$APK.sha256"
