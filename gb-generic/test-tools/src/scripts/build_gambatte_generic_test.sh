# Gambatte 코어(PC) + 범용 GB 1배속 시험 도구. 결과: work/build/tests/gambatte_generic_test.exe
source "$(dirname "$0")/common.sh"
G="$WORK/android/gambatte-libretro"; S="$G/libgambatte/src"; F="$G/libgambatte/libretro/ffbgm"; T="$BUILD/tests"; O="$BUILD/gambatte-pc"
mkdir -p "$T" "$O"
INC="-D__LIBRETRO__ -I$S -I$G/libgambatte/include -I$G/common -I$G/common/resample -I$G/libgambatte/libretro -I$G/libgambatte/libretro-common/include -include cstdint"
SRCS="bootloader cpu gambatte initstate interrupter interruptrequester gambatte-memory sound statesaver tima video video_libretro mem/cartridge mem/cartridge_libretro mem/huc3 mem/memptrs mem/rtc sound/channel1 sound/channel2 sound/channel3 sound/channel4 sound/duty_unit sound/envelope_unit sound/length_counter video/ly_counter video/lyc_irq video/next_m0_time video/ppu video/sprite_mapper"
OBJS=""
for s in $SRCS; do o="$O/$(echo $s | tr / _).o"; g++ -O2 -std=gnu++11 $INC -c "$S/$s.cpp" -o "$o" 2>>"$O/err.log" || echo "FAIL $s"; OBJS="$OBJS $o"; done
gcc -O2 $(echo $INC | sed "s/-include cstdint//") -c "$G/libgambatte/libretro/gambatte_log.c" -o "$O/gambatte_log.o" 2>>"$O/err.log"
for s in gb_driver_bgm gb_driver_generic gb_shadow_cpu gsc_player sound_catalog; do g++ -O2 -std=gnu++17 $INC -I$F -c "$F/$s.cpp" -o "$O/ff_$s.o" 2>>"$O/err.log" || echo "FAIL $s"; OBJS="$OBJS $O/ff_$s.o"; done
g++ -O2 -std=gnu++17 $INC -I$F "$SRC/tools/gambatte_generic_test.cpp" $OBJS "$O/gambatte_log.o" -o "$T/gambatte_generic_test.exe" 2>&1 | grep -E "error|undefined" | head -20
[ -e "$T/gambatte_generic_test.exe" ] && echo "gambatte_generic_test ok"
