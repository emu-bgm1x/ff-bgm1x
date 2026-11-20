/* ff-bgm1x: 소리 분류 창의 재생. 설명은 FfSoundPreview.h */
#include "FfSoundPreview.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <SDL.h>

using namespace QGBA;

FfSoundPreview::FfSoundPreview() = default;

FfSoundPreview::~FfSoundPreview() {
	if (m_device) {
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}

bool FfSoundPreview::openDevice(std::string& error) {
	if (m_device) {
		return true;
	}
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
		error = std::string("오디오 장치를 열 수 없습니다: ") + SDL_GetError();
		return false;
	}
	SDL_AudioSpec want{}, have{};
	want.freq = 48000;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = &FfSoundPreview::callback;
	want.userdata = this;
	m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!m_device) {
		error = std::string("오디오 장치를 열 수 없습니다: ") + SDL_GetError();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}
	m_rate = have.freq > 0 ? have.freq : 48000;
	return true;
}

bool FfSoundPreview::play(const Rom& game, const BgmMultiMirror& mirror, const std::string& key, std::string& error) {
	if (game.data.empty()) {
		error = "게임이 없습니다.";
		return false;
	}
	if (!openDevice(error)) {
		return false;
	}
	SDL_PauseAudioDevice(m_device, 1);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_engine.reset();
		m_gsc.reset();
		m_playing = false;
		if (!m_rom || m_rom->data != game.data) {
			m_rom.reset(new Rom());
			m_rom->data = game.data;
		}
	}
	const Rom* rom = m_rom.get();
	std::unique_ptr<Mp2kEngine> eng(new Mp2kEngine(rom));
	EngineConfig cfg;
	cfg.outRate = m_rate;
	eng->configure(cfg);
	const SoundInfoSnapshot si = mirror.lastSoundInfo();
	if (si.valid) {
		eng->setRuntimeParams(si.maxChans, si.masterVolume, si.reverb, si.samplesPerVBlank, si.dmaPeriod);
	}
	double length = 0;
	if (key.rfind("song:", 0) == 0) {
		const uint32_t header = (uint32_t) std::strtoul(key.c_str() + 5, nullptr, 16);
		if (!rom->valid(header, 8) || rom->u8(header) == 0) {
			error = "이 게임의 롬에서 이 곡을 찾지 못했습니다.";
			return false;
		}
		eng->start(header);
		length = isLoopingSong(*rom, header) ? 0 : songSeconds(*rom, header);
	} else if (key.rfind("cry:", 0) == 0) {
		uint32_t header = 0;
		std::shared_ptr<const RamImage> ram;
		if (!mirror.lastRamSong(header, ram)) {
			error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
			return false;
		}
		eng->startWithRam(header, std::move(ram));
	} else {
		error = "알 수 없는 이름표입니다.";
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_engine = std::move(eng);
		m_playing = true;
		m_paused = false;
		m_length = length;
	}
	SDL_PauseAudioDevice(m_device, 0);
	return true;
}

bool FfSoundPreview::playGb(const std::vector<uint8_t>& rom, const GscTables& tables, const std::string& key,
                            std::string& error, int cryId, int cryPitch, int cryLength) {
	const bool song = key.rfind("gbsong:", 0) == 0;
	const bool cry = key.rfind("cry:", 0) == 0;
	const bool sfx = key.rfind("gbsfx:", 0) == 0 || cry;
	if (!song && !sfx) {
		error = "알 수 없는 이름표입니다.";
		return false;
	}
	if (cry && (cryId < 0 || !tables.cries)) {
		error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
		return false;
	}
	if (!tables.valid()) {
		error = "이 게임의 사운드 엔진 표를 찾지 못했습니다.";
		return false;
	}
	if (!openDevice(error)) {
		return false;
	}
	SDL_PauseAudioDevice(m_device, 1);
	std::unique_ptr<GscPlayer> player(new GscPlayer(rom, tables, false));
	if (cry) {
		player->requestCry((uint16_t) cryId, (uint16_t) cryPitch, (uint16_t) cryLength, nullptr);
	} else if (song) {
		player->requestSong((uint16_t) std::atoi(key.c_str() + 7));
	} else {
		player->requestSfx((uint16_t) std::atoi(key.c_str() + 6));
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_engine.reset();
		m_gsc = std::move(player);
		m_gscSfx = sfx;
		m_gscFrames = 0;
		m_playing = true;
		m_paused = false;
		m_length = 0;
	}
	SDL_PauseAudioDevice(m_device, 0);
	return true;
}

void FfSoundPreview::stop() {
	if (m_device) {
		SDL_PauseAudioDevice(m_device, 1);
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_engine.reset();
	m_gsc.reset();
	m_playing = false;
	m_paused = false;
}

void FfSoundPreview::setPaused(bool paused) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_playing) {
		return;
	}
	m_paused = paused;
	if (m_device) {
		SDL_PauseAudioDevice(m_device, paused ? 1 : 0);
	}
}

bool FfSoundPreview::isPlaying() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_playing;
}

bool FfSoundPreview::isPaused() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_playing && m_paused;
}

void FfSoundPreview::callback(void* user, uint8_t* stream, int len) {
	FfSoundPreview* self = static_cast<FfSoundPreview*>(user);
	float* out = reinterpret_cast<float*>(stream);
	const int frames = len / (int) (sizeof(float) * 2);
	std::memset(stream, 0, (size_t) len);
	std::lock_guard<std::mutex> lock(self->m_mutex);
	if (self->m_gsc && self->m_playing && !self->m_paused) {
		// GB: int16 로 만들어 float 로 바꾼다
		std::vector<int16_t> pcm((size_t) frames * 2, 0);
		self->m_gsc->mixInto(pcm.data(), frames, self->m_rate, 0x100);
		for (int i = 0; i < frames * 2; ++i) {
			out[i] = pcm[i] / 32768.0f;
		}
		// 효과음은 끝나면 멈춘다 (시작 직후 몇 번은 아직 채널이 켜지기 전일 수 있다)
		if (self->m_gscSfx && ++self->m_gscFrames > 4 && !self->m_gsc->sfxPlaying()) {
			self->m_playing = false;
		}
		return;
	}
	if (!self->m_engine || !self->m_playing || self->m_paused) {
		return;
	}
	if (!self->m_engine->currentSong() || self->m_engine->finished()) {
		// 반복 없는 소리가 끝났다 (남은 울림은 짧아 그대로 끊는다)
		self->m_playing = false;
		return;
	}
	self->m_engine->render(out, frames);
	// 재생기 출력 1.0 = GBA 믹서 512 단위. mGBA 기본 볼륨(0x100)으로 int16 에 섞을 때와 같은 크기로 맞춘다
	const float scale = 512.0f * 0x100 * 3.0f / 16.0f / 32768.0f;
	for (int i = 0; i < frames * 2; ++i) {
		out[i] *= scale;
	}
}
