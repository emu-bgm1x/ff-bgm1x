#!/bin/bash
# ff-bgm1x: 수정한 VBA-M 의 libretro 코어를 Android 용으로 빌드한다 (Git Bash 에서 실행)
# 사용: bash build_vbam_libretro.sh [abi...]   (기본 arm64-v8a)
# VBAM_SRC: 수정한 VBA-M 소스 폴더 (vbam/module 과 hooks.patch 를 적용한 것)
# OUT: 결과 폴더 (jniLibs/<abi>/libvbam_libretro_android.so 가 생긴다)
SDK="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk"
NDK="$SDK/ndk/27.3.13750724"
SRC="${VBAM_SRC:-$(cd "$(dirname "$0")/../vbam/vbam-src" 2>/dev/null && pwd)}"
OUT="${OUT:-$(cd "$(dirname "$0")" && pwd)/vbam-libretro}"
[ -d "$SRC/src/libretro/jni" ] || { echo "VBAM_SRC 를 VBA-M 소스 폴더로 지정하세요"; exit 1; }
ABIS="${@:-arm64-v8a}"
mkdir -p "$OUT"

for ABI in $ABIS; do
  B="$OUT/build-$ABI"
  "$NDK/ndk-build.cmd" -C "$(cygpath -w "$SRC/src/libretro/jni")" -j8 \
    APP_ABI=$ABI APP_PLATFORM=android-23 APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true \
    NDK_OUT="$(cygpath -w "$B/obj")" NDK_LIBS_OUT="$(cygpath -w "$B/libs")" \
    > "$OUT/build-$ABI.log" 2>&1
  echo "$ABI BUILD_EXIT=$?"
  grep -E "error:|Error " "$OUT/build-$ABI.log" | head -20
  mkdir -p "$OUT/jniLibs/$ABI"
  cp "$B/libs/$ABI/libretro.so" "$OUT/jniLibs/$ABI/libvbam_libretro_android.so" 2>/dev/null && ls -la "$OUT/jniLibs/$ABI"
done
