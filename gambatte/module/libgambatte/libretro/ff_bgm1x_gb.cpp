/* ff-bgm1x: Gambatte 용 GB BGM 1배속 연결 */
#include "ff_bgm1x_gb.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gambatte.h>

#include "catalog_command.h"
#include "gb_driver_bgm.h"
#include "sound_catalog.h"

#if defined(_WIN32)
#define FFBGM_EXPORT extern "C" __declspec(dllexport)
#else
#define FFBGM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

// 원본(Gambatte) 게임 상태 읽기와 채널 끄기·볼륨
struct GambatteMain final : QGBA::GbMainCore {
	gambatte::GB* gb = nullptr;
	uint8_t r8(uint16_t address) override { return gb->ffbgmRead(address); }
	uint8_t r8Bank1(uint16_t address) override { return gb->ffbgmReadBank1(address); }
	void setChannelsDisabled(uint8_t mask) override { gb->ffbgmSetMuteMask(mask); }
	// Gambatte 는 좌우 소리를 32비트 하나에 넣고 넘침을 자르지 않으므로(넘치면 반대쪽 소리가 깨짐) 원본 채널은 두 배까지만
	void setChannelGain(int ch, int q8) override {
		gb->ffbgmSetChannelGain(ch, q8 < 0 ? 0u : static_cast<unsigned>(q8 > 512 ? 512 : q8));
	}
};

std::mutex s_mutex;  // 에뮬레이션 스레드(프레임·옵션)와 오디오 스레드(섞기), 앱 화면(소리 분류) 사이
std::unique_ptr<QGBA::GbDriverBgm> s_driver;
GambatteMain s_main;
// 프론트엔드가 1배속 소리를 섞어 주는지. 섞지 않으면 원본 소리를 건드리지 않는다
std::atomic<bool> s_frontendMixes{false};
bool s_enabled = true;
bool s_sfx1x = true;
SoundCatalog s_catalog;  // 소리 분류 (게임별 파일. 폴더는 앱이 dir 명령으로 알려 준다)

// 분류 화면의 재생: 게임 사운드 엔진 재생기를 따로 하나 만들어 낸다 (게임이 멈춰 있어도)
class GbPreview final : public CatalogPreview {
public:
	// s_mutex 를 잡은 채로 부른다 (s_driver 를 읽는다)
	bool play(const std::string& key, std::string& error) override {
		if (!s_driver) {
			error = "이 게임의 소리는 알아볼 수 없습니다.";
			return false;
		}
		const QGBA::GscTables& tables = s_driver->profile().tables;
		const bool song = key.rfind("gbsong:", 0) == 0;
		const bool cry = key.rfind("cry:", 0) == 0;
		const bool sfx = key.rfind("gbsfx:", 0) == 0 || cry;
		if (!song && !sfx) {
			error = "알 수 없는 이름표입니다.";
			return false;
		}
		std::unique_ptr<QGBA::GscPlayer> gsc(new QGBA::GscPlayer(s_driver->rom(), tables, false));
		if (cry) {
			int id = -1, pitch = 0, len = 0;
			if (!tables.cries || !s_driver->lastCry(id, pitch, len)) {
				error = "울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
				return false;
			}
			gsc->requestCry(static_cast<uint16_t>(id), static_cast<uint16_t>(pitch), static_cast<uint16_t>(len), nullptr);
		} else if (song) {
			gsc->requestSong(static_cast<uint16_t>(std::atoi(key.c_str() + 7)));
		} else {
			gsc->requestSfx(static_cast<uint16_t>(std::atoi(key.c_str() + 6)));
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		m_gsc = std::move(gsc);
		m_sfx = sfx;
		m_calls = 0;
		m_playing = true;
		m_paused = false;
		return true;
	}
	void stop() override {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_gsc.reset();
		m_playing = m_paused = false;
	}
	void setPaused(bool paused) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_playing) m_paused = paused;
	}
	bool playing() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_playing;
	}
	bool paused() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_playing && m_paused;
	}
	double length() const override { return 0; }
	int render(int16_t* stereo, int frames, int rate) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_playing || m_paused || !m_gsc || frames <= 0 || rate <= 0) return 0;
		m_gsc->mixInto(stereo, frames, rate, 0x100);
		if (m_sfx && ++m_calls > 4 && !m_gsc->sfxPlaying()) m_playing = false;
		return frames;
	}

private:
	mutable std::mutex m_mutex;
	std::unique_ptr<QGBA::GscPlayer> m_gsc;
	bool m_sfx = false;
	int m_calls = 0;
	bool m_playing = false, m_paused = false;
} s_preview;

}  // namespace

void ffbgm_gb_load(gambatte::GB* gb, const void* rom, size_t size) {
	s_preview.stop();
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
		s_driver->setCatalog(&s_catalog);
		// 울음소리를 사용자가 1배속으로 정했을 때 원본의 음높이·길이를 읽어 낸다 (정하지 않으면 원본 그대로)
		s_driver->setRamSongs(true);
		// 게임 코드: 롬 머리의 제목(0x134~0x143)과 전체 체크섬(0x14E). 빈칸 등은 _ 로 (PC 판과 같음)
		std::string title;
		for (size_t i = 0x134; i < 0x144 && i < size; ++i) {
			const uint8_t c = p[i];
			if (c == 0) break;
			title.push_back(c >= 0x21 && c < 0x7F ? static_cast<char>(c) : '_');
		}
		char sum[8] = {};
		if (size > 0x14F) std::snprintf(sum, sizeof(sum), "%02X%02X", p[0x14E], p[0x14F]);
		s_catalog.SetGame("GB-" + title + "-" + sum, title);
	}
}

void ffbgm_gb_unload() {
	s_preview.stop();
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

// 에뮬레이션 스레드: 지금 배속을 멈춰야 하는지 (사용자가 연출로 정한 소리가 나오는 동안)
FFBGM_EXPORT bool ff_bgm1x_speed_hold(void) {
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_driver && s_enabled && s_frontendMixes && s_driver->cutscene();
}

// 소리 분류 (common/catalog_command.h 의 명령). 돌려주는 글자는 다음 호출 때까지 유효하다
FFBGM_EXPORT const char* ff_catalog_command(const char* cmd) {
	static std::string result;
	std::lock_guard<std::mutex> lock(s_mutex);
	// 사운드 엔진을 알아본 게임만 (아니면 지난 게임의 목록이 남아 있을 수 있다)
	SoundCatalog* cat = s_driver ? &s_catalog : nullptr;
	result = catalogCommand(cat, &s_preview, s_driver != nullptr, cmd ? cmd : "");
	return result.c_str();
}

// 분류 화면의 재생 소리: stereo 에 frames 개를 더한다. 재생 중이 아니면 0
FFBGM_EXPORT int ff_preview_render(int16_t* stereo, int frames, int sampleRate) {
	return s_preview.render(stereo, frames, sampleRate);
}
