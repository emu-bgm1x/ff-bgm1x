/* ff-bgm1x: GB 2세대 사운드 엔진의 자체 재생기. 설명은 GscPlayer.h
 * 엔진 부분은 pret 의 GB 2세대 디컴파일 소스 audio/engine.asm 을 한 줄씩 옮긴 것이다 (함수 이름도 같다). */
#include "gsc_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace QGBA {

namespace {

constexpr double kClock = 4194304.0;              // GB 사운드 클럭 (Hz)
constexpr double kFrameCycles = 70224.0;           // 한 화면 프레임 (엔진은 프레임마다 한 번 갱신)
constexpr int kRecordRate = 131072;                // mGBA GB 오디오의 녹화 스트림 표본율
constexpr uint8_t kFirstMusicCmd = 0xD0;
constexpr uint8_t kSoundRet = 0xFF;
constexpr uint8_t kMaxVolume = 0x77;
constexpr int kNumMusicChans = 4;
constexpr int kNoiseChanBit = 2;                   // NOISE_CHAN_F: 채널 번호 4~7 에서 켜지는 비트

// CHANNEL_FLAGS1
constexpr uint8_t SOUND_CHANNEL_ON = 1 << 0;
constexpr uint8_t SOUND_SUBROUTINE = 1 << 1;
constexpr uint8_t SOUND_LOOPING = 1 << 2;
constexpr uint8_t SOUND_SFX = 1 << 3;
constexpr uint8_t SOUND_NOISE = 1 << 4;
constexpr uint8_t SOUND_CRY = 1 << 5;
// CHANNEL_FLAGS2
constexpr uint8_t SOUND_VIBRATO = 1 << 0;
constexpr uint8_t SOUND_PITCH_SLIDE = 1 << 1;
constexpr uint8_t SOUND_DUTY_LOOP = 1 << 2;
constexpr uint8_t SOUND_PITCH_OFFSET = 1 << 4;
// CHANNEL_FLAGS3
constexpr uint8_t SOUND_VIBRATO_DIR = 1 << 0;
constexpr uint8_t SOUND_PITCH_SLIDE_DIR = 1 << 1;
// CHANNEL_NOTE_FLAGS
constexpr uint8_t NOTE_DUTY_OVERRIDE = 1 << 0;
constexpr uint8_t NOTE_FREQ_OVERRIDE = 1 << 1;
constexpr uint8_t NOTE_PITCH_SWEEP = 1 << 3;
constexpr uint8_t NOTE_NOISE_SAMPLING = 1 << 4;
constexpr uint8_t NOTE_REST = 1 << 5;
constexpr uint8_t NOTE_VIBRATO_OVERRIDE = 1 << 6;

constexpr uint8_t kMonoTracks[4] = {0x11, 0x22, 0x44, 0x88};

// 레지스터 (FF00 기준)
enum : uint8_t {
	NR10 = 0x10, NR11, NR12, NR13, NR14,
	NR21 = 0x16, NR22, NR23, NR24,
	NR30 = 0x1A, NR31, NR32, NR33, NR34,
	NR41 = 0x20, NR42, NR43, NR44,
	NR50 = 0x24, NR51, NR52,
	WAVE0 = 0x30,
};

constexpr int ANY = -1;
bool matchAt(const std::vector<uint8_t>& rom, size_t at, const std::vector<int>& pat) {
	if (at + pat.size() > rom.size()) {
		return false;
	}
	for (size_t i = 0; i < pat.size(); ++i) {
		if (pat[i] != ANY && rom[at + i] != pat[i]) {
			return false;
		}
	}
	return true;
}

std::vector<size_t> findAll(const std::vector<uint8_t>& rom, const std::vector<int>& pat) {
	std::vector<size_t> out;
	for (size_t a = 0; a + pat.size() <= rom.size(); ++a) {
		if (matchAt(rom, a, pat)) {
			out.push_back(a);
		}
	}
	return out;
}

uint16_t word(const std::vector<uint8_t>& rom, size_t at) {
	return rom[at] | (rom[at + 1] << 8);
}

}  // namespace

bool findGscTables(const std::vector<uint8_t>& rom, GscTables& out) {
	out = GscTables();
	// _PlayMusic / _PlayCry / _PlaySFX / PlayStereoSFX 가 차례로:
	//   ld hl,wMusicID / ld [hl],e / inc hl / ld [hl],d / ld hl,TABLE / add hl,de x3 / ld a,[hli] / ld [wMusicBank],a
	const std::vector<int> tableLoad = {0x21, ANY, ANY, 0x73, 0x23, 0x72, 0x21, ANY, ANY, 0x19, 0x19, 0x19, 0x2A, 0xEA, ANY, ANY};
	std::vector<size_t> hits = findAll(rom, tableLoad);
	if (hits.size() < 3) {
		return false;
	}
	const size_t bankOf = hits[0] / 0x4000;
	for (size_t h : hits) {
		if (h / 0x4000 != bankOf) {
			return false;
		}
	}
	out.engineBank = static_cast<uint8_t>(bankOf);
	out.music = word(rom, hits[0] + 7);
	out.sfx = word(rom, hits[2] + 7);
	out.cries = word(rom, hits[1] + 7);  // _PlayCry 의 ld hl,Cries
	auto inEngine = [&](size_t at) { return at / 0x4000 == bankOf; };
	// GetFrequency: add hl,hl / ld de,FrequencyTable / add hl,de / ld e,[hl] / inc hl / ld d,[hl]
	for (size_t h : findAll(rom, {0x29, 0x11, ANY, ANY, 0x19, 0x5E, 0x23, 0x56})) {
		if (inEngine(h)) {
			out.frequency = word(rom, h + 2);
			break;
		}
	}
	// .load_wave_pattern: add hl,hl x4 / ld de,WaveSamples / add hl,de / ld a,[hli] / ldh [rAUD3WAVE_0],a
	for (size_t h : findAll(rom, {0x29, 0x29, 0x29, 0x29, 0x11, ANY, ANY, 0x19, 0x2A, 0xE0, 0x30})) {
		if (inEngine(h)) {
			out.wave = word(rom, h + 5);
			break;
		}
	}
	// GetNoiseSample: ld hl,Drumkits / add hl,de / add hl,de / ld a,[hli] / ld h,[hl] / ld l,a / ld a,[wCurMusicByte]
	for (size_t h : findAll(rom, {0x21, ANY, ANY, 0x19, 0x19, 0x2A, 0x66, 0x6F, 0xFA})) {
		if (inEngine(h)) {
			out.drumkits = word(rom, h + 1);
			break;
		}
	}
	// PlayDanger: ld a,[wLowHealthAlarm] / bit DANGER_ON_F,a / ret z
	for (size_t h : findAll(rom, {0xFA, ANY, ANY, 0xCB, 0x7F, 0xC8})) {
		if (inEngine(h)) {
			out.lowHealthAlarm = word(rom, h + 1);
			break;
		}
	}
	return out.valid();
}

