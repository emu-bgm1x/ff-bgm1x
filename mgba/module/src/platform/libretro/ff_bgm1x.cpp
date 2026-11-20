/* ff-bgm1x: libretro 코어용 연결. Qt 판과 같은 BgmReplacer 를 쓴다.
 * 프론트엔드(수정한 LibretroDroid)가 dlsym 으로 ff_bgm1x_mix / ff_bgm1x_speed_hold 를 찾아 쓴다.
 * 이 함수가 없는 프론트엔드에서는 원본 BGM 을 무음 처리하지 않는다 (ff_bgm1x_mix 를 처음 부를 때 켠다). */
#include "ff_bgm1x.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../qt/BgmReplacer.h"
#include "catalog_command.h"

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
	bool ramSongs = false;  // "RAM 곡 읽기"
} s_opts;

// 분류 화면의 재생: GBA 는 롬 사본으로 MP2K 재생기, GB/GBC 는 게임 사운드 엔진 재생기 (게임이 멈춰 있어도 낸다)
class LibretroPreview final : public CatalogPreview {
public:
	// s_mutex 를 잡은 채로 부른다 (s_bgm 을 읽는다)
	bool play(const std::string& key, std::string& error) override;
	void stop() override {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_eng.reset();
		m_gsc.reset();
		m_rom.reset();
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
	double length() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_length;
	}
	int render(int16_t* stereo, int frames, int rate) override;

private:
	mutable std::mutex m_mutex;
	std::unique_ptr<Rom> m_rom;
	std::unique_ptr<Mp2kEngine> m_eng;
	std::unique_ptr<QGBA::GscPlayer> m_gsc;
	bool m_gscSfx = false;
	int m_gscCalls = 0;
	int m_rate = 0;
	bool m_playing = false, m_paused = false;
	double m_length = 0;
	uint32_t m_header = 0;
	std::shared_ptr<const RamImage> m_ramImg;
	bool m_ram = false;
	std::vector<float> m_buf;
} s_preview;

