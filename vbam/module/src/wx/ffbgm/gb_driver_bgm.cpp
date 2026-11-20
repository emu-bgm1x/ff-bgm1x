/* ff-bgm1x: GB/GBC 게임용 BGM 1배속 유지 */
#include "gb_driver_bgm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>


using namespace QGBA;

namespace {

// ---------------------------------------------------------------- ROM 패턴 검색

constexpr int ANY = -1;

bool matchAt(const std::vector<uint8_t>& rom, size_t off, const std::vector<int>& pat) {
	if (off + pat.size() > rom.size()) {
		return false;
	}
	for (size_t i = 0; i < pat.size(); ++i) {
		if (pat[i] != ANY && rom[off + i] != pat[i]) {
			return false;
		}
	}
	return true;
}

// bank:addr (addr 0x4000~0x7FFF 는 bank 영역) -> ROM 오프셋
bool romOffset(const std::vector<uint8_t>& rom, int bank, uint16_t addr, size_t& off) {
	if (addr < 0x4000) {
		off = addr;
	} else if (addr < 0x8000) {
		off = static_cast<size_t>(bank) * 0x4000 + (addr - 0x4000);
	} else {
		return false;
	}
	return off < rom.size();
}

uint16_t word(const std::vector<uint8_t>& rom, size_t off) {
	return static_cast<uint16_t>(rom[off] | (rom[off + 1] << 8));
}

// GB 2세대 사운드 엔진. 코드는 pret 의 2세대 디컴파일 소스 기준.
//   PlayMusic / UpdateSound: bank 0 의 래퍼 (hROMBank 저장 -> 오디오 bank 전환 -> 엔진 호출 -> 복원)
//   _UpdateSound 첫머리: ld a,[wMusicPlaying] / and a / ret z / xor a / ld [wCurChannel],a /
//                        ld [wSoundOutput],a / ld bc,wChannel1 / ld hl,CHANNEL_FLAGS1 / add hl,bc / bit 0,[hl]
//   _PlayMusic 첫머리: call MusicOff / ld hl,wMusicID / ld [hl],e / inc hl / ld [hl],d
bool detectGen2SoundEngine(const std::vector<uint8_t>& rom, GbDriverProfile& out) {
	const std::vector<int> updateWrapper = {
		0xE5, 0xD5, 0xC5, 0xF5, 0xF0, ANY, 0xF5, 0x3E, ANY, 0xE0, ANY, 0xEA, 0x00, 0x20,
		0xCD, ANY, ANY, 0xF1, 0xE0, ANY, 0xEA, 0x00, 0x20, 0xF1, 0xC1, 0xD1, 0xE1, 0xC9,
	};
	const std::vector<int> playWrapper = {
		0xE5, 0xD5, 0xC5, 0xF5, 0xF0, ANY, 0xF5, 0x3E, ANY, 0xE0, ANY, 0xEA, 0x00, 0x20,
		0x7B, 0xA7, 0x28, 0x05, 0xCD, ANY, ANY, 0x18, 0x03, 0xCD, ANY, ANY, 0xF1,
	};
	const std::vector<int> updateBody = {
		0xFA, ANY, ANY, 0xA7, 0xC8, 0xAF, 0xEA, ANY, ANY, 0xEA, ANY, ANY,
		0x01, ANY, ANY, 0x21, ANY, 0x00, 0x09, 0xCB, 0x46,
	};
	const std::vector<int> nextChannel = {0x21, ANY, 0x00, 0x09, 0x4D, 0x44, 0xFA};
	const std::vector<int> playBody = {0xCD, ANY, ANY, 0x21, ANY, ANY, 0x73, 0x23, 0x72};

	bool haveUpdate = false;
	bool havePlay = false;
	const size_t bank0End = std::min<size_t>(rom.size(), 0x4000);
	for (size_t a = 0; a + 32 < bank0End && !(haveUpdate && havePlay); ++a) {
		if (!haveUpdate && matchAt(rom, a, updateWrapper)) {
			size_t off;
			if (romOffset(rom, rom[a + 8], word(rom, a + 15), off) && matchAt(rom, off, updateBody)) {
				out.updateSound = static_cast<uint16_t>(a);
				out.channelBase = word(rom, off + 13);
				out.flagsOffset = rom[off + 16];
				// 다음 채널로 넘어가는 ld hl,CHANNEL_STRUCT_LENGTH 에서 구조체 크기를 읽는다
				for (size_t s = off; s < off + 512 && s + 8 < rom.size(); ++s) {
					if (matchAt(rom, s, nextChannel)) {
						out.channelStride = rom[s + 1];
						break;
					}
				}
				haveUpdate = out.channelStride != 0;
			}
		}
		if (!havePlay && matchAt(rom, a, playWrapper)) {
			size_t off;
			if (romOffset(rom, rom[a + 8], word(rom, a + 19), off) && matchAt(rom, off, playBody)) {
				out.playMusic = static_cast<uint16_t>(a);
				out.musicId = word(rom, off + 4);
				havePlay = true;
			}
		}
	}
	if (!haveUpdate || !havePlay) {
		return false;
	}
	out.engine = "GB 2nd-generation sound engine";
	out.onMask = 0x01;  // SOUND_CHANNEL_ON
	out.sfxChannelStart = 4;

	// 효과음: PlaySFX 의 .play 부분 (우선순위 판단 뒤)
	//   ldh a,[hROMBank] / push af / ld a,BANK(_PlaySFX) / ldh [hROMBank],a / ld [rROMB],a /
	//   ld a,e / ld [wCurSFX],a / call _PlaySFX / pop af / ldh [hROMBank],a / ld [rROMB],a
	//   _PlaySFX 첫머리: call MusicOff / ld hl,wChannel5Flags1 / bit SOUND_CHANNEL_ON,[hl]
	const std::vector<int> sfxPlay = {
		0xF0, ANY, 0xF5, 0x3E, ANY, 0xE0, ANY, 0xEA, 0x00, 0x20, 0x7B, 0xEA, ANY, ANY,
		0xCD, ANY, ANY, 0xF1, 0xE0, ANY, 0xEA, 0x00, 0x20,
	};
	const std::vector<int> sfxBody = {0xCD, ANY, ANY, 0x21, ANY, ANY, 0xCB, 0x46};
	const uint16_t ch5Flags = out.channelBase + 4 * out.channelStride + out.flagsOffset;
	for (size_t a = 0; a + 32 < bank0End; ++a) {
		if (!matchAt(rom, a, sfxPlay) || rom[a + 1] != rom[a + 6]) {
			continue;
		}
		size_t off;
		if (romOffset(rom, rom[a + 4], word(rom, a + 15), off) && matchAt(rom, off, sfxBody) &&
		    word(rom, off + 4) == ch5Flags) {
			out.playSfx = word(rom, a + 15);
			out.sfxBank = rom[a + 4];
			out.hRomBank = rom[a + 1];
			out.curSfx = word(rom, a + 12);
			out.cryFlag = 1 << 5;  // SOUND_CRY
			out.addressOffset = 6; // CHANNEL_MUSIC_ADDRESS
			break;
		}
	}

	// 게임 설정 바이트(wOptions): Music_StereoPanning 의
	//   ld a,[wOptions] / bit STEREO,a / jr nz,ForceStereoPanning / call GetMusicByte / ret
	const std::vector<int> stereoPanning = {0xFA, ANY, ANY, 0xCB, 0x6F, 0x20, ANY, 0xCD, ANY, ANY, 0xC9};
	for (size_t a = 0; a + stereoPanning.size() <= rom.size(); ++a) {
		if (matchAt(rom, a, stereoPanning)) {
			const uint16_t addr = word(rom, a + 1);
			if (addr >= 0xC000 && addr < 0xE000) {
				out.optionsAddr = addr;
				break;
			}
		}
	}
	return true;
}

int16_t clamp16(int v) {
	return static_cast<int16_t>(std::max(-32768, std::min(32767, v)));
}

}