// ================================================================ GB 사운드 합성기

GbApuSynth::GbApuSynth() {
	m_regs[NR50] = 0x77;
	m_regs[NR51] = 0xFF;
	m_regs[NR52] = 0x80;
}

uint8_t GbApuSynth::read(uint8_t reg) const {
	return m_regs[reg & 0x3F];
}

void GbApuSynth::write(uint8_t reg, uint8_t v) {
	reg &= 0x3F;
	if (reg >= WAVE0) {
		m_waveRam[reg - WAVE0] = v;
		return;
	}
	m_regs[reg] = v;
	switch (reg) {
	case NR10:
		m_sq[0].sweepPeriod = (v >> 4) & 7;
		m_sq[0].sweepNeg = (v >> 3) & 1;
		m_sq[0].sweepShift = v & 7;
		break;
	case NR11:
	case NR21: {
		Square& s = m_sq[reg == NR11 ? 0 : 1];
		s.duty = v >> 6;
		s.length = 64 - (v & 0x3F);
		break;
	}
	case NR12:
	case NR22:
	case NR42: {
		Envelope& e = reg == NR12 ? m_sq[0].env : reg == NR22 ? m_sq[1].env : m_noise.env;
		e.initial = v >> 4;
		e.dir = (v >> 3) & 1;
		e.period = v & 7;
		if (!e.dac()) {
			// DAC 가 꺼지면 채널도 꺼진다
			if (reg == NR12) m_sq[0].on = false;
			else if (reg == NR22) m_sq[1].on = false;
			else m_noise.on = false;
		}
		break;
	}
	case NR13:
	case NR23: {
		Square& s = m_sq[reg == NR13 ? 0 : 1];
		s.freq = (s.freq & 0x700) | v;
		break;
	}
	case NR14:
	case NR24: {
		Square& s = m_sq[reg == NR14 ? 0 : 1];
		s.freq = (s.freq & 0xFF) | ((v & 7) << 8);
		s.lengthOn = v & 0x40;
		if (v & 0x80) {
			trigger(reg == NR14 ? 0 : 1);
		}
		break;
	}
	case NR30:
		m_wave.dac = v & 0x80;
		if (!m_wave.dac) {
			m_wave.on = false;
		}
		break;
	case NR31:
		m_wave.length = 256 - v;
		break;
	case NR32:
		m_wave.level = (v >> 5) & 3;
		break;
	case NR33:
		m_wave.freq = (m_wave.freq & 0x700) | v;
		break;
	case NR34:
		m_wave.freq = (m_wave.freq & 0xFF) | ((v & 7) << 8);
		m_wave.lengthOn = v & 0x40;
		if (v & 0x80) {
			trigger(2);
		}
		break;
	case NR41:
		m_noise.length = 64 - (v & 0x3F);
		break;
	case NR43:
		m_noise.shift = v >> 4;
		m_noise.width7 = (v >> 3) & 1;
		m_noise.divisor = v & 7;
		break;
	case NR44:
		m_noise.lengthOn = v & 0x40;
		if (v & 0x80) {
			trigger(3);
		}
		break;
	default:
		break;
	}
}

void GbApuSynth::trigger(int ch) {
	if (ch < 2) {
		Square& s = m_sq[ch];
		s.on = s.env.dac();
		if (!s.length) {
			s.length = 64;
		}
		s.timer = (2048 - s.freq) * 4;
		s.env.volume = s.env.initial;
		s.env.timer = s.env.period ? s.env.period : 8;
		if (ch == 0) {
			s.shadow = s.freq;
			s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8;
			s.sweepOn = s.sweepPeriod || s.sweepShift;
			if (s.sweepShift && sweepCalc(s) > 2047) {
				s.on = false;
			}
		}
	} else if (ch == 2) {
		m_wave.on = m_wave.dac;
		if (!m_wave.length) {
			m_wave.length = 256;
		}
		m_wave.timer = (2048 - m_wave.freq) * 2;
		m_wave.pos = 0;
	} else {
		Noise& n = m_noise;
		n.on = n.env.dac();
		if (!n.length) {
			n.length = 64;
		}
		n.lfsr = 0x7FFF;
		n.env.volume = n.env.initial;
		n.env.timer = n.env.period ? n.env.period : 8;
		n.timer = (n.divisor ? n.divisor * 16 : 8) << n.shift;
	}
}

int GbApuSynth::sweepCalc(Square& s) {
	int delta = s.shadow >> s.sweepShift;
	return s.sweepNeg ? s.shadow - delta : s.shadow + delta;
}

void GbApuSynth::clockLength() {
	for (Square& s : m_sq) {
		if (s.lengthOn && s.length && !--s.length) {
			s.on = false;
		}
	}
	if (m_wave.lengthOn && m_wave.length && !--m_wave.length) {
		m_wave.on = false;
	}
	if (m_noise.lengthOn && m_noise.length && !--m_noise.length) {
		m_noise.on = false;
	}
}

void GbApuSynth::clockSweep() {
	Square& s = m_sq[0];
	if (--s.sweepTimer > 0) {
		return;
	}
	s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8;
	if (!s.sweepOn || !s.sweepPeriod) {
		return;
	}
	int f = sweepCalc(s);
	if (f > 2047) {
		s.on = false;
		return;
	}
	if (s.sweepShift) {
		s.shadow = f;
		s.freq = f;
		if (sweepCalc(s) > 2047) {
			s.on = false;
		}
	}
}

void GbApuSynth::clockEnvelope() {
	auto clock = [](Envelope& e) {
		if (!e.period) {
			return;
		}
		if (--e.timer > 0) {
			return;
		}
		e.timer = e.period;
		if (e.dir && e.volume < 15) {
			++e.volume;
		} else if (!e.dir && e.volume > 0) {
			--e.volume;
		}
	};
	clock(m_sq[0].env);
	clock(m_sq[1].env);
	clock(m_noise.env);
}

int GbApuSynth::squareOut(const Square& s) const {
	static const uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7E};
	return ((kDuty[s.duty] >> (7 - s.step)) & 1) ? s.env.volume : 0;
}

int GbApuSynth::waveOut() const {
	uint8_t b = m_waveRam[m_wave.pos >> 1];
	int nib = (m_wave.pos & 1) ? (b & 0xF) : (b >> 4);
	static const int kShift[4] = {4, 0, 1, 2};
	return nib >> kShift[m_wave.level];
}

int GbApuSynth::noiseOut() const {
	return (~m_noise.lfsr & 1) ? m_noise.env.volume : 0;
}

