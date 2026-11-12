/* ff-bgm1x: libretro 코어용 연결. Qt 판과 같은 BgmReplacer 를 쓴다.
 * 프론트엔드(수정한 LibretroDroid)가 dlsym 으로 ff_bgm1x_mix / ff_bgm1x_speed_hold 를 찾아 쓴다.
 * 이 함수가 없는 프론트엔드에서는 원본 BGM 을 무음 처리하지 않는다 (ff_bgm1x_mix 를 처음 부를 때 켠다). */
#include "ff_bgm1x.h"

#include <atomic>
#include <memory>
#include <mutex>

#include "../qt/BgmReplacer.h"

namespace {

std::mutex s_mutex;
std::unique_ptr<QGBA::BgmReplacer> s_bgm;
// 프론트엔드가 1배속 소리를 섞어 주는지. 섞지 않으면 원본 BGM 을 건드리지 않는다
std::atomic<bool> s_frontendMixes{false};

struct Options {
	bool enabled = true;
	bool ffCutscenes = false;
	bool sfx1x = true;
	int sfxVolume = 0x100;
} s_opts;

void applyOptions() {
	if (!s_bgm) {
		return;
	}
	s_bgm->setEnabled(s_opts.enabled && s_frontendMixes);
	s_bgm->setFastForwardCutscenes(s_opts.ffCutscenes);
	s_bgm->setSfx1x(s_opts.sfx1x);
	s_bgm->setSfxVolume(s_opts.sfxVolume);
}

}

extern "C" {

void ffBgm1xAttach(struct mCore* core) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_bgm.reset(new QGBA::BgmReplacer(core));
	applyOptions();
}

void ffBgm1xDetach(void) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_bgm.reset();
}

void ffBgm1xReset(struct mCore* core) {
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_bgm) {
		s_bgm->onReset(core);
	}
}

void ffBgm1xFrame(struct mCore* core) {
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_bgm) {
		s_bgm->onFrame(core);
	}
}

void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_opts.enabled = enabled;
	s_opts.ffCutscenes = ffCutscenes;
	s_opts.sfx1x = sfx1x;
	s_opts.sfxVolume = sfxVolume;
	applyOptions();
}

// ---- 프론트엔드가 부르는 함수 ----

// 오디오 출력 스레드: 출력 직전의 스테레오 int16 버퍼에 1배속 소리를 더한다
RETRO_API void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate) {
	if (!s_frontendMixes.exchange(true)) {
		std::lock_guard<std::mutex> lock(s_mutex);
		applyOptions();
	}
	QGBA::BgmReplacer::mixActive(stereo, frames, sampleRate);
}

// 에뮬레이션 스레드: 지금 배속을 멈춰야 하는지 (연출 중)
RETRO_API bool ff_bgm1x_speed_hold(void) {
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_bgm && s_frontendMixes && s_bgm->speedHold();
}

}
