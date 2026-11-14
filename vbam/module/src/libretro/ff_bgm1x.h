// ff-bgm1x: libretro 코어(안드로이드 Lemuroid)용 연결. 구현은 ff_bgm1x.cpp
// PC 판(src/wx/ffbgm)과 같은 재생기를 쓴다. GBA 만 지원한다 (GB/GBC 는 Lemuroid 에서 Gambatte 코어가 맡는다).
#ifndef VBAM_LIBRETRO_FF_BGM1X_H_
#define VBAM_LIBRETRO_FF_BGM1X_H_

#include <cstddef>
#include <cstdint>

#include <libretro.h>

// 에뮬레이션 스레드에서 부른다
void ffBgm1xGbaLoaded(const uint8_t* rom, size_t size);
void ffBgm1xUnload();
void ffBgm1xFrame();
void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume);

// 프론트엔드(수정한 LibretroDroid)가 dlsym 으로 찾는다
extern "C" {
// 오디오 출력 스레드: 출력 직전의 스테레오 int16 버퍼에 1배속 소리를 더한다
RETRO_API void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate);
// 에뮬레이션 스레드: 지금 배속을 멈춰야 하는지 (연출 중)
RETRO_API bool ff_bgm1x_speed_hold(void);
}

#endif  // VBAM_LIBRETRO_FF_BGM1X_H_