void GbApuSynth::run(double cycles, float& left, float& right) {
	// 채널마다 (디지털 값 - 7.5) 를 시간 평균한다. 꺼진 채널은 0 (켜고 끌 때 직류가 튀지 않게)
	double acc[4] = {0, 0, 0, 0};
	double remain = cycles;
	while (remain > 0) {
		// 다음 사건까지의 사이클
		double step = std::min(remain, m_seqTimer > 0 ? m_seqTimer : 8192.0);
		if (m_sq[0].on) step = std::min(step, m_sq[0].timer);
		if (m_sq[1].on) step = std::min(step, m_sq[1].timer);
		if (m_wave.on) step = std::min(step, m_wave.timer);
		if (m_noise.on) step = std::min(step, m_noise.timer);
		if (step <= 0) step = 1e-9;

		const double v0 = m_sq[0].on ? squareOut(m_sq[0]) - 7.5 : 0;
		const double v1 = m_sq[1].on ? squareOut(m_sq[1]) - 7.5 : 0;
		const double v2 = m_wave.on ? waveOut() - 7.5 : 0;
		const double v3 = m_noise.on ? noiseOut() - 7.5 : 0;
		acc[0] += v0 * step;
		acc[1] += v1 * step;
		acc[2] += v2 * step;
		acc[3] += v3 * step;
		remain -= step;

		for (int i = 0; i < 2; ++i) {
			Square& s = m_sq[i];
			if (!s.on) continue;
			s.timer -= step;
			while (s.timer <= 0) {
				s.timer += (2048 - s.freq) * 4;
				s.step = (s.step + 1) & 7;
			}
		}
		if (m_wave.on) {
			m_wave.timer -= step;
			while (m_wave.timer <= 0) {
				m_wave.timer += (2048 - m_wave.freq) * 2;
				m_wave.pos = (m_wave.pos + 1) & 31;
			}
		}
		if (m_noise.on) {
			Noise& n = m_noise;
			n.timer -= step;
			while (n.timer <= 0) {
				n.timer += (n.divisor ? n.divisor * 16 : 8) << n.shift;
				const uint16_t x = (n.lfsr ^ (n.lfsr >> 1)) & 1;
				n.lfsr = (n.lfsr >> 1) | (x << 14);
				if (n.width7) {
					n.lfsr = (n.lfsr & ~(1 << 6)) | (x << 6);
				}
			}
		}
		m_seqTimer -= step;
		if (m_seqTimer <= 0) {
			m_seqTimer += 8192;
			// 512Hz 프레임 시퀀서: 0,2,4,6 길이 / 2,6 스윕 / 7 엔벨로프
			if (!(m_seqStep & 1)) clockLength();
			if (m_seqStep == 2 || m_seqStep == 6) clockSweep();
			if (m_seqStep == 7) clockEnvelope();
			m_seqStep = (m_seqStep + 1) & 7;
		}
	}
	const int nr50 = m_overrideNr50 >= 0 ? m_overrideNr50 : m_regs[NR50];
	const int nr51 = m_overrideNr51 >= 0 ? m_overrideNr51 : m_regs[NR51];
	const double volL = ((nr50 >> 4) & 7) + 1;
	const double volR = (nr50 & 7) + 1;
	double l = 0, r = 0;
	for (int i = 0; i < 4; ++i) {
		if (m_muted & (1 << i)) continue;
		const double v = acc[i] / cycles * m_gain[i];
		if (nr51 & (0x10 << i)) l += v;
		if (nr51 & (0x01 << i)) r += v;
	}
	// mGBA 와 같은 크기: 채널 값 x 8 x (마스터 볼륨 + 1) x 12
	left = static_cast<float>(l * 96.0 * volL);
	right = static_cast<float>(r * 96.0 * volR);
}

// ================================================================ 사운드 엔진

GscPlayer::GscPlayer(const std::vector<uint8_t>& rom, const GscTables& tables, bool recording)
	: m_rom(rom)
	, m_tables(tables)
	, m_recording(recording)
{
	initSound();
}

uint8_t GscPlayer::romByte(uint8_t bank, uint16_t addr) const {
	size_t off;
	if (addr < 0x4000) {
		off = addr;
	} else if (addr < 0x8000) {
		off = static_cast<size_t>(bank) * 0x4000 + (addr - 0x4000);
	} else {
		return 0xFF;  // 롬 밖 (엔진 데이터는 모두 롬에 있다)
	}
	return off < m_rom.size() ? m_rom[off] : 0xFF;
}

uint16_t GscPlayer::romWord(uint8_t bank, uint16_t addr) const {
	return romByte(bank, addr) | (romByte(bank, addr + 1) << 8);
}

bool GscPlayer::stereo() const {
	return (m_options.load() >> 5) & 1;  // wOptions 의 STEREO 비트
}

void GscPlayer::initSound() {
	m_musicPlaying = false;
	apu(NR50, 0);
	apu(NR51, 0);
	apu(NR52, 0x80);
	for (int i = 0; i < kNumMusicChans; ++i) {
		clearChannel(i);
	}
	for (Channel& c : m_ch) {
		c = Channel();
	}
	m_soundOutput = 0;
	m_sfxPriority = 0;
	m_pitchSweep = 0;
	m_noiseSampleAddress = 0;
	m_noiseSampleDelay = 0;
	m_musicNoiseSampleSet = 0;
	m_sfxNoiseSampleSet = 0;
	m_musicId = 0;
	m_musicBank = 0;
	m_volume = kMaxVolume;
	m_musicPlaying = true;
}

void GscPlayer::clearChannel(int hw) {
	const uint8_t base = static_cast<uint8_t>(NR10 + hw * 5);
	apu(base, 0);
	apu(base + 1, 0);
	apu(base + 2, 0x08);
	apu(base + 3, 0);
	apu(base + 4, 0x80);
}

void GscPlayer::updateSound() {
	if (!m_musicPlaying) {
		return;
	}
	m_soundOutput = 0;
	for (m_curChannel = 0; m_curChannel < 8; ++m_curChannel) {
		Channel& c = m_ch[m_curChannel];
		if (!(c.flags1 & SOUND_CHANNEL_ON)) {
			continue;
		}
		if (c.noteDuration < 2) {
			c.vibratoDelayCount = c.vibratoDelay;
			c.flags2 &= ~SOUND_PITCH_SLIDE;
			parseMusic(c);
		} else {
			--c.noteDuration;
		}
		applyPitchSlide(c);
		m_curTrackDuty = c.dutyCycle;
		m_curTrackVolumeEnvelope = c.volumeEnvelope;
		m_curTrackFrequency = c.frequency;
		handleTrackVibrato(c);
		handleNoise(c);
		if (m_sfxPriority && m_curChannel < kNumMusicChans) {
			for (int s = 4; s < 8; ++s) {
				if (m_ch[s].flags1 & SOUND_CHANNEL_ON) {
					c.noteFlags |= NOTE_REST;
					break;
				}
			}
		}
		const bool sfxChannel = m_curChannel >= kNumMusicChans;
		if (sfxChannel || !(m_ch[m_curChannel + kNumMusicChans].flags1 & SOUND_CHANNEL_ON)) {
			updateChannels(c);
			m_soundOutput |= c.tracks;
		}
		c.noteFlags = 0;
	}
	playDanger();
	// FadeMusic: 페이드는 원본의 NR50 을 따라가므로 따로 하지 않는다
	apu(NR50, m_volume);
	apu(NR51, m_soundOutput);
}

