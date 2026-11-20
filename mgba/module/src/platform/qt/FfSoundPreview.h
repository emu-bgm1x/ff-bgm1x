/* ff-bgm1x: 소리 분류 창의 재생. 게임이 일시정지돼 있어도 들리도록 SDL 오디오 장치를 따로 연다.
 * GBA 곡은 롬에서 자체 재생기(Mp2kEngine)로, 울음소리는 마지막으로 나온 울음소리의 RAM 사본으로 낸다. */
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "GscPlayer.h"
#include "bgm_mirror.h"
#include "mp2k_engine.h"
#include "rom.h"

namespace QGBA {

class FfSoundPreview {
public:
	FfSoundPreview();
	~FfSoundPreview();

	// key: SoundCatalog 이름표. 롬은 복사해 두고 쓴다 (재생 중에 게임을 닫아도 안전하게)
	bool play(const Rom& rom, const BgmMultiMirror& mirror, const std::string& key, std::string& error);
	// GB/GBC: 게임 사운드 엔진을 옮긴 재생기로 곡(gbsong:)·효과음(gbsfx:)을 낸다
	// 울음소리(cry:all)는 cryId 가 0 이상일 때 그 울음소리를 pitch·length 로 낸다
	bool playGb(const std::vector<uint8_t>& rom, const GscTables& tables, const std::string& key, std::string& error,
	            int cryId = -1, int cryPitch = 0, int cryLength = 0);
	void stop();
	void setPaused(bool paused);
	bool isPlaying() const;
	bool isPaused() const;
	double lengthSeconds() const { return m_length; }  // 알면 초, 모르면 0

private:
	static void callback(void* user, uint8_t* stream, int len);
	bool openDevice(std::string& error);

	mutable std::mutex m_mutex;
	std::shared_ptr<Rom> m_rom;  // 재생기가 읽는 롬 사본
	std::unique_ptr<Mp2kEngine> m_engine;
	std::unique_ptr<GscPlayer> m_gsc;
	bool m_gscSfx = false;  // 효과음: 끝나면 멈춘다
	int m_gscFrames = 0;    // 효과음을 낸 지 몇 번째 출력인지 (시작 직후엔 아직 채널이 켜지기 전)
	uint32_t m_device = 0;
	int m_rate = 48000;
	bool m_paused = false;
	bool m_playing = false;
	double m_length = 0;
	std::vector<float> m_buf;
};

}
