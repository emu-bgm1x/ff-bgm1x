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
void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume);

RETRO_API void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate);
RETRO_API bool ff_bgm1x_speed_hold(void);

#ifdef __cplusplus
}
#endif

#endif