bool QGBA::detectGbDriver(const std::vector<uint8_t>& rom, GbDriverProfile& out) {
	// 새 엔진 프로필은 여기에 추가한다
	if (detectGen2SoundEngine(rom, out)) {
		// 자체 재생기(GscPlayer)가 쓰는 엔진 표도 찾아야 한다
		return findGscTables(rom, out.tables);
	}
	return false;
}

// ---------------------------------------------------------------- 백엔드

GbDriverBgm::GbDriverBgm(const std::vector<uint8_t>& rom, const GbDriverProfile& profile)
	: m_rom(rom)
	, m_profile(profile)
{
}

GbDriverBgm::~GbDriverBgm() {
	std::lock_guard<std::mutex> lock(m_liveMutex);
	m_live.reset();
}

void GbDriverBgm::onReset() {
	m_resetLive = true;
	m_rec.reset();
	m_lastSong = -1;
}

void GbDriverBgm::releaseMute(GbMainCore& core) {
	core.setChannelsDisabled(0);
	m_muting = false;
	const int one[4] = {256, 256, 256, 256};
	setNativeGains(core, one);
	m_cutscene = false;
}

void GbDriverBgm::setNativeGains(GbMainCore& core, const int gains[4]) {
	const bool any = gains[0] != 256 || gains[1] != 256 || gains[2] != 256 || gains[3] != 256;
	if (!any && !m_gainsSet) {
		return;
	}
	for (int i = 0; i < 4; ++i) {
		core.setChannelGain(i, gains[i]);
	}
	m_gainsSet = any;
}

