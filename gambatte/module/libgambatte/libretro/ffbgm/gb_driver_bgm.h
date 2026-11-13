/* ff-bgm1x: GB/GBC 게임용 BGM 1배속 유지. (mGBA 판 GbDriverBgm 을 VBA-M 에 옮긴 것: 원본 코어는 GbMainCore 로 접근)
 * 숨겨진 두 번째 GB 코어에 같은 ROM 을 올리고, 게임 자체의 사운드 드라이버(곡 재생/프레임 갱신 루틴)만
 * 실시간 1배속으로 호출해 음악을 만든다. 원본 코어의 음악 채널은 하드웨어 채널 단위로 무음 처리한다.
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

struct mCore;

namespace QGBA {

// 원본(게임이 돌고 있는) 코어 접근: 메모리 읽기와, 하드웨어 채널 무음 처리
struct GbMainCore {
	virtual ~GbMainCore() = default;
	virtual uint8_t r8(uint16_t address) = 0;
	// WRAM 뱅크 1 기준으로 읽는다 (D000~DFFF 는 CGB 에서 뱅크가 바뀌므로)
	virtual uint8_t r8Bank1(uint16_t address) { return r8(address); }
	// 비트 i = 채널 i+1 을 출력에서 끈다 (게임 메모리·진행은 그대로)
	virtual void setChannelsDisabled(uint8_t mask) = 0;
};

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
};

// 알려진 엔진 프로필들을 차례로 시도한다. 찾으면 true.
bool detectGbDriver(const std::vector<uint8_t>& rom, GbDriverProfile& out);

// 게임 드라이버만 실행하는 숨겨진 GB 코어
class GbShadowPlayer {
public:
	GbShadowPlayer(const std::vector<uint8_t>& rom, const GbDriverProfile& profile, bool captureStream);
	~GbShadowPlayer();
	bool ok() const { return m_core != nullptr; }
	mCore* core() const { return m_core; }  // 진단용

	void requestSong(uint16_t id);       // 다음 프레임 시작 전에 곡을 바꾼다
	// 다음 프레임 시작 전에 효과음을 낸다. 이 코어에서 앞의 효과음이 아직 울리고 있으면 건너뛴다
	// (배속 중에는 게임이 같은 소리를 빨리 거듭 내므로, 매번 다시 틀면 소리가 나기도 전에 끊긴다)
	void requestSfx(uint16_t id);
	void setMutedChannels(uint8_t mask); // 원본에서 효과음이 쓰는 하드웨어 채널은 음악을 끈다
	// 원본 코어의 NR50(마스터 볼륨)/NR51(좌우 출력)을 따라간다. 스테레오 설정, 페이드, 울음소리 중 볼륨 감소가 반영된다
	void setOutputRegs(uint8_t nr50, uint8_t nr51);
	// 원본의 게임 설정 바이트(스테레오 등)를 따라간다. 다르면 드라이버가 원본과 다른 NR51 을 써서 지직거린다
	void setOptions(uint8_t value);
	void runFrame();

	// 실시간용: sampleRate 로 frames 개를 만들어 stereo 에 더한다
	void mixInto(int16_t* stereo, int frames, int sampleRate, int volume);
	// 녹화용: 원본 코어와 같은 레이트로 쌓인 샘플을 하나 꺼낸다
	bool popStreamSample(int16_t& left, int16_t& right);

private:
	struct Capture {
		mAVStream base;
		GbShadowPlayer* self;
	};
	static void _capture(mAVStream*, int16_t left, int16_t right);

	void applyRequest();
	bool sfxBusy() const;

	GbDriverProfile m_profile;
	mCore* m_core = nullptr;
	std::vector<uint8_t> m_rom;
	std::vector<uint32_t> m_video;
	std::atomic<int> m_pending{-1};
	std::atomic<int> m_pendingSfx{-1};
	std::atomic<uint8_t> m_mutedMask{0};
	std::atomic<int> m_outRegs{-1};  // (nr50 << 8) | nr51, -1 이면 아직 없음
	std::atomic<int> m_options{-1};  // 게임 설정 바이트, -1 이면 아직 없음
	int m_rate = 0;
	Capture m_capture;
	std::deque<int16_t> m_streamFifo;
};

// BgmReplacer 가 GB 플랫폼일 때 쓰는 백엔드
class GbDriverBgm {
public:
	GbDriverBgm(const std::vector<uint8_t>& rom, const GbDriverProfile& profile);
	~GbDriverBgm();

	const GbDriverProfile& profile() const { return m_profile; }

	void onFrame(GbMainCore& core, bool enabled, bool sfx1x);  // 에뮬레이션 스레드
	void onReset();
	void releaseMute(GbMainCore& core);

	void mixRealtime(int16_t* stereo, int frames, int sampleRate, int volume);  // 오디오 스레드

	void startRecording();
	void stopRecording();
	// 녹화용: 게임 시간 기준 소리를 frames 개 만들어 stereo 에 더한다 (에뮬레이션 스레드)
	void mixRecording(int16_t* stereo, int frames, int sampleRate, int volume);

private:
	std::vector<uint8_t> m_rom;
	GbDriverProfile m_profile;

	std::mutex m_liveMutex;
	std::unique_ptr<GbShadowPlayer> m_live;   // 오디오 스레드에서 생성/사용
	std::atomic<int> m_liveSong{-1};
	std::atomic<uint8_t> m_sfxMask{0};
	std::atomic<int> m_outRegs{-1};
	std::atomic<int> m_options{-1};  // 원본의 게임 설정 바이트
	std::atomic<bool> m_resetLive{false};
	int m_liveRequested = -1;  // 오디오 스레드 전용

	std::unique_ptr<GbShadowPlayer> m_rec;    // 에뮬레이션 스레드
	std::atomic<bool> m_recording{false};

	int m_lastSong = -1;
	bool m_muting = false;

	// 효과음 1배속: 원본에서 새로 시작된 효과음 (번호, 순번). 오디오 스레드는 순번이 바뀌면 숨겨진 코어에 낸다
	std::atomic<int> m_sfxId{0};
	std::atomic<uint32_t> m_sfxSeq{0};
	uint32_t m_liveSfxSeq = 0;  // 오디오 스레드 전용
	struct SfxChannelState {
		bool on = false;
		uint16_t id = 0;
		uint16_t address = 0;
	};
	SfxChannelState m_sfxState[4];
};

}
