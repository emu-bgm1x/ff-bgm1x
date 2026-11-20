/* ff-bgm1x: libretro.c 에서 부르는 BGM 1배속 연결 (구현은 ff_bgm1x.cpp) */
#ifndef FF_BGM1X_H
#define FF_BGM1X_H

#include <stdbool.h>
#include <stdint.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mCore;

void ffBgm1xAttach(struct mCore* core);
void ffBgm1xDetach(void);
void ffBgm1xReset(struct mCore* core);
void ffBgm1xFrame(struct mCore* core);
void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume, bool ramSongs);

RETRO_API void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate);
RETRO_API bool ff_bgm1x_speed_hold(void);
// 소리 분류 (common/catalog_command.h 의 명령). 돌려주는 글자는 다음 호출 때까지 유효하다
RETRO_API const char* ff_catalog_command(const char* cmd);
// 분류 화면의 재생 소리: stereo 에 frames 개를 더한다. 재생 중이 아니면 0
RETRO_API int ff_preview_render(int16_t* stereo, int frames, int sampleRate);

#ifdef __cplusplus
}
#endif

#endif