namespace {

std::string gbKey(const char* prefix, int id) {
	return std::string(prefix) + std::to_string(id);
}

int toQ8(int percent) {
	const int maxVolume = SoundCatalog::kMaxVolume;  // C++14: 참조로 넘기지 않게 값으로
	return std::max(0, std::min(maxVolume, percent)) * 256 / 100;
}

}

// 나온 횟수: 곡은 번호가 바뀔 때, 효과음·울음소리는 채널이 새로 켜지거나 다른 소리로 바뀌거나 처음부터 다시 낼 때 센다
void GbDriverBgm::noteSounds(GbMainCore& core) {
	const GbDriverProfile& p = m_profile;
	int song = 0;
	for (int i = 0; i < p.sfxChannelStart; ++i) {
		const uint16_t ch = p.channelBase + i * p.channelStride;
		if (core.r8(ch + p.flagsOffset) & p.onMask) {
			song = core.r8(ch) | (core.r8(ch + 1) << 8);
			break;
		}
	}
	if (song != m_cntSong) {
		m_cntSong = song;
		if (song) {
			m_catalog->NoteSeen(gbKey("gbsong:", song), SoundCatalog::Type_Song, "곡 " + std::to_string(song),
			                    SoundCatalog::Kind_Bgm, nullptr, nullptr);
		}
	}
	bool counted = false;  // 효과음 하나가 여러 채널을 쓰므로 한 프레임에 한 번만 센다
	for (int i = 0; i < 4; ++i) {
		const uint16_t ch = p.channelBase + (p.sfxChannelStart + i) * p.channelStride;
		const uint8_t flags = core.r8(ch + p.flagsOffset);
		CountState& st = m_cnt[i];
		const bool on = flags & p.onMask;
		if (!on) {
			st.on = false;
			continue;
		}
		const bool cry = p.cryFlag && (flags & p.cryFlag);
		const uint16_t id = core.r8(ch) | (core.r8(ch + 1) << 8);
		const uint16_t address =
		    core.r8(ch + p.addressOffset) | (core.r8(ch + p.addressOffset + 1) << 8);
		const bool started = !st.on || cry != st.cry || id != st.id || address < st.address;
		st.on = true;
		st.cry = cry;
		st.id = id;
		st.address = address;
		if (!started || counted) {
			continue;
		}
		counted = true;
		if (cry) {
			m_catalog->NoteSeen("cry:all", SoundCatalog::Type_Cry, "울음소리 전체", SoundCatalog::Kind_None, nullptr, nullptr);
		} else {
			m_catalog->NoteSeen(gbKey("gbsfx:", id), SoundCatalog::Type_Sfx, "효과음 " + std::to_string(id),
			                    p.playSfx ? SoundCatalog::Kind_Sfx : SoundCatalog::Kind_None, nullptr, nullptr);
		}
	}
}