void applyOptions() {
	if (!s_bgm) {
		return;
	}
	s_bgm->setEnabled(s_opts.enabled && s_frontendMixes);
	s_bgm->setFastForwardCutscenes(s_opts.ffCutscenes);
	s_bgm->setSfx1x(s_opts.sfx1x);
	s_bgm->setSfxVolume(s_opts.sfxVolume);
	s_bgm->setRamSongs(s_opts.ramSongs);
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

void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume, bool ramSongs) {
	std::lock_guard<std::mutex> lock(s_mutex);
	s_opts.enabled = enabled;
	s_opts.ffCutscenes = ffCutscenes;
	s_opts.sfx1x = sfx1x;
	s_opts.sfxVolume = sfxVolume;
	s_opts.ramSongs = ramSongs;
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

// ---- 소리 분류 (안드로이드 앱) ----

namespace {

bool LibretroPreview::play(const std::string& key, std::string& error) {
	QGBA::BgmReplacer* bgm = s_bgm.get();
	if (!bgm) {
		error = "게임이 없습니다.";
		return false;
	}
	std::unique_ptr<Rom> rom;
	std::unique_ptr<QGBA::GscPlayer> gsc;
	bool sfx = false, ram = false;
	uint32_t header = 0;
	std::shared_ptr<const RamImage> ramImg;
	double length = 0;
	if (QGBA::GbDriverBgm* gb = bgm->gbDriver()) {
		const QGBA::GscTables& tables = gb->profile().tables;
		const bool song = key.rfind("gbsong:", 0) == 0;
		const bool cry = key.rfind("cry:", 0) == 0;
		sfx = key.rfind("gbsfx:", 0) == 0 || cry;
		if (!song && !sfx) {
			error = "알 수 없는 이름표입니다.";
			return false;
		}
		gsc.reset(new QGBA::GscPlayer(gb->rom(), tables, false));
		if (cry) {
			int id = -1, pitch = 0, len = 0;
			if (!tables.cries || !gb->lastCry(id, pitch, len)) {
				error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
				return false;
			}
			gsc->requestCry((uint16_t) id, (uint16_t) pitch, (uint16_t) len, nullptr);
		} else if (song) {
			gsc->requestSong((uint16_t) std::atoi(key.c_str() + 7));
		} else {
			gsc->requestSfx((uint16_t) std::atoi(key.c_str() + 6));
		}
	} else if (bgm->isGba()) {
		rom.reset(new Rom());
		rom->data = bgm->rom().data;
		if (key.rfind("song:", 0) == 0) {
			header = (uint32_t) std::strtoul(key.c_str() + 5, nullptr, 16);
			if (!rom->valid(header, 8) || rom->u8(header) == 0) {
				error = "이 게임의 롬에서 이 곡을 찾지 못했습니다.";
				return false;
			}
			length = isLoopingSong(*rom, header) ? 0 : songSeconds(*rom, header);
		} else if (key.rfind("cry:", 0) == 0) {
			if (!bgm->liveMirror().lastRamSong(header, ramImg)) {
				error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
				return false;
			}
			ram = true;
		} else {
			error = "알 수 없는 이름표입니다.";
			return false;
		}
	} else {
		error = "이 게임의 소리는 알아볼 수 없습니다.";
		return false;
	}
	const SoundInfoSnapshot si = bgm->isGba() ? bgm->liveMirror().lastSoundInfo() : SoundInfoSnapshot();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_eng.reset();
	m_gsc = std::move(gsc);
	m_rom = std::move(rom);
	m_gscSfx = sfx;
	m_gscCalls = 0;
	m_header = header;
	m_ram = ram;
	m_ramImg = std::move(ramImg);
	m_rate = 0;  // 재생기는 출력 표본율을 알게 되는 첫 render 에서 만든다
	m_playing = true;
	m_paused = false;
	m_length = length;
	if (m_rom || m_ram) {
		// GBA 재생기: 롬 사본(RAM 곡이면 지금 게임 롬)을 읽는다
		if (!m_rom) {
			m_rom.reset(new Rom());
			m_rom->data = bgm->rom().data;
		}
		m_eng.reset(new Mp2kEngine(m_rom.get()));
		if (si.valid) m_eng->setRuntimeParams(si.maxChans, si.masterVolume, si.reverb, si.samplesPerVBlank, si.dmaPeriod);
	}
	return true;
}

int LibretroPreview::render(int16_t* stereo, int frames, int rate) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_playing || m_paused || frames <= 0 || rate <= 0) return 0;
	if (m_gsc) {
		m_gsc->mixInto(stereo, frames, rate, 0x100);
		if (m_gscSfx && ++m_gscCalls > 4 && !m_gsc->sfxPlaying()) m_playing = false;
		return frames;
	}
	if (!m_eng) return 0;
	if (rate != m_rate) {
		// 처음(또는 표본율이 바뀜): 맞춰 설정하고 처음부터
		EngineConfig cfg;
		cfg.outRate = rate;
		m_eng->configure(cfg);
		if (m_ram) m_eng->startWithRam(m_header, m_ramImg);
		else m_eng->start(m_header);
		m_rate = rate;
	}
	if (!m_eng->currentSong() || m_eng->finished()) {
		m_playing = false;
		return 0;
	}
	if (m_buf.size() < (size_t) frames * 2) m_buf.resize((size_t) frames * 2);
	m_eng->render(m_buf.data(), frames);
	const float scale = 512.0f * 0x100 * 3.0f / 16.0f;  // mGBA 기본 볼륨으로 int16 에 섞을 때와 같은 크기
	for (int i = 0; i < frames * 2; ++i) {
		const float v = stereo[i] + m_buf[i] * scale;
		stereo[i] = (int16_t) std::max(-32768.0f, std::min(32767.0f, v));
	}
	return frames;
}

}

extern "C" {

RETRO_API const char* ff_catalog_command(const char* cmd) {
	static std::string result;
	std::lock_guard<std::mutex> lock(s_mutex);
	SoundCatalog* cat = s_bgm ? &s_bgm->catalog() : nullptr;
	const bool usable = s_bgm && (s_bgm->isGba() || s_bgm->gbDriver());
	result = catalogCommand(cat, &s_preview, usable, cmd ? cmd : "");
	return result.c_str();
}

RETRO_API int ff_preview_render(int16_t* stereo, int frames, int sampleRate) {
	return s_preview.render(stereo, frames, sampleRate);
}

}
