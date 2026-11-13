#!/bin/bash
# ff-bgm1x: 숨겨진 GB 코어용 mGBA(공식 0.10.5, GB 만) 정적 라이브러리를 Android 용으로 빌드한다 (Git Bash)
# Gambatte(libretro) 에 넣어 GB BGM 1배속을 낸다. 결과: mgba-gbcore-android/build-<abi>/libmgba.a
SDK="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk"
NDK="$SDK/ndk/27.3.13750724"
CMAKE_BIN="$SDK/cmake/3.31.6/bin"
SRC="$(cd "$(dirname "$0")/../mgba-0.10.5-upstream" && pwd)"
OUT="$(cd "$(dirname "$0")" && pwd)/mgba-gbcore-android"
mkdir -p "$OUT"
ABIS="${@:-arm64-v8a}"
for ABI in $ABIS; do
  B="$OUT/build-$ABI"
  # crc32 는 zlib 과 이름이 겹치므로 바꾼다 (VBA-M 판과 같음)
  "$CMAKE_BIN/cmake.exe" -S "$(cygpath -w "$SRC")" -B "$(cygpath -w "$B")" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$(cygpath -w "$CMAKE_BIN/ninja.exe")" \
    -DCMAKE_TOOLCHAIN_FILE="$(cygpath -w "$NDK/build/cmake/android.toolchain.cmake")" \
    -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-21 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DLIBMGBA_ONLY=ON -DM_CORE_GBA=OFF -DM_CORE_GB=ON -DBUILD_STATIC=ON -DBUILD_SHARED=OFF \
    -DDISABLE_DEPS=ON -DENABLE_SCRIPTING=OFF -DUSE_DISCORD_RPC=OFF \
    -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF \
    "-DCMAKE_C_FLAGS=-Dcrc32=mgba_crc32 -fPIC" "-DCMAKE_CXX_FLAGS=-Dcrc32=mgba_crc32 -fPIC" \
    > "$OUT/cfg-$ABI.log" 2>&1
  echo "$ABI CFG_EXIT=$?"
  "$CMAKE_BIN/cmake.exe" --build "$(cygpath -w "$B")" > "$OUT/build-$ABI.log" 2>&1
  echo "$ABI BUILD_EXIT=$?"
  grep -E "error:|FAILED" "$OUT/build-$ABI.log" | head -20
  ls -la "$B"/*.a 2>/dev/null
done