void GscPlayer::updateChannels(Channel& c) {
	const int hw = m_curChannel & 3;
	const uint8_t f = c.noteFlags;
	switch (hw) {
	case 0:
		// .Channel1: 경고음이 켜져 있으면 음악 1번 채널은 하드웨어에 쓰지 않는다 (효과음 5번 채널은 그대로)
		if (m_curChannel == 0 && m_dangerOn) {
			return;
		}
		if (f & NOTE_PITCH_SWEEP) {
			apu(NR10, m_pitchSweep);
		}
		if (f & NOTE_REST) {
			clearChannel(0);
			return;
		}
		if (f & NOTE_NOISE_SAMPLING) {
			apu(NR11, 0x3F | m_curTrackDuty);
			apu(NR12, m_curTrackVolumeEnvelope);
			apu(NR13, m_curTrackFrequency & 0xFF);
			apu(NR14, (m_curTrackFrequency >> 8) | 0x80);
			return;
		}
		if (f & NOTE_FREQ_OVERRIDE) {
			apu(NR13, m_curTrackFrequency & 0xFF);
			apu(NR14, m_curTrackFrequency >> 8);
			if (f & NOTE_DUTY_OVERRIDE) {
				apu(NR11, (m_apu.read(NR11) & 0x3F) | m_curTrackDuty);
			}
			return;
		}
		if (f & NOTE_VIBRATO_OVERRIDE) {
			apu(NR11, (m_apu.read(NR11) & 0x3F) | m_curTrackDuty);
			apu(NR13, m_curTrackFrequency & 0xFF);
			return;
		}
		if (f & NOTE_DUTY_OVERRIDE) {
			apu(NR11, (m_apu.read(NR11) & 0x3F) | m_curTrackDuty);
		}
		return;
	case 1:
		if (f & NOTE_REST) {
			clearChannel(1);
			return;
		}
		if (f & NOTE_NOISE_SAMPLING) {
			apu(NR21, 0x3F | m_curTrackDuty);
			apu(NR22, m_curTrackVolumeEnvelope);
			apu(NR23, m_curTrackFrequency & 0xFF);
			apu(NR24, (m_curTrackFrequency >> 8) | 0x80);
			return;
		}
		if (f & NOTE_VIBRATO_OVERRIDE) {
			apu(NR21, (m_apu.read(NR21) & 0x3F) | m_curTrackDuty);
			apu(NR23, m_curTrackFrequency & 0xFF);
			return;
		}
		if (f & NOTE_DUTY_OVERRIDE) {
			apu(NR21, (m_apu.read(NR21) & 0x3F) | m_curTrackDuty);
		}
		return;
	case 2:
		if (f & NOTE_REST) {
			clearChannel(2);
			return;
		}
		if (f & NOTE_NOISE_SAMPLING) {
			apu(NR31, 0x3F);
			apu(NR30, 0);
			const uint16_t waveAddr = static_cast<uint16_t>(m_tables.wave + (m_curTrackVolumeEnvelope & 0xF) * 16);
			for (int i = 0; i < 16; ++i) {
				apu(static_cast<uint8_t>(WAVE0 + i), romByte(m_tables.engineBank, static_cast<uint16_t>(waveAddr + i)));
			}
			apu(NR32, static_cast<uint8_t>((m_curTrackVolumeEnvelope & 0xF0) << 1));
			apu(NR30, 0x80);
			apu(NR33, m_curTrackFrequency & 0xFF);
			apu(NR34, (m_curTrackFrequency >> 8) | 0x80);
			return;
		}
		if (f & NOTE_VIBRATO_OVERRIDE) {
			apu(NR33, m_curTrackFrequency & 0xFF);
		}
		return;
	default:
		if (f & NOTE_REST) {
			clearChannel(3);
			return;
		}
		if (f & NOTE_NOISE_SAMPLING) {
			apu(NR41, 0x3F);
			apu(NR42, m_curTrackVolumeEnvelope);
			apu(NR43, m_curTrackFrequency & 0xFF);
			apu(NR44, 0x80);
		}
		return;
	}
}

void GscPlayer::playDanger() {
	if (!m_dangerOn) {
		return;
	}
	if (!sfxBusy()) {
		// 0 프레임째 높은 음, 16 프레임째 낮은 음 (30 프레임 주기)
		static const uint8_t kHigh[4] = {0x80, 0xE2, 0x50, 0x87};
		static const uint8_t kLow[4] = {0x80, 0xE2, 0xEE, 0x86};
		const uint8_t* tone = m_dangerCount == 0 ? kHigh : m_dangerCount == 16 ? kLow : nullptr;
		if (tone) {
			apu(NR10, 0);
			apu(NR11, tone[0]);
			apu(NR12, tone[1]);
			apu(NR13, tone[2]);
			apu(NR14, tone[3]);
		}
	}
	m_dangerCount = static_cast<uint8_t>(m_dangerCount + 1 >= 30 ? 0 : m_dangerCount + 1);
	if (!(m_soundOutput & 0x11)) {
		m_soundOutput |= 0x11;
	}
}

void GscPlayer::loadNote(Channel& c) {
	if (!(c.flags2 & SOUND_PITCH_SLIDE)) {
		return;
	}
	int dur = c.noteDuration - m_curNoteDuration;
	m_curNoteDuration = static_cast<uint8_t>(dur < 0 ? 1 : dur);
	int distance;
	if (c.frequency < c.pitchSlideTarget) {
		c.flags3 |= SOUND_PITCH_SLIDE_DIR;
		distance = c.pitchSlideTarget - c.frequency;
	} else {
		c.flags3 &= ~SOUND_PITCH_SLIDE_DIR;
		distance = c.frequency - c.pitchSlideTarget;
	}
	// 원본은 뺄셈을 되풀이해 몫(1 크게)과 나머지를 구한다
	const int d = std::max<int>(m_curNoteDuration, 1);
	const int quotient = distance / d + 1;
	const int remainder = distance % d;
	c.pitchSlideAmount = static_cast<uint8_t>(quotient);
	c.pitchSlideAmountFraction = static_cast<uint8_t>(remainder);
	c.field25 = 0;
}

