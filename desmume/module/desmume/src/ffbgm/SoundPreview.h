// ff-bgm1x: 소리 분류 창의 "재생". 게임 메모리가 아니라 롬에서 직접 불러와 재생한다.
//
// 시퀀스(SSEQ)와 효과음 묶음(SSAR) 안의 소리는 악보만으로는 소리가 나지 않으므로, SDAT 목록에 적힌 악기(SBNK)와
// 파형 묶음(SWAR)을 함께 꺼내 따로 만든 메모리에 올리고 NitroSynth 로 연주한다 (뱅크의 파형 묶음 칸에 주소를 채운다).
// STRM 은 NitroStrm 으로 롬에서 읽은 파일을 바로 푼다.
// 게임이 일시정지되면 에뮬레이터의 오디오 출력이 멈추므로, 재생용 오디오 출력을 따로 연다 (재생할 때만).
// 출력: melonDS 는 SDL, DeSmuME(FFBGM_DESMUME) 는 Windows 의 waveOut.
#ifndef SOUNDPREVIEW_H
#define SOUNDPREVIEW_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef FFBGM_DESMUME
#include <SDL2/SDL.h>
#endif

#include "NitroStrm.h"
#include "NitroSynth.h"
#include "SoundCatalog.h"

class SoundPreview
{
public:
    ~SoundPreview();

    // key 의 소리를 처음부터 낸다. 실패하면 false 와 error
    bool play(const SoundCatalog& cat, const std::string& key, std::string& error);
    void stop();
    bool isPlaying();  // 일시정지 중이어도 true
    void setPaused(bool on);
    bool isPaused();
    // 지금 소리의 길이(초). STRM 만 알고, 모르면 0
    double lengthSeconds();

private:
    // 출력 쪽이 부른다: 스테레오 s16 frames 개를 채운다
    void renderOut(melonDS::s16* out, int frames);
    bool openDevice(std::string& error);
    void closeDevice();
    // SDAT 의 뱅크와 그 파형 묶음을 image 에 올리고 뱅크 주소를 돌려준다 (실패 0)
    melonDS::u32 loadBank(const SoundCatalog& cat, const SoundCatalog::Sdat& sd, int bank);
    // 롬의 파일을 image 에 붙이고 그 주소를 돌려준다 (실패 0)
    melonDS::u32 appendRom(const SoundCatalog& cat, melonDS::u32 off, melonDS::u32 size);

    std::mutex lock;
    int rate = 48000;
#ifdef FFBGM_DESMUME
    void outputThread();
    std::thread worker;
    std::atomic<bool> running{false};
    bool deviceOpen = false;
#else
    static void callback(void* user, Uint8* stream, int len);
    SDL_AudioDeviceID device = 0;
#endif
    std::vector<melonDS::u8> image;  // 시퀀스 재생용 메모리 (주소 0x02000000 부터)
    melonDS::u32 used = 0;
    NitroSynth synth;
    NitroStrm strm;
    NitroStrm::File strmFile;
    std::vector<melonDS::u8> strmData;
    bool useSynth = false, useStrm = false;
    bool paused = false;
};

#endif // SOUNDPREVIEW_H
