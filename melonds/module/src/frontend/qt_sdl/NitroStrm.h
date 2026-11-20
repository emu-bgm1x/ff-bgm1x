// ff-bgm1x: 스트림 곡(SDAT 의 STRM)을 롬에서 직접 풀어 실제 시간(1배속)으로 내는 재생기.
//
// 스트림 곡은 악보(SSEQ)가 아니라 녹음된 소리라서, 게임(ARM9)이 롬에서 조금씩 읽어 버퍼에 채우고 ARM7 의
// PCM 채널이 그 버퍼를 연주한다. 어느 STRM 인지는 게임이 카트에서 읽는 롬 위치로 안다 (BgmReplacer).
// 이 재생기는 그 STRM 파일을 처음부터 풀어 반복 구간대로 되풀이한다. 지원 형식: PCM8, PCM16, IMA-ADPCM.
#ifndef NITROSTRM_H
#define NITROSTRM_H

#include <string>
#include <vector>

#ifdef FFBGM_DESMUME
#include "ffbgm_glue.h"
#else
#include "types.h"
#endif

class NitroStrm
{
public:
    struct File
    {
        melonDS::u32 start = 0, end = 0;  // 롬 안 파일 범위
        melonDS::u32 data = 0;            // 첫 데이터 블록의 롬 위치
        int type = 0;                     // 0 PCM8, 1 PCM16, 2 IMA-ADPCM
        bool loop = false;
        int channels = 1;
        int rate = 32768;
        melonDS::u32 loopStart = 0, samples = 0;
        melonDS::u32 blocks = 0, blockLen = 0, blockSamples = 0, lastLen = 0, lastSamples = 0;
        std::string name;
        const melonDS::u8* base = nullptr;  // 이 파일을 담은 메모리 (start 등은 이 안의 위치)
        melonDS::u32 baseLen = 0;
    };

    // STRM 파일 헤더를 읽는다 (rom = 파일을 담은 메모리, 재생하는 동안 살아 있어야 한다). 지원하지 않는 형식이면 false
    static bool parse(const melonDS::u8* rom, melonDS::u32 romlen, melonDS::u32 start, melonDS::u32 size, File& out);

    // 이 파일을 처음부터 낸다 (같은 파일이 이미 나오는 중이면 그대로 둔다). nullptr = 짧게 줄이며 멈춤
    void play(const File* f);
    bool isActive() const { return cur != nullptr; }
    // 줄이지 않고 바로 멈춘다 (지금 파일의 데이터를 곧 버릴 때)
    void reset() { cur = pending = nullptr; stopping = false; primed = false; }
    const File* current() const { return cur; }

    // 하드웨어 채널과 같은 방식으로 섞기 위한 값 (게임이 그 스트림 채널에 정한 음량·팬, 주 음량)
    struct Mix
    {
        int volume = 128;  // 0~128 (127 은 128 로)
        int shift = 4;     // melonDS 의 VolumeShift (4,3,2,0)
        int pan = 64;      // 0~127
    };
    void setMix(int ch, const Mix& m) { mix[ch & 1] = m; }
    void setMaster(int v) { master = v; }
    // 사용자 볼륨 (1 = 그대로)
    void setGain(float g) { userGain = g; }

    // 실제 시간 rate(Hz)로 frames 개를 만들어 out(스테레오 s16)에 더한다
    void render(melonDS::s16* out, int frames, int rate);

private:
    bool fetch(melonDS::s32* s);  // 다음 표본 (채널별). 끝나면 false
    bool decodeBlock(melonDS::u32 block);

    const File* cur = nullptr;
    const File* pending = nullptr;  // 짧게 줄인 뒤 낼 다음 파일
    bool stopping = false;
    int fadeLeft = 0, fadeLen = 1;
    melonDS::u32 pos = 0;  // 다음에 꺼낼 표본 번호
    melonDS::u32 block = 0xFFFFFFFF;
    std::vector<melonDS::s16> buf[2];
    melonDS::s32 s0[2] = {0, 0}, s1[2] = {0, 0};
    double frac = 0;
    bool primed = false;
    Mix mix[2];
    int master = 128;
    float userGain = 1.0f;
};

#endif // NITROSTRM_H
