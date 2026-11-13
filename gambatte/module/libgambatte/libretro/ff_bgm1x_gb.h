/* ff-bgm1x: Gambatte 용 GB BGM 1배속 연결 (숨겨진 mGBA GB 코어가 같은 곡을 1배속으로 낸다).
 * 프론트엔드(수정한 LibretroDroid)가 dlsym 으로 ff_bgm1x_mix / ff_bgm1x_speed_hold 를 찾아 쓴다.
 * 이 함수가 없는 프론트엔드에서는 원본 소리를 건드리지 않는다 (ff_bgm1x_mix 를 처음 부를 때 켠다). */
#ifndef FF_BGM1X_GB_H
#define FF_BGM1X_GB_H

#include <stddef.h>

namespace gambatte {
class GB;
}

void ffbgm_gb_load(gambatte::GB* gb, const void* rom, size_t size);
void ffbgm_gb_unload();
void ffbgm_gb_reset();
void ffbgm_gb_frame();
void ffbgm_gb_set_options(bool enabled, bool sfx1x);

#endif
