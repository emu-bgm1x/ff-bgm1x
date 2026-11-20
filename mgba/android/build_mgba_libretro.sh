#!/bin/bash
# ff-bgm1x: 수정한 mGBA 의 libretro 코어를 Android 용으로 빌드한다 (Git Bash 에서 실행)
# 사용: bash build_mgba_libretro.sh [abi...]   (기본 arm64-v8a)
SDK="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk"
NDK="$SDK/ndk/27.3.13750724"
CMAKE_BIN="$SDK/cmake/3.31.6/bin"
# MGBA_SRC: 수정한 mGBA 소스 폴더 (mgba/module 과 hooks.patch 를 적용한 것, 옆에 common/ 이 있어야 함)
# OUT: 결과 폴더 (jniLibs/<abi>/libmgba_libretro_android.so 가 생긴다)
SRC="${MGBA_SRC:-$(cd "$(dirname "$0")/../../src/mgba" 2>/dev/null && pwd)}"
OUT="${OUT:-$(dirname "$0")/mgba-libretro}"
[ -d "$SRC" ] || { echo "MGBA_SRC 를 mGBA 소스 폴더로 지정하세요"; exit 1; }
ABIS="${@:-arm64-v8a}"

for ABI in $ABIS; do
  B="$OUT/build-$ABI"
  "$CMAKE_BIN/cmake.exe" -S "$(cygpath -w "$SRC")" -B "$(cygpath -w "$B")" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$(cygpath -w "$CMAKE_BIN/ninja.exe")" \
    -DCMAKE_TOOLCHAIN_FILE="$(cygpath -w "$NDK/build/cmake/android.toolchain.cmake")" \
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-23 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_LIBRETRO=ON -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=OFF \
    -DSKIP_LIBRARY=ON -DDISABLE_DEPS=ON -DENABLE_SCRIPTING=OFF -DUSE_DISCORD_RPC=OFF \
    -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF \
    > "$OUT/cfg-$ABI.log" 2>&1
  echo "$ABI CFG_EXIT=$?"
  "$CMAKE_BIN/cmake.exe" --build "$(cygpath -w "$B")" --target mgba_libretro > "$OUT/build-$ABI.log" 2>&1
  echo "$ABI BUILD_EXIT=$?"
  grep -E "error:|FAILED" "$OUT/build-$ABI.log" | head -20
  mkdir -p "$OUT/jniLibs/$ABI"
  cp "$B/mgba_libretro.so" "$OUT/jniLibs/$ABI/libmgba_libretro_android.so" 2>/dev/null && ls -la "$OUT/jniLibs/$ABI"
done
