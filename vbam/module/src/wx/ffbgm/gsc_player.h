/* ff-bgm1x: GB 2세대 사운드 엔진의 자체 재생기 (GB/GBC BGM·효과음 1배속).
 * 게임 롬의 곡·효과음 데이터를 게임의 사운드 엔진(pret 의 GB 2세대 디컴파일 소스 audio/engine.asm)과 같은 규칙으로 읽어,
 * 직접 만든 GB 사운드 합성기로 낸다. 에뮬레이터 코어를 쓰지 않으므로 mGBA·VBA-M·Gambatte 어디에나 붙일 수 있다.
 * 게임 시간(프레임)과 무관하게 실제 시간으로 진행하므로 배속 중에도 1배속으로 들린다. */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <vector>

namespace QGBA {

// 엔진이 쓰는 롬 안의 표 (모두 engineBank 안의 주소)
struct GscTables {
	uint8_t engineBank = 0;
	uint16_t music = 0;      // 곡 포인터 표 (3바이트: bank, 주소)
	uint16_t sfx = 0;        // 효과음 포인터 표
	uint16_t cries = 0;      // 울음소리 포인터 표 (없으면 0: 울음소리 1배속을 하지 않는다)
	uint16_t frequency = 0;  // 음 높이 표
	uint16_t wave = 0;       // 파형 표 (16바이트씩)
	uint16_t drumkits = 0;   // 드럼(노이즈) 세트 포인터 표
	uint16_t lowHealthAlarm = 0;  // wLowHealthAlarm (RAM, 비트 7 = 낮은 체력 경고음 켜짐)
	bool valid() const { return engineBank && music && sfx && frequency && wave && drumkits; }
};

// 롬에서 표를 찾는다 (엔진 코드의 바이트 패턴). 못 찾으면 false
bool findGscTables(const std::vector<uint8_t>& rom, GscTables& out);

// GB 사운드 합성기 (레지스터 쓰기를 받아 소리를 만든다)
class GbApuSynth {
public:
	GbApuSynth();
	void write(uint8_t reg, uint8_t value);  // reg = FF00 기준 하위 바이트 (0x10~0x3F)
	uint8_t read(uint8_t reg) const;
	// cycles(4.19MHz 기준)만큼 진행하며 그 동안의 평균 출력을 L/R 로 돌려준다 (int16 범위의 실수)
	void run(double cycles, float& left, float& right);
	void setMutedChannels(uint8_t mask) { m_muted = mask; }
	void setOutputOverride(int nr50, int nr51) { m_overrideNr50 = nr50; m_overrideNr51 = nr51; }
	// 채널별 볼륨 배율 (1 = 그대로)
	void setChannelGain(int ch, float gain) { m_gain[ch & 3] = gain; }

private:
	struct Envelope {
		int initial = 0, dir = 0, period = 0;
		int volume = 0, timer = 0;
		bool dac() const { return initial || dir; }
	};
	struct Square {
		bool on = false;
		int duty = 0, step = 0;
		int freq = 0;
		double timer = 0;
		int length = 0;
		bool lengthOn = false;
		Envelope env;
		// 스윕 (1번 채널)
		int sweepPeriod = 0, sweepNeg = 0, sweepShift = 0, sweepTimer = 0, shadow = 0;
		bool sweepOn = false;
	};
	struct Wave {
		bool on = false, dac = false;
		int level = 0, freq = 0, pos = 0;
		double timer = 0;
		int length = 0;
		bool lengthOn = false;
	};
	struct Noise {
		bool on = false;
		int shift = 0, width7 = 0, divisor = 0;
		uint16_t lfsr = 0x7FFF;
		double timer = 0;
		int length = 0;
		bool lengthOn = false;
		Envelope env;
	};

	void trigger(int ch);
	void clockLength();
	void clockSweep();
	void clockEnvelope();
	int sweepCalc(Square& s);
	int squareOut(const Square& s) const;
	int waveOut() const;
	int noiseOut() const;

