/* ff-bgm1x: Gambatte 용 GB BGM 1배속 연결 */
#include "ff_bgm1x_gb.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <gambatte.h>

#include "gb_driver_bgm.h"

#if defined(_WIN32)
#define FFBGM_EXPORT extern "C" __declspec(dllexport)
#else
#define FFBGM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

// 원본(Gambatte) 게임 상태 읽기와 채널 끄기
struct GambatteMain final : QGBA::GbMainCore {
	gambatte::GB* gb = nullptr;
	uint8_t r8(uint16_t address) override { return gb->ffbgmRead(address); }
	uint8_t r8Bank1(uint16_t address) override { return gb->ffbgmReadBank1(address); }
	void setChannelsDisabled(uint8_t mask) override { gb->ffbgmSetMuteMask(mask); }
};

std::mutex s_mutex;  // 에뮬레이션 스레드(프레임·옵션)와 오디오 스레드(섞기) 사이
std::unique_ptr<QGBA::GbDriverBgm> s_driver;
GambatteMain s_main;
// 프론트엔드가 1배속 소리를 섞어 주는지. 섞지 않으면 원본 소리를 건드리지 않는다
std::atomic<bool> s_frontendMixes{false};
bool s_enabled = true;
bool s_sfx1x = true;

}  // namespace

void ffbgm_gb_load(gambatte::GB* gb, const void* rom, size_t size) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_driver.reset();
	s_main.gb = gb;
	if (!rom || !size) {
		return;
	}
	const uint8_t* p = static_cast<const uint8_t*>(rom);
	std::vector<uint8_t> data(p, p + size);
	QGBA::GbDriverProfile profile;
	if (QGBA::detectGbDriver(data, profile)) {
		s_driver.reset(new QGBA::GbDriverBgm(data, profile));
	}
}

void ffbgm_gb_unload() {
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_driver && s_main.gb) {
		s_driver->releaseMute(s_main);
	}
	s_driver.reset();
}

void ffbgm_gb_reset() {
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_driver) {
		s_driver->onReset();
	}
}

void ffbgm_gb_frame() {
	std::lock_guard<std::mutex> lock(s_mutex);
	if (!s_driver || !s_main.gb) {
		return;
	}
	// 프론트엔드가 섞어 주지 않으면 끈 것과 같다 (원본 채널을 끄지 않는다)
	s_driver->onFrame(s_main, s_enabled && s_frontendMixes, s_sfx1x);
}

void ffbgm_gb_set_options(bool enabled, bool sfx1x) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_enabled = enabled;
	s_sfx1x = sfx1x;
}

// ---- 프론트엔드가 부르는 함수 ----

// 오디오 출력 스레드: 출력 직전의 스테레오 int16 버퍼에 1배속 소리를 더한다
FFBGM_EXPORT void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate) {
	s_frontendMixes = true;
	std::lock_guard<std::mutex> lock(s_mutex);
	if (!s_driver || !s_enabled || frames <= 0 || sampleRate <= 0) {
		return;
	}
	s_driver->mixRealtime(stereo, frames, sampleRate, 0x100);
}

// 에뮬레이션 스레드: 지금 배속을 멈춰야 하는지 (GB 엔진에는 연출 판단이 없다)
FFBGM_EXPORT bool ff_bgm1x_speed_hold(void) {
	return false;
}