void GscPlayer::handleTrackVibrato(Channel& c) {
	if (c.flags2 & SOUND_DUTY_LOOP) {
		c.dutyCyclePattern = static_cast<uint8_t>((c.dutyCyclePattern << 2) | (c.dutyCyclePattern >> 6));
		m_curTrackDuty = c.dutyCyclePattern & 0xC0;
		c.noteFlags |= NOTE_DUTY_OVERRIDE;
	}
	if (c.flags2 & SOUND_PITCH_OFFSET) {
		m_curTrackFrequency = static_cast<uint16_t>(m_curTrackFrequency + c.pitchOffset);
	}
	if (!(c.flags2 & SOUND_VIBRATO)) {
		return;
	}
	if (c.vibratoDelayCount) {
		--c.vibratoDelayCount;
		return;
	}
	const uint8_t extent = c.vibratoExtent;
	if (!extent) {
		return;
	}
	if (c.vibratoRate & 0xF) {
		--c.vibratoRate;
		return;
	}
	c.vibratoRate = static_cast<uint8_t>(c.vibratoRate | (c.vibratoRate >> 4));
	uint8_t lo = m_curTrackFrequency & 0xFF;
	int v;
	if (c.flags3 & SOUND_VIBRATO_DIR) {
		c.flags3 &= ~SOUND_VIBRATO_DIR;
		v = lo - (extent & 0xF);
		if (v < 0) v = 0;
	} else {
		c.flags3 |= SOUND_VIBRATO_DIR;
		v = lo + (extent >> 4);
		if (v > 0xFF) v = 0xFF;
	}
	m_curTrackFrequency = static_cast<uint16_t>((m_curTrackFrequency & 0xFF00) | v);
	c.noteFlags |= NOTE_VIBRATO_OVERRIDE;
}

void GscPlayer::applyPitchSlide(Channel& c) {
	if (!(c.flags2 & SOUND_PITCH_SLIDE)) {
		return;
	}
	int f = c.frequency;
	bool finished;
	if (c.flags3 & SOUND_PITCH_SLIDE_DIR) {
		f += c.pitchSlideAmount;
		const int sum = c.field25 + c.pitchSlideAmountFraction;
		c.field25 = static_cast<uint8_t>(sum);
		if (sum > 0xFF) ++f;
		f &= 0xFFFF;
		finished = c.pitchSlideTarget < f;
	} else {
		f -= c.pitchSlideAmount;
		const int dbl = c.pitchSlideAmountFraction * 2;
		c.pitchSlideAmountFraction = static_cast<uint8_t>(dbl);
		if (dbl > 0xFF) --f;
		f &= 0xFFFF;
		finished = f < c.pitchSlideTarget;
	}
	if (finished) {
		c.flags2 &= ~SOUND_PITCH_SLIDE;
		c.flags3 &= ~SOUND_PITCH_SLIDE_DIR;
		return;
	}
	c.frequency = static_cast<uint16_t>(f);
	c.noteFlags |= NOTE_FREQ_OVERRIDE | NOTE_DUTY_OVERRIDE;
}

void GscPlayer::handleNoise(Channel& c) {
	if (!(c.flags1 & SOUND_NOISE)) {
		return;
	}
	if (!(m_curChannel & (1 << kNoiseChanBit))) {
		const Channel& c8 = m_ch[7];
		if ((c8.flags1 & SOUND_CHANNEL_ON) && (c8.flags1 & SOUND_NOISE)) {
			return;
		}
	}
	if (m_noiseSampleDelay) {
		--m_noiseSampleDelay;
		return;
	}
	readNoiseSample(c);
}

void GscPlayer::readNoiseSample(Channel& c) {
	uint16_t de = m_noiseSampleAddress;
	if (!de) {
		return;
	}
	const uint8_t a = romByte(m_tables.engineBank, de++);
	if (a == kSoundRet) {
		return;
	}
	m_noiseSampleDelay = static_cast<uint8_t>((a & 0xF) + 1);
	m_curTrackVolumeEnvelope = romByte(m_tables.engineBank, de++);
	m_curTrackFrequency = romByte(m_tables.engineBank, de++);
	m_noiseSampleAddress = de;
	c.noteFlags |= NOTE_NOISE_SAMPLING;
}

void GscPlayer::parseMusic(Channel& c) {
	for (int guard = 0; guard < 4096; ++guard) {
		const uint8_t a = getMusicByte(c);
		if (a == kSoundRet) {
			if (c.flags1 & SOUND_SUBROUTINE) {
				parseMusicCommand(c);
				continue;
			}
			// 곡(채널) 끝
			const bool music = m_curChannel < kNumMusicChans;
			if (!music || !(m_ch[m_curChannel + kNumMusicChans].flags1 & SOUND_CHANNEL_ON)) {
				if ((c.flags1 & SOUND_CRY) && m_curChannel == 4) {
					// RestoreVolume: 울음소리 동안 최대로 올렸던 음량을 되돌린다
					m_ch[5].pitchOffset = 0;
					m_ch[7].pitchOffset = 0;
					m_volume = m_lastVolume;
					m_lastVolume = 0;
					m_sfxPriority = 0;
				}
				if (m_curChannel == 4) {
					apu(NR10, 0);
				}
			}
			c.flags1 &= ~SOUND_CHANNEL_ON;
			c.noteFlags |= NOTE_REST;
			c.musicId = 0;
			c.musicBank = 0;
			return;
		}
		if (a >= kFirstMusicCmd) {
			parseMusicCommand(c);
			continue;
		}
		// 음
		if (c.flags1 & (SOUND_SFX | SOUND_CRY)) {
			parseSfxOrCry(c);
			return;
		}
		if (c.flags1 & SOUND_NOISE) {
			getNoiseSample(c);
			return;
		}
		setNoteDuration(c, a & 0xF);
		const uint8_t pitch = a >> 4;
		if (!pitch) {
			c.noteFlags |= NOTE_REST;
			return;
		}
		c.pitch = pitch;
		c.frequency = getFrequency(c, c.octave, pitch);
		c.noteFlags |= NOTE_NOISE_SAMPLING;
		loadNote(c);
		return;
	}
	c.flags1 &= ~SOUND_CHANNEL_ON;  // 데이터가 이상하면 멈춘다
}

void GscPlayer::parseSfxOrCry(Channel& c) {
	c.noteFlags |= NOTE_NOISE_SAMPLING;
	setNoteDuration(c, m_curMusicByte);
	c.volumeEnvelope = getMusicByte(c);
	c.frequency = static_cast<uint16_t>((c.frequency & 0xFF00) | getMusicByte(c));
	if ((m_curChannel & 3) == 3) {
		return;
	}
	c.frequency = static_cast<uint16_t>((c.frequency & 0xFF) | (getMusicByte(c) << 8));
}

void GscPlayer::getNoiseSample(Channel& c) {
	if ((m_curChannel & 3) != 3) {
		return;
	}
	setNoteDuration(c, m_curMusicByte & 0xF);
	uint8_t set;
	if (m_curChannel & (1 << kNoiseChanBit)) {
		set = m_sfxNoiseSampleSet;
	} else {
		if (m_ch[7].flags1 & SOUND_CHANNEL_ON) {
			return;
		}
		set = m_musicNoiseSampleSet;
	}
	const uint16_t kit = romWord(m_tables.engineBank, static_cast<uint16_t>(m_tables.drumkits + set * 2));
	const uint8_t pitch = m_curMusicByte >> 4;
	if (!pitch) {
		return;
	}
	m_noiseSampleAddress = romWord(m_tables.engineBank, static_cast<uint16_t>(kit + pitch * 2));
	m_noiseSampleDelay = 0;
}

