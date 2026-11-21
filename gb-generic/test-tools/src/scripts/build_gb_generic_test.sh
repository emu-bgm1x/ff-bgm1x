# GB 1배속 두 방식 대조 도구 빌드 (build_mgba.sh 로 만든 libmgba.a 사용). 결과: work/build/tests/gb_generic_test.exe
source "$(dirname "$0")/common.sh"
M="$SRC/mgba"; B="$BUILD/mgba-qt"; C="$SRC/common"; T="$BUILD/tests"; Q="$M/src/platform/qt"
mkdir -p "$T"
rm -f "$T/gb_generic_test.exe"
LIBS="$B/libmgba.a -lavcodec -lavfilter -lavformat -lavutil -lswscale -lswresample -lbcrypt -lzip -lz -lpng -lsqlite3 -lelf -llua -lepoxy -lopengl32 -lws2_32 -lshlwapi -lole32 -luuid -lshell32 -lm"
INC="-DM_CORE_GB -DM_CORE_GBA -DBUILD_STATIC -I$M/include -I$B/include -I$M/src -I$Q -I$C"
g++ -O2 -std=gnu++17 $INC "$SRC/tools/gb_generic_test.cpp" "$Q/GbDriverBgm.cpp" "$Q/GbDriverGeneric.cpp" "$Q/GbNativeApuMgba.cpp" "$Q/GbShadowCpu.cpp" "$Q/GscPlayer.cpp" "$C/sound_catalog.cpp" -o "$T/gb_generic_test.exe" $LIBS 2>&1 | grep -E "error|undefined" | head -30
[ -e "$T/gb_generic_test.exe" ] && echo "gb_generic_test ok"