void GbDriverBgm::onFrame(GbMainCore& core, bool enabled, bool sfx1x) {
	const bool cat = m_catalog && m_catalog->Active();
	if (cat) {
		noteSounds(core);
	}
	if (!enabled) {
		if (m_muting) {
			releaseMute(core);
		}
		return;
	}
	const GbDriverProfile& p = m_profile;
	// wMusicID 는 효과음·울음소리 재생 때도 덮어쓰이므로, 켜져 있는 음악 채널(1~4)의 곡 번호를 쓴다
	int song = 0;
	for (int i = 0; i < p.sfxChannelStart; ++i) {
		uint16_t ch = p.channelBase + i * p.channelStride;
		if (core.r8(ch + p.flagsOffset) & p.onMask) {
			song = core.r8(ch) | (core.r8(ch + 1) << 8);
			break;
		}
	}
	// 소리 분류: 곡의 사용자 지정. 아무것도 아님·연출이면 대체하지 않고 원본을 그대로 낸다
	int songUser = SoundCatalog::Kind_Auto, songVol = 100;
	if (cat && song) {
		m_catalog->Lookup(gbKey("gbsong:", song), &songUser, &songVol);
	}
	const bool songNative = songUser == SoundCatalog::Kind_None || songUser == SoundCatalog::Kind_Cutscene;
	bool hold = song && songUser == SoundCatalog::Kind_Cutscene;
	int gains[4] = {256, 256, 256, 256};
	float sfxGain = 1.0f;

	// 효과음 채널: 원본 소리를 그대로 낼 채널(울음소리 등)과, 숨겨진 코어에서 1배속으로 대신 낼 효과음
	const bool replaceSfx = sfx1x && p.playSfx;
	uint8_t sfx = 0;       // 원본에서 소리를 내는 효과음 채널
	uint8_t replaced = 0;  // 1배속으로 대신 내는 효과음 채널
	int newSfx = -1;
	int newCry = -1;
	for (int i = 0; i < 4; ++i) {
		const uint16_t ch = p.channelBase + (p.sfxChannelStart + i) * p.channelStride;
		const uint8_t flags = core.r8(ch + p.flagsOffset);
		const bool on = flags & p.onMask;
		const bool cry = flags & p.cryFlag;
		const uint16_t id = core.r8(ch) | (core.r8(ch + 1) << 8);
		int user = SoundCatalog::Kind_Auto, vol = 100;
		if (cat && on) {
			m_catalog->Lookup(cry ? std::string("cry:all") : gbKey("gbsfx:", id), &user, &vol);
		}
		if (on && user == SoundCatalog::Kind_Cutscene) {
			hold = true;
		}
		// 효과음 채널은 효과음과 울음소리만 쓴다. 효과음 표시(SOUND_SFX)는 보지 않는다:
		// 아이템 받는 소리처럼 첫 명령(toggle_sfx)으로 표시를 끄는 효과음이 있다.
		// 사용자 지정: BGM·팡파레는 옵션과 관계없이 1배속, 아무것도 아님·연출은 원본 그대로
		bool want = replaceSfx;
		if (user == SoundCatalog::Kind_Bgm || user == SoundCatalog::Kind_Jingle) {
			want = p.playSfx != 0;
		} else if (user == SoundCatalog::Kind_None || user == SoundCatalog::Kind_Cutscene) {
			want = false;
		}
		if (cry) {
			// 울음소리: "RAM 곡 읽기" 가 켜져 있고 사용자가 1배속으로 정했을 때만 (자동 판단은 원본 그대로)
			want = m_ramSongs && p.tables.cries &&
			       (user == SoundCatalog::Kind_Bgm || user == SoundCatalog::Kind_Jingle ||
			        (user == SoundCatalog::Kind_Sfx && sfx1x));
		}
		const bool replace = on && want;
		if (on && !replace) {
			sfx |= 1 << i;
			gains[i] = toQ8(vol);  // 원본 그대로 나오는 효과음·울음소리의 볼륨
		}
		if (replace) {
			replaced |= 1 << i;
		}
		SfxChannelState& st = m_sfxState[i];
		if (!replace) {
			st.on = false;
			continue;
		}
		const uint16_t address =
		    core.r8(ch + p.addressOffset) | (core.r8(ch + p.addressOffset + 1) << 8);
		// 새로 시작: 꺼져 있다 켜짐, 다른 효과음, 같은 효과음을 처음부터 다시 냄(읽는 위치가 뒤로 감)
		if ((!st.on || id != st.id || address < st.address) && newSfx < 0 && newCry < 0) {
			sfxGain = vol / 100.0f;
			if (cry) {
				// 원본 채널 구조체의 음높이 보정(CHANNEL_PITCH_OFFSET, +39)과 길이(CHANNEL_TEMPO, +25)를 읽는다.
				// 노이즈 채널(4번)은 템포가 없으므로 길이는 1~3번 채널에서
				newCry = id;
				m_cryPitch = core.r8(ch + 39) | (core.r8(ch + 40) << 8);
				for (int k = 0; k < 3; ++k) {
					const uint16_t cc = p.channelBase + (p.sfxChannelStart + k) * p.channelStride;
					if ((core.r8(cc + p.flagsOffset) & (p.onMask | p.cryFlag)) == (p.onMask | p.cryFlag)) {
						m_cryLength = core.r8(cc + 25) | (core.r8(cc + 26) << 8);
						break;
					}
				}
				for (int k = 0; k < 4; ++k) {
					const uint16_t cc = p.channelBase + (p.sfxChannelStart + k) * p.channelStride;
					const bool isCry = (core.r8(cc + p.flagsOffset) & (p.onMask | p.cryFlag)) == (p.onMask | p.cryFlag);
					m_cryTracks[k] = isCry ? core.r8(cc + 27) : -1;
				}
			} else {
				newSfx = id;
			}
		}
		st.on = true;
		st.id = id;
		st.address = address;
	}
	if (newSfx >= 0) {
		m_sfxId = newSfx;
		m_sfxGainNext = sfxGain;
		++m_sfxSeq;
	}
	if (newCry >= 0) {
		m_cryId = newCry;
		m_sfxGainNext = sfxGain;
		++m_crySeq;
	}
	uint8_t disabled = 0;
	for (int i = 0; i < 4; ++i) {
		if (songNative) {
			// 곡을 원본 그대로 낸다: 1배속으로 대신 내는 효과음 채널만 끈다
			if ((replaced >> i) & 1) {
				disabled |= 1 << i;
			}
			if (!((sfx >> i) & 1) && !((replaced >> i) & 1)) {
				gains[i] = toQ8(songVol);
			}
		} else if (!((sfx >> i) & 1)) {
			// 원본에서 소리를 내는 효과음 채널만 남기고 끈다 (음악과 대신 내는 효과음은 재생기가 낸다)
			disabled |= 1 << i;
		}
	}
	core.setChannelsDisabled(disabled);
	setNativeGains(core, gains);
	m_cutscene = hold;
	m_muting = true;
	m_liveSong = songNative ? 0 : song;
	m_songGain = songVol / 100.0f;
	m_sfxMask = sfx;
	const int regs = (core.r8(0xFF24) << 8) | core.r8(0xFF25);
	m_outRegs = regs;
	if (p.tables.lowHealthAlarm) {
		m_danger = (core.r8(p.tables.lowHealthAlarm) & 0x80) != 0;
	}
	if (p.optionsAddr) {
		m_options = core.r8Bank1(p.optionsAddr);
	}

	if (m_recording) {
		if (!m_rec) {
			m_rec.reset(new GscPlayer(m_rom, m_profile.tables, false));
			m_lastSong = -1;
		}
		const int recSong = songNative ? 0 : song;
		if (recSong != m_lastSong) {
			m_rec->requestSong(static_cast<uint16_t>(recSong));
			m_lastSong = recSong;
		}
		if (newSfx >= 0) {
			m_rec->requestSfx(static_cast<uint16_t>(newSfx));
		}
		if (newCry >= 0) {
			sendCry(*m_rec);
		}
		m_rec->setGains(m_songGain, m_sfxGainNext);
		m_rec->setMutedChannels(sfx);
		m_rec->setOutputRegs(static_cast<uint8_t>(regs >> 8), static_cast<uint8_t>(regs & 0xFF));
		if (m_options >= 0) {
			m_rec->setOptions(static_cast<uint8_t>(m_options.load()));
		}
		m_rec->setDanger(m_danger);
	}
}