uint8_t GscPlayer::getMusicByte(Channel& c) {
	m_curMusicByte = romByte(c.musicBank, c.musicAddress);
	++c.musicAddress;
	return m_curMusicByte;
}

uint16_t GscPlayer::getFrequency(Channel& c, uint8_t octave, uint8_t pitch) {
	int oct = ((c.transposition >> 4) & 0xF) + octave;
	const int idx = (c.transposition & 0xF) + pitch;
	int16_t de = static_cast<int16_t>(romWord(m_tables.engineBank, static_cast<uint16_t>(m_tables.frequency + idx * 2)));
	while ((oct & 0xFF) < 7) {
		de = static_cast<int16_t>(de >> 1);  // sra d / rr e
		++oct;
	}
	return static_cast<uint16_t>(de & 0x7FF);
}

void GscPlayer::setNoteDuration(Channel& c, uint8_t a) {
	auto multiply = [](uint8_t m, uint16_t de, uint16_t l) -> uint16_t {
		uint16_t hl = l;
		while (m) {
			if (m & 1) hl = static_cast<uint16_t>(hl + de);
			m >>= 1;
			de = static_cast<uint16_t>(de << 1);
		}
		return hl;
	};
	const uint16_t units = static_cast<uint16_t>(a + 1);
	const uint8_t low = static_cast<uint8_t>(multiply(c.noteLength, units, 0) & 0xFF);
	const uint16_t r = multiply(low, c.tempo, c.noteDurationModifier);
	c.noteDurationModifier = r & 0xFF;
	c.noteDuration = r >> 8;
}

void GscPlayer::setGlobalTempo(uint16_t tempo) {
	const int base = m_curChannel < kNumMusicChans ? 0 : 4;
	for (int i = base; i < base + 4; ++i) {
		m_ch[i].tempo = tempo;
		m_ch[i].noteDurationModifier = 0;
	}
}

void GscPlayer::setLRTracks(Channel& c) {
	c.tracks = kMonoTracks[m_curChannel & 3];  // Mono/StereoTracks 는 같은 값
}

void GscPlayer::startChannel(Channel& c) {
	setLRTracks(c);
	c.flags1 |= SOUND_CHANNEL_ON;
}

void GscPlayer::parseMusicCommand(Channel& c) {
	const uint8_t cmd = m_curMusicByte;
	switch (cmd) {
	case 0xD0: case 0xD1: case 0xD2: case 0xD3:
	case 0xD4: case 0xD5: case 0xD6: case 0xD7:  // octave
		c.octave = cmd & 7;
		break;
	case 0xD8:  // note_type
		c.noteLength = getMusicByte(c);
		if ((m_curChannel & 3) != 3) {
			c.volumeEnvelope = getMusicByte(c);
		}
		break;
	case 0xD9:  // transpose
		c.transposition = getMusicByte(c);
		break;
	case 0xDA: {  // tempo
		const uint8_t d = getMusicByte(c);
		const uint8_t e = getMusicByte(c);
		setGlobalTempo(static_cast<uint16_t>((d << 8) | e));
		break;
	}
	case 0xDB:  // duty_cycle
		c.dutyCycle = static_cast<uint8_t>(((getMusicByte(c) >> 2) | (m_curMusicByte << 6)) & 0xC0);
		break;
	case 0xDC:  // volume_envelope
		c.volumeEnvelope = getMusicByte(c);
		break;
	case 0xDD:  // pitch_sweep
		m_pitchSweep = getMusicByte(c);
		c.noteFlags |= NOTE_PITCH_SWEEP;
		break;
	case 0xDE: {  // duty_cycle_pattern
		c.flags2 |= SOUND_DUTY_LOOP;
		const uint8_t v = getMusicByte(c);
		const uint8_t rr = static_cast<uint8_t>((v >> 2) | (v << 6));
		c.dutyCyclePattern = rr;
		c.dutyCycle = rr & 0xC0;
		break;
	}
	case 0xDF:  // toggle_sfx
		c.flags1 ^= SOUND_SFX;
		break;
	case 0xE0: {  // pitch_slide
		m_curNoteDuration = getMusicByte(c);
		const uint8_t v = getMusicByte(c);
		c.pitchSlideTarget = getFrequency(c, v >> 4, v & 0xF);
		c.flags2 |= SOUND_PITCH_SLIDE;
		break;
	}
	case 0xE1: {  // vibrato
		c.flags2 |= SOUND_VIBRATO;
		c.flags3 &= ~SOUND_VIBRATO_DIR;
		const uint8_t delay = getMusicByte(c);
		c.vibratoDelay = delay;
		c.vibratoDelayCount = delay;
		const uint8_t d = getMusicByte(c);
		const uint8_t top = d >> 4;
		const uint8_t half = top >> 1;
		const uint8_t up = static_cast<uint8_t>(half + (top & 1));
		c.vibratoExtent = static_cast<uint8_t>((up << 4) | half);
		const uint8_t rate = d & 0xF;
		c.vibratoRate = static_cast<uint8_t>((rate << 4) | rate);
		break;
	}
	case 0xE2:  // 쓰이지 않음
		getMusicByte(c);
		break;
	case 0xE3:  // toggle_noise
		if (c.flags1 & SOUND_NOISE) {
			c.flags1 &= ~SOUND_NOISE;
		} else {
			c.flags1 |= SOUND_NOISE;
			m_musicNoiseSampleSet = getMusicByte(c);
		}
		break;
	case 0xE4: {  // force_stereo_panning
		setLRTracks(c);
		c.tracks &= getMusicByte(c);
		break;
	}
	case 0xE5:  // volume
		m_volume = getMusicByte(c);
		break;
	case 0xE6: {  // pitch_offset
		c.flags2 |= SOUND_PITCH_OFFSET;
		const uint8_t hi = getMusicByte(c);
		const uint8_t lo = getMusicByte(c);
		c.pitchOffset = static_cast<uint16_t>((hi << 8) | lo);
		break;
	}
	case 0xE7:
	case 0xE8:
		getMusicByte(c);
		break;
	case 0xE9: {  // tempo_relative
		const int8_t e = static_cast<int8_t>(getMusicByte(c));
		setGlobalTempo(static_cast<uint16_t>(c.tempo + e));
		break;
	}
	case 0xEA: {  // restart_channel
		m_musicId = c.musicId;
		m_musicBank = c.musicBank;
		const uint8_t l = getMusicByte(c);
		const uint8_t h = getMusicByte(c);
		const int saved = m_curChannel;
		// 원본: ld e,[hl] / inc hl / ld d,[hl] (엔진 코드가 도는 뱅크에서 읽는다) -> de 를 채널 헤더로 LoadChannel
		uint16_t de = romWord(m_tables.engineBank, static_cast<uint16_t>((h << 8) | l));
		Channel& nc = loadChannel(m_musicBank, de);
		startChannel(nc);
		m_curChannel = saved;
		break;
	}
	case 0xEB: {  // new_song
		const uint8_t e = getMusicByte(c);
		const uint8_t d = getMusicByte(c);
		const int saved = m_curChannel;
		playMusic(static_cast<uint16_t>((d << 8) | e));
		m_curChannel = saved;
		break;
	}
	case 0xEC:  // sfx_priority_on
		m_sfxPriority = 1;
		break;
	case 0xED:  // sfx_priority_off
		m_sfxPriority = 0;
		break;
	case 0xEE: {  // 쓰이지 않음 (조건 점프)
		c.musicAddress = static_cast<uint16_t>(c.musicAddress + 2);
		break;
	}
	case 0xEF:  // stereo_panning
		if (stereo()) {
			setLRTracks(c);
			c.tracks &= getMusicByte(c);
		} else {
			getMusicByte(c);
		}
		break;
	case 0xF0:  // sfx_toggle_noise
		if (c.flags1 & SOUND_NOISE) {
			c.flags1 &= ~SOUND_NOISE;
		} else {
			c.flags1 |= SOUND_NOISE;
			m_sfxNoiseSampleSet = getMusicByte(c);
		}
		break;
	case 0xFA:  // set_condition
		c.condition = getMusicByte(c);
		break;
	case 0xFB: {  // sound_jump_if
		const uint8_t cond = getMusicByte(c);
		if (cond == c.condition) {
			const uint8_t l = getMusicByte(c);
			const uint8_t h = getMusicByte(c);
			c.musicAddress = static_cast<uint16_t>((h << 8) | l);
		} else {
			c.musicAddress = static_cast<uint16_t>(c.musicAddress + 2);
		}
		break;
	}
	case 0xFC: {  // sound_jump
		const uint8_t l = getMusicByte(c);
		const uint8_t h = getMusicByte(c);
		c.musicAddress = static_cast<uint16_t>((h << 8) | l);
		break;
	}
	case 0xFD: {  // sound_loop
		const uint8_t count = getMusicByte(c);
		bool jump;
		if (c.flags1 & SOUND_LOOPING) {
			jump = c.loopCount != 0;
			if (jump) --c.loopCount;
		} else if (!count) {
			jump = true;
		} else {
			c.flags1 |= SOUND_LOOPING;
			c.loopCount = static_cast<uint8_t>(count - 1);
			jump = c.loopCount != 0;
			if (jump) --c.loopCount;
		}
		if (jump) {
			const uint8_t l = getMusicByte(c);
			const uint8_t h = getMusicByte(c);
			c.musicAddress = static_cast<uint16_t>((h << 8) | l);
		} else {
			c.flags1 &= ~SOUND_LOOPING;
			c.musicAddress = static_cast<uint16_t>(c.musicAddress + 2);
		}
		break;
	}
	case 0xFE: {  // sound_call
		const uint8_t l = getMusicByte(c);
		const uint8_t h = getMusicByte(c);
		c.lastMusicAddress = c.musicAddress;
		c.musicAddress = static_cast<uint16_t>((h << 8) | l);
		c.flags1 |= SOUND_SUBROUTINE;
		break;
	}
	case 0xFF:  // sound_ret (서브루틴 안)
		c.flags1 &= ~SOUND_SUBROUTINE;
		c.musicAddress = c.lastMusicAddress;
		break;
	default:  // F1~F9: 아무것도 하지 않음
		break;
	}
}

