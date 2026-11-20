/* ff-bgm1x: GB/GBC 게임용 BGM 1배속 유지.
 * 원본 코어가 재생 중인 곡·효과음 번호를 읽어, 게임 사운드 엔진을 옮긴 자체 재생기(GscPlayer)로
 * 실시간 1배속으로 낸다. 원본 코어의 음악 채널은 하드웨어 채널 단위로 무음 처리한다.
 * 사운드 엔진마다 다른 정보(진입점, 곡 번호 위치, 채널 구조)는 엔진별 프로필로 ROM 에서 찾는다. */
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <mgba/core/interface.h>

#include "GscPlayer.h"
#include "sound_catalog.h"

struct mCore;

namespace QGBA {

struct GbDriverProfile {
	std::string engine;          // 감지된 엔진 이름
	uint16_t playMusic = 0;      // 곡 시작 루틴 (bank 0, DE = 곡 번호)
	uint16_t updateSound = 0;    // 프레임마다 부르는 갱신 루틴 (bank 0)
	uint16_t musicId = 0;        // 현재 곡 번호가 있는 RAM (16비트)
	uint16_t channelBase = 0;    // 채널 구조체 배열 시작 RAM
	uint16_t channelStride = 0;  // 채널 구조체 크기
	uint16_t flagsOffset = 0;    // 채널 구조체 안의 on/off 플래그 위치
	uint8_t onMask = 0;          // on 비트
	int sfxChannelStart = 4;     // 효과음 채널 번호 시작 (하드웨어 채널 i 는 sfxChannelStart + i)
	uint16_t driverRam = 0xDE00; // 숨겨진 코어에 넣는 호출 프로그램 위치 (게임 코드가 돌지 않으므로 비어 있음)
	uint16_t stackTop = 0xDFF0;
	// 효과음 (없으면 playSfx = 0: 효과음 1배속을 하지 않는다)
	uint16_t playSfx = 0;        // 효과음 시작 루틴 (오디오 bank, DE = 효과음 번호)
	uint8_t sfxBank = 0;         // 그 bank
	uint8_t hRomBank = 0;        // 현재 bank 를 기억하는 HRAM (FF00 + 이 값)
	uint16_t curSfx = 0;         // 현재 효과음 번호 RAM (우선순위 판단용, 시작 루틴 전에 써 둔다)
	uint8_t cryFlag = 0;         // 채널 플래그: 울음소리 (대체하지 않는다)
	uint8_t addressOffset = 0;   // 채널 구조체 안의 현재 읽는 위치 (16비트)
	uint16_t optionsAddr = 0;    // 게임 설정 바이트(wOptions). 드라이버가 여기서 스테레오 비트를 읽는다 (0 이면 없음)
	GscTables tables;            // 자체 재생기(GscPlayer)가 쓰는 엔진 표
};

// 알려진 엔진 프로필들을 차례로 시도한다. 찾으면 true.
bool detectGbDriver(const std::vector<uint8_t>& rom, GbDriverProfile& out);

// BgmReplacer 가 GB 플랫폼일 때 쓰는 백엔드
class GbDriverBgm {
public:
	GbDriverBgm(const std::vector<uint8_t>& rom, const GbDriverProfile& profile);
	~GbDriverBgm();

	const GbDriverProfile& profile() const { return m_profile; }

	void onFrame(mCore* core, bool enabled, bool sfx1x);  // 에뮬레이션 스레드
	// 소리 분류 (BgmReplacer 가 가진 목록). 이름표: gbsong:<번호>, gbsfx:<번호>, cry:all
	void setCatalog(SoundCatalog* catalog) { m_catalog = catalog; }
	// "RAM 곡 읽기": 켜져 있고 울음소리 전체를 1배속으로 정했으면, 원본 울음소리의 음높이·길이를 읽어 1배속으로 낸다
	void setRamSongs(bool on) { m_ramSongs = on; }
	// 마지막으로 1배속으로 낸 울음소리 (미리 듣기용). 없으면 false
	bool lastCry(int& id, int& pitch, int& length) const {
		if (!m_crySeq) return false;
		id = m_cryId;
		pitch = m_cryPitch;
		length = m_cryLength;
		return true;
	}
	// 사용자가 연출로 정한 소리가 나오는 중 (배속을 멈춘다)
	bool cutscene() const { return m_cutscene; }
	const std::vector<uint8_t>& rom() const { return m_rom; }
	void onReset();
	void releaseMute(mCore* core);

	void mixRealtime(int16_t* stereo, int frames, int sampleRate, int volume);  // 오디오 스레드

	void startRecording();
	void stopRecording();
	void recSample(int16_t& left, int16_t& right);  // 에뮬레이션 스레드 (녹화 스트림)

private:
	std::vector<uint8_t> m_rom;
	GbDriverProfile m_profile;

	std::mutex m_liveMutex;
	std::unique_ptr<GscPlayer> m_live;   // 오디오 스레드에서 생성/사용
	std::atomic<int> m_liveSong{-1};
	std::atomic<uint8_t> m_sfxMask{0};
	std::atomic<int> m_outRegs{-1};
	std::atomic<int> m_options{-1};  // 원본의 게임 설정 바이트
	std::atomic<bool> m_danger{false};  // 원본의 낮은 체력 경고음 (wLowHealthAlarm 비트 7)
	std::atomic<bool> m_resetLive{false};
	int m_liveRequested = -1;  // 오디오 스레드 전용

	std::unique_ptr<GscPlayer> m_rec;    // 에뮬레이션 스레드
	std::atomic<bool> m_recording{false};

	int m_lastSong = -1;
	bool m_muting = false;

	// 효과음 1배속: 원본에서 새로 시작된 효과음 (번호, 순번). 오디오 스레드는 순번이 바뀌면 숨겨진 코어에 낸다
	std::atomic<int> m_sfxId{0};
	std::atomic<uint32_t> m_sfxSeq{0};
	std::atomic<float> m_songGain{1.0f};     // 소리 분류 창의 볼륨: 지금 곡
	std::atomic<float> m_sfxGainNext{1.0f};  // 마지막으로 1배속으로 낸 효과음
	uint32_t m_liveSfxSeq = 0;  // 오디오 스레드 전용
	struct SfxChannelState {
		bool on = false;
		uint16_t id = 0;
		uint16_t address = 0;
	};
	SfxChannelState m_sfxState[4];

	// ---- 소리 분류 ----
	// 나온 소리를 세고(분류 창이 열려 있으면 목록에 넣고), 곡의 사용자 지정을 읽는다
	void noteSounds(mCore* core);
	void setNativeGains(mCore* core, const int gains[4]);
	SoundCatalog* m_catalog = nullptr;
	std::atomic<bool> m_cutscene{false};
	std::atomic<bool> m_ramSongs{false};
	// 1배속으로 낼 울음소리 (순번이 바뀌면 재생기에 낸다)
	std::atomic<int> m_cryId{0}, m_cryPitch{0}, m_cryLength{0};
	std::atomic<int> m_cryTracks[4] = {{-1}, {-1}, {-1}, {-1}};
	std::atomic<uint32_t> m_crySeq{0};
	uint32_t m_liveCrySeq = 0;  // 오디오 스레드 전용
	void sendCry(GscPlayer& player);
	int m_cntSong = -1;
	struct CountState {
		bool on = false;
		bool cry = false;
		uint16_t id = 0;
		uint16_t address = 0;
	};
	CountState m_cnt[4];
	bool m_gainsSet = false;  // 원본 채널 볼륨 배율을 바꿔 둠
};

}