void GbDriverBgm::mixRealtime(int16_t* stereo, int frames, int sampleRate, int volume) {
	std::lock_guard<std::mutex> lock(m_liveMutex);
	if (m_resetLive.exchange(false)) {
		m_live.reset();
	}
	int song = m_liveSong;
	if (song < 0) {
		return;  // 아직 게임 상태를 읽지 못함
	}
	if (!m_live) {
		m_live.reset(new GscPlayer(m_rom, m_profile.tables, false));
		m_liveRequested = -1;
		m_liveSfxSeq = m_sfxSeq;  // 만들기 전에 시작된 효과음은 내지 않는다
		m_liveCrySeq = m_crySeq;
	}
	if (!m_live->ok()) {
		return;
	}
	if (song != m_liveRequested) {
		m_live->requestSong(static_cast<uint16_t>(song));
		m_liveRequested = song;
	}
	const uint32_t seq = m_sfxSeq;
	if (seq != m_liveSfxSeq) {
		m_liveSfxSeq = seq;
		m_live->requestSfx(static_cast<uint16_t>(m_sfxId.load()));
	}
	const uint32_t crySeq = m_crySeq;
	if (crySeq != m_liveCrySeq) {
		m_liveCrySeq = crySeq;
		sendCry(*m_live);
	}
	m_live->setMutedChannels(m_sfxMask);
	int regs = m_outRegs;
	if (regs >= 0) {
		m_live->setOutputRegs(static_cast<uint8_t>(regs >> 8), static_cast<uint8_t>(regs & 0xFF));
	}
	const int options = m_options;
	if (options >= 0) {
		m_live->setOptions(static_cast<uint8_t>(options));
	}
	m_live->setDanger(m_danger);
	m_live->setGains(m_songGain, m_sfxGainNext);
	m_live->mixInto(stereo, frames, sampleRate, volume);
}

void GbDriverBgm::sendCry(GscPlayer& player) {
	const int tracks[4] = {m_cryTracks[0], m_cryTracks[1], m_cryTracks[2], m_cryTracks[3]};
	player.requestCry(static_cast<uint16_t>(m_cryId.load()), static_cast<uint16_t>(m_cryPitch.load()),
	                  static_cast<uint16_t>(m_cryLength.load()), tracks);
}

void GbDriverBgm::startRecording() {
	m_recording = true;
}

void GbDriverBgm::stopRecording() {
	m_recording = false;
	m_rec.reset();
}

void GbDriverBgm::mixRecording(int16_t* stereo, int frames, int sampleRate, int volume) {
	if (m_rec && m_rec->ok()) {
		m_rec->mixInto(stereo, frames, sampleRate, volume);
	}
}