	std::array<uint8_t, 0x40> m_regs{};
	Square m_sq[2];
	Wave m_wave;
	Noise m_noise;
	std::array<uint8_t, 16> m_waveRam{};
	double m_seqTimer = 0;
	int m_seqStep = 0;
	uint8_t m_muted = 0;
	int m_overrideNr50 = -1, m_overrideNr51 = -1;
	float m_gain[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

class GscPlayer {
public:
	GscPlayer(const std::vector<uint8_t>& rom, const GscTables& tables, bool recording);
	bool ok() const { return m_tables.valid(); }

	void requestSong(uint16_t id);  // 0 이면 소리를 끈다 (게임의 PlayMusic 과 같음)
	// 효과음: 앞의 효과음이 아직 울리면 번호가 더 작을(우선순위가 높을) 때만 끊고 낸다 (게임의 PlaySFX 규칙)
	void requestSfx(uint16_t id);
	// 울음소리 (게임의 _PlayCry): 원본이 정한 음높이 보정(wCryPitch)·길이(wCryLength)와,
	// 하드웨어 채널별 좌우 출력(전투에서 한쪽으로 내는 값, 모르면 -1)
	void requestCry(uint16_t id, uint16_t pitch, uint16_t length, const int tracks[4]);
	void setMutedChannels(uint8_t mask) { m_mutedMask = mask; }
	void setOutputRegs(uint8_t nr50, uint8_t nr51) { m_outRegs = (nr50 << 8) | nr51; }
	void setOptions(uint8_t value) { m_options = value; }
	// 원본의 wLowHealthAlarm 비트 7. 경고음 박자(삐-삐)는 이 재생기가 실제 시간으로 낸다
	void setDanger(bool on) { m_dangerReq = on; }
	// 소리 분류 창의 볼륨: 곡(음악 채널)과 효과음(효과음 채널)에 따로 곱한다 (1 = 그대로)
	void setGains(float music, float sfx) {
		m_musicGain = music;
		m_sfxGain = sfx;
	}
	// 효과음이 아직 울리는지 (미리 듣기에서 끝을 알 때)
	bool sfxPlaying() const { return sfxBusy(); }

	// 녹화용: 게임 프레임 하나만큼 진행하고 녹화 표본율(131072Hz)로 만든 소리를 쌓는다
	void runFrame();
	bool popStreamSample(int16_t& left, int16_t& right);
	// 실시간용: sampleRate 로 frames 개를 만들어 stereo 에 더한다 (volume 0x100 = 그대로)
	void mixInto(int16_t* stereo, int frames, int sampleRate, int volume);

private:
	// ---- 사운드 엔진 (audio/engine.asm 과 같은 동작) ----
	struct Channel {
		uint16_t musicId = 0;
		uint8_t musicBank = 0;
		uint8_t flags1 = 0, flags2 = 0, flags3 = 0;
		uint16_t musicAddress = 0, lastMusicAddress = 0;
		uint8_t noteFlags = 0;
		uint8_t condition = 0;
		uint8_t dutyCycle = 0;
		uint8_t volumeEnvelope = 0;
		uint16_t frequency = 0;
		uint8_t pitch = 0, octave = 0, transposition = 0;
		uint8_t noteDuration = 0;
		uint8_t loopCount = 0;
		uint16_t tempo = 0;
		uint8_t tracks = 0;
		uint8_t dutyCyclePattern = 0;
		uint8_t vibratoDelayCount = 0, vibratoDelay = 0, vibratoExtent = 0, vibratoRate = 0;
		uint16_t pitchSlideTarget = 0;
		uint8_t pitchSlideAmount = 0, pitchSlideAmountFraction = 0, field25 = 0;
		uint16_t pitchOffset = 0;
		uint8_t noteDurationModifier = 0;
		uint8_t noteLength = 0;
	};

	uint8_t romByte(uint8_t bank, uint16_t addr) const;
	uint16_t romWord(uint8_t bank, uint16_t addr) const;

	void initSound();
	void updateSound();  // 프레임마다
	void updateChannels(Channel& c);
	void playDanger();
	void clearChannel(int hw);
	void loadNote(Channel& c);
	void handleTrackVibrato(Channel& c);
	void applyPitchSlide(Channel& c);
	void handleNoise(Channel& c);
	void readNoiseSample(Channel& c);
	void parseMusic(Channel& c);
	void parseSfxOrCry(Channel& c);
	void getNoiseSample(Channel& c);
	void parseMusicCommand(Channel& c);
	uint8_t getMusicByte(Channel& c);
	uint16_t getFrequency(Channel& c, uint8_t octave, uint8_t pitch);
	void setNoteDuration(Channel& c, uint8_t a);
	void setGlobalTempo(uint16_t tempo);
	void startChannel(Channel& c);
	void setLRTracks(Channel& c);
	void playMusic(uint16_t id);
	void playSfx(uint16_t id);
	void playCry(uint16_t id, uint16_t pitch, uint16_t length, const int tracks[4]);
	Channel& loadChannel(uint8_t bank, uint16_t& addr);
	bool sfxBusy() const;
	bool stereo() const;
	void apu(uint8_t reg, uint8_t value) { m_apu.write(reg, value); }

	void applyRequests();
	void render(int frames, int sampleRate, float* out);

	std::vector<uint8_t> m_rom;
	GscTables m_tables;
	bool m_recording;
	GbApuSynth m_apu;

	Channel m_ch[8];
	int m_curChannel = 0;
	uint8_t m_soundOutput = 0;
	uint8_t m_curTrackDuty = 0, m_curTrackVolumeEnvelope = 0;
	uint16_t m_curTrackFrequency = 0;
	uint8_t m_curNoteDuration = 0, m_curMusicByte = 0;
	bool m_musicPlaying = false;
	uint8_t m_volume = 0x77;
	uint8_t m_lastVolume = 0;  // 울음소리가 최대 음량으로 나는 동안 저장해 둔 음량 (wLastVolume)
	uint8_t m_sfxPriority = 0;
	uint8_t m_pitchSweep = 0;
	uint16_t m_noiseSampleAddress = 0;
	uint8_t m_noiseSampleDelay = 0;
	uint8_t m_musicNoiseSampleSet = 0, m_sfxNoiseSampleSet = 0;
	uint16_t m_musicId = 0;
	uint8_t m_musicBank = 0;
	uint8_t m_curSfx = 0;
	bool m_dangerOn = false;
	uint8_t m_dangerCount = 0;
	std::atomic<bool> m_dangerReq{false};

	// ---- 요청 (다른 스레드에서 들어온다) ----
	std::atomic<int> m_pending{-1};
	std::atomic<int> m_pendingSfx{-1};
	std::atomic<int> m_pendingCry{-1};
	std::atomic<int> m_cryPitch{0}, m_cryLength{0};
	std::atomic<int> m_cryTracks[4] = {{-1}, {-1}, {-1}, {-1}};
	std::atomic<uint8_t> m_mutedMask{0};
	std::atomic<int> m_outRegs{-1};
	std::atomic<int> m_options{0};
	std::atomic<float> m_musicGain{1.0f};
	std::atomic<float> m_sfxGain{1.0f};

	// ---- 시간 ----
	double m_frameCycles = 0;  // 다음 엔진 프레임까지 남은 사이클
	std::deque<int16_t> m_streamFifo;
	float m_capL = 0, m_capR = 0;  // 직류 제거 필터
	double m_recCarry = 0;
};

}