GscPlayer::Channel& GscPlayer::loadChannel(uint8_t bank, uint16_t& addr) {
	const uint8_t id = romByte(bank, addr++) & 7;
	m_curChannel = id;
	Channel& c = m_ch[id];
	c.flags1 &= ~SOUND_CHANNEL_ON;
	c = Channel();
	c.tempo = 0x100;
	c.noteLength = 1;
	const uint8_t lo = romByte(bank, addr++);
	const uint8_t hi = romByte(bank, addr++);
	c.musicAddress = static_cast<uint16_t>((hi << 8) | lo);
	c.musicId = m_musicId;
	c.musicBank = bank;
	return c;
}

void GscPlayer::playMusic(uint16_t id) {
	m_musicPlaying = false;
	m_musicId = id;
	const uint16_t entry = static_cast<uint16_t>(m_tables.music + id * 3);
	m_musicBank = romByte(m_tables.engineBank, entry);
	uint16_t hdr = romWord(m_tables.engineBank, static_cast<uint16_t>(entry + 1));
	int count = ((romByte(m_musicBank, hdr) >> 6) & 3) + 1;
	while (count--) {
		Channel& c = loadChannel(m_musicBank, hdr);
		startChannel(c);
	}
	m_noiseSampleAddress = 0;
	m_noiseSampleDelay = 0;
	m_musicNoiseSampleSet = 0;
	m_musicPlaying = true;
}

void GscPlayer::playSfx(uint16_t id) {
	m_musicPlaying = false;
	if (m_ch[4].flags1 & SOUND_CHANNEL_ON) {
		m_ch[4].flags1 &= ~SOUND_CHANNEL_ON;
		apu(NR11, 0);
		apu(NR12, 0x08);
		apu(NR13, 0);
		apu(NR14, 0x80);
		m_pitchSweep = 0;
		apu(NR10, 0);
	}
	if (m_ch[5].flags1 & SOUND_CHANNEL_ON) {
		m_ch[5].flags1 &= ~SOUND_CHANNEL_ON;
		apu(NR21, 0);
		apu(NR22, 0x08);
		apu(NR23, 0);
		apu(NR24, 0x80);
	}
	if (m_ch[6].flags1 & SOUND_CHANNEL_ON) {
		m_ch[6].flags1 &= ~SOUND_CHANNEL_ON;
		apu(NR30, 0);
		apu(NR31, 0);
		apu(NR32, 0x08);
		apu(NR33, 0);
		apu(NR34, 0x80);
	}
	if (m_ch[7].flags1 & SOUND_CHANNEL_ON) {
		m_ch[7].flags1 &= ~SOUND_CHANNEL_ON;
		apu(NR41, 0);
		apu(NR42, 0x08);
		apu(NR43, 0);
		apu(NR44, 0x80);
		m_noiseSampleAddress = 0;
	}
	m_musicId = id;
	const uint16_t entry = static_cast<uint16_t>(m_tables.sfx + id * 3);
	m_musicBank = romByte(m_tables.engineBank, entry);
	uint16_t hdr = romWord(m_tables.engineBank, static_cast<uint16_t>(entry + 1));
	int count = ((romByte(m_musicBank, hdr) >> 6) & 3) + 1;
	while (count--) {
		Channel& c = loadChannel(m_musicBank, hdr);
		c.flags1 |= SOUND_SFX;
		startChannel(c);
	}
	m_musicPlaying = true;
	m_sfxPriority = 0;
	m_curSfx = static_cast<uint8_t>(id);
}

void GscPlayer::playCry(uint16_t id, uint16_t pitch, uint16_t length, const int tracks[4]) {
	// _PlayCry (audio/engine.asm): 울음소리 표에서 머리를 읽어 채널마다 울음소리 표시, 음높이 보정, 길이(템포)를 넣는다
	if (!m_tables.cries) {
		return;
	}
	m_musicId = id;
	const uint16_t entry = static_cast<uint16_t>(m_tables.cries + id * 3);
	m_musicBank = romByte(m_tables.engineBank, entry);
	uint16_t hdr = romWord(m_tables.engineBank, static_cast<uint16_t>(entry + 1));
	int count = ((romByte(m_musicBank, hdr) >> 6) & 3) + 1;
	while (count--) {
		Channel& c = loadChannel(m_musicBank, hdr);
		c.flags1 |= SOUND_CRY;
		c.flags2 |= SOUND_PITCH_OFFSET;
		c.pitchOffset = pitch;
		const int hw = m_curChannel & 3;
		if (hw != 3) {
			c.tempo = length;  // 노이즈 채널에는 템포가 없다
		}
		startChannel(c);
		// 전투에서는 원본이 그 캐릭터 쪽으로만 낸다 ([CHANNEL_TRACKS] &= [wCryTracks]). 원본 값을 그대로 쓴다
		if (tracks && tracks[hw] >= 0) {
			c.tracks = static_cast<uint8_t>(tracks[hw]);
		}
	}
	// 울음소리는 최대 음량으로 낸다
	if (!m_lastVolume) {
		m_lastVolume = m_volume;
		m_volume = kMaxVolume;
	}
	m_sfxPriority = 1;
	m_musicPlaying = true;
}

void GscPlayer::requestCry(uint16_t id, uint16_t pitch, uint16_t length, const int tracks[4]) {
	m_cryPitch = pitch;
	m_cryLength = length;
	for (int i = 0; i < 4; ++i) {
		m_cryTracks[i] = tracks ? tracks[i] : -1;
	}
	m_pendingCry = id;
}

bool GscPlayer::sfxBusy() const {
	for (int i = 4; i < 8; ++i) {
		if (m_ch[i].flags1 & SOUND_CHANNEL_ON) {
			return true;
		}
	}
	return false;
}

void GscPlayer::requestSong(uint16_t id) {
	m_pending = id;
}

void GscPlayer::requestSfx(uint16_t id) {
	m_pendingSfx = id;
}

void GscPlayer::applyRequests() {
	// 원본에서 경고음이 켜지면 처음(높은 음)부터 낸다
	const bool danger = m_tables.lowHealthAlarm && m_dangerReq;
	if (danger && !m_dangerOn) {
		m_dangerCount = 0;
	}
	m_dangerOn = danger;
	const int song = m_pending.exchange(-1);
	if (song >= 0) {
		// 게임의 PlayMusic: 0 이면 소리를 끄고(_InitSound), 아니면 곡을 시작한다
		if (song == 0) {
			initSound();
		} else {
			playMusic(static_cast<uint16_t>(song));
		}
	}
	const int sfx = m_pendingSfx.exchange(-1);
	if (sfx >= 0 && (!sfxBusy() || (sfx & 0xFF) < m_curSfx)) {
		playSfx(static_cast<uint16_t>(sfx));
	}
	const int cry = m_pendingCry.exchange(-1);
	if (cry >= 0) {
		const int tracks[4] = {m_cryTracks[0], m_cryTracks[1], m_cryTracks[2], m_cryTracks[3]};
		playCry(static_cast<uint16_t>(cry), static_cast<uint16_t>(m_cryPitch.load()),
		        static_cast<uint16_t>(m_cryLength.load()), tracks);
	}
	m_apu.setMutedChannels(m_mutedMask);
	const int regs = m_outRegs;
	if (regs >= 0) {
		m_apu.setOutputOverride(regs >> 8, regs & 0xFF);
	}
	// 하드웨어 채널마다 지금 내는 쪽(효과음이 쓰고 있으면 효과음, 아니면 곡)의 볼륨
	const float musicGain = m_musicGain, sfxGain = m_sfxGain;
	for (int i = 0; i < 4; ++i) {
		m_apu.setChannelGain(i, (m_ch[4 + i].flags1 & SOUND_CHANNEL_ON) ? sfxGain : musicGain);
	}
}

void GscPlayer::render(int frames, int sampleRate, float* out) {
	const double perSample = kClock / sampleRate;
	for (int i = 0; i < frames; ++i) {
		double need = perSample;
		double accL = 0, accR = 0;
		while (need > 0) {
			if (m_frameCycles <= 0) {
				applyRequests();
				updateSound();
				m_frameCycles += kFrameCycles;
			}
			const double step = std::min(need, m_frameCycles);
			float l, r;
			m_apu.run(step, l, r);
			accL += l * step;
			accR += r * step;
			need -= step;
			m_frameCycles -= step;
		}
		out[i * 2] = static_cast<float>(accL / perSample);
		out[i * 2 + 1] = static_cast<float>(accR / perSample);
	}
	// 직류 제거 (mGBA GB 오디오의 축전기 필터와 같은 세기: 131072Hz 에서 표본마다 65368/65536)
	const float k = static_cast<float>(std::pow(65368.0 / 65536.0, 131072.0 / sampleRate));
	for (int i = 0; i < frames; ++i) {
		const float l = out[i * 2] - m_capL;
		m_capL = out[i * 2] - l * k;
		out[i * 2] = l;
		const float r = out[i * 2 + 1] - m_capR;
		m_capR = out[i * 2 + 1] - r * k;
		out[i * 2 + 1] = r;
	}
}

void GscPlayer::mixInto(int16_t* stereo, int frames, int sampleRate, int volume) {
	if (!ok() || frames <= 0 || sampleRate <= 0) {
		return;
	}
	std::vector<float> buf(static_cast<size_t>(frames) * 2);
	render(frames, sampleRate, buf.data());
	for (int i = 0; i < frames * 2; ++i) {
		const int v = stereo[i] + static_cast<int>(buf[i] * volume / 0x100);
		stereo[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, v)));
	}
}

void GscPlayer::runFrame() {
	if (!ok()) {
		return;
	}
	// 녹화: 게임 프레임 하나 = 엔진 갱신 한 번 + 그만큼의 소리
	const double perFrame = kFrameCycles * kRecordRate / kClock;
	m_recCarry += perFrame;
	const int n = static_cast<int>(m_recCarry);
	m_recCarry -= n;
	std::vector<float> buf(static_cast<size_t>(n) * 2);
	render(n, kRecordRate, buf.data());
	for (int i = 0; i < n * 2; ++i) {
		m_streamFifo.push_back(static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, buf[i]))));
	}
	while (m_streamFifo.size() > 2 * 32768) {
		m_streamFifo.pop_front();
	}
}

bool GscPlayer::popStreamSample(int16_t& left, int16_t& right) {
	if (m_streamFifo.size() < 2) {
		return false;
	}
	left = m_streamFifo.front();
	m_streamFifo.pop_front();
	right = m_streamFifo.front();
	m_streamFifo.pop_front();
	return true;
}

}
