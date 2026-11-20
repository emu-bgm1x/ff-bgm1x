// ff-bgm1x: 소리 분류 창의 재생. 설명은 SoundPreview.h
#include "SoundPreview.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef FFBGM_DESMUME
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace melonDS;

namespace
{

u32 rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
void wr32(u8* p, u32 v) { memcpy(p, &v, 4); }

constexpr u32 kBase = 0x02000000;
constexpr u32 kImageSize = 16 * 1024 * 1024;  // 2 의 거듭제곱 (주소를 이 크기로 접는다)

} // namespace

SoundPreview::~SoundPreview()
{
    closeDevice();
}

// ---- 오디오 출력 ----

#ifdef FFBGM_DESMUME

// waveOut 버퍼 4개를 돌려 가며 채운다 (재생 창이 열려 있는 동안만 스레드가 돈다)
void SoundPreview::outputThread()
{
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * 4;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT wo = nullptr;
    if (waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
    {
        CloseHandle(event);
        running = false;
        return;
    }
    constexpr int kBufs = 4, kFrames = 2048;
    std::vector<s16> data[kBufs];
    WAVEHDR hdr[kBufs]{};
    for (int i = 0; i < kBufs; i++)
    {
        data[i].assign(kFrames * 2, 0);
        hdr[i].lpData = (LPSTR)data[i].data();
        hdr[i].dwBufferLength = kFrames * 4;
        waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE;
    }
    while (running)
    {
        bool queued = false;
        for (int i = 0; i < kBufs; i++)
        {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            renderOut(data[i].data(), kFrames);
            hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR));
            queued = true;
        }
        if (!queued) WaitForSingleObject(event, 50);
    }
    waveOutReset(wo);
    for (int i = 0; i < kBufs; i++) waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(wo);
    CloseHandle(event);
}

bool SoundPreview::openDevice(std::string& error)
{
    (void)error;
    if (deviceOpen) return true;
    rate = 48000;
    running = true;
    worker = std::thread(&SoundPreview::outputThread, this);
    deviceOpen = true;
    return true;
}

void SoundPreview::closeDevice()
{
    if (!deviceOpen) return;
    running = false;
    if (worker.joinable()) worker.join();
    deviceOpen = false;
}

#else

bool SoundPreview::openDevice(std::string& error)
{
    if (device) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        error = std::string("오디오를 열 수 없습니다: ") + SDL_GetError();
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &SoundPreview::callback;
    want.userdata = this;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device)
    {
        error = std::string("오디오 장치를 열 수 없습니다: ") + SDL_GetError();
        return false;
    }
    rate = have.freq;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void SoundPreview::closeDevice()
{
    if (!device) return;
    SDL_CloseAudioDevice(device);
    device = 0;
}

void SoundPreview::callback(void* user, Uint8* stream, int len)
{
    ((SoundPreview*)user)->renderOut((s16*)stream, len / 4);
}

#endif

void SoundPreview::renderOut(s16* out, int frames)
{
    memset(out, 0, (size_t)frames * 4);
    std::lock_guard<std::mutex> g(lock);
    if (paused) return;  // 일시정지: 무음, 재생 위치는 그대로
    if (useSynth && synth.isActive()) synth.render(out, frames, rate);
    if (useStrm) strm.render(out, frames, rate);
}

// ---- 불러오기 ----

u32 SoundPreview::appendRom(const SoundCatalog& cat, u32 off, u32 size)
{
    const u32 at = (used + 3) & ~3u;
    if (at + size > image.size()) return 0;
    std::vector<u8> tmp;
    if (!cat.readRom(off, size, tmp)) return 0;
    memcpy(&image[at], tmp.data(), size);
    used = at + size;
    return kBase + at;
}

u32 SoundPreview::loadBank(const SoundCatalog& cat, const SoundCatalog::Sdat& sd, int bankNo)
{
    if (bankNo < 0 || bankNo >= (int)sd.banks.size()) return 0;
    const SoundCatalog::BankEntry& b = sd.banks[bankNo];
    if (!b.fileSize) return 0;
    const u32 bank = appendRom(cat, b.fileOff, b.fileSize);
    if (!bank) return 0;
    // 뱅크 머리 뒤의 {파형 묶음 주소, 다음 연결} 4칸: 게임이 파형 묶음을 올린 뒤 채우는 자리
    for (int w = 0; w < 4; w++)
    {
        u32 addr = 0;
        const int warc = b.warc[w];
        if (warc >= 0 && warc < (int)sd.warcs.size() && sd.warcs[warc].fileSize)
            addr = appendRom(cat, sd.warcs[warc].fileOff, sd.warcs[warc].fileSize);
        if (0x18 + 8 * w + 8 <= b.fileSize) wr32(&image[bank - kBase + 0x18 + 8 * w], addr);
    }
    return bank;
}

bool SoundPreview::play(const SoundCatalog& cat, const std::string& key, std::string& error)
{
    stop();
    // 이름표: seq:S:N, ssar:S:A:N, strm:S:N
    std::vector<int> num;
    std::string type;
    {
        std::istringstream in(key);
        std::getline(in, type, ':');
        std::string part;
        while (std::getline(in, part, ':')) num.push_back(atoi(part.c_str()));
    }
    const SoundCatalog::Sdat* sd = num.empty() ? nullptr : cat.sdat(num[0]);
    if (!sd)
    {
        error = "롬에서 이 소리를 찾지 못했습니다.";
        return false;
    }
    if (!openDevice(error)) return false;
    const float gain = SoundCatalog::perceivedGain((float)cat.volume(key));

    if (type == "strm" && num.size() == 2 && num[1] < (int)sd->strms.size())
    {
        const SoundCatalog::StrmEntry& e = sd->strms[num[1]];
        std::vector<u8> data;
        NitroStrm::File f;
        if (!e.fileSize || !cat.readRom(e.fileOff, e.fileSize, data) ||
            !NitroStrm::parse(data.data(), (u32)data.size(), 0, (u32)data.size(), f))
        {
            error = "이 STRM 형식은 재생할 수 없습니다.";
            return false;
        }
        std::lock_guard<std::mutex> g(lock);
        strm.reset();  // 앞 데이터를 버리기 전에 멈춘다
        strmData.swap(data);
        strmFile = f;
        strmFile.base = strmData.data();
        NitroStrm::Mix left, right;
        left.pan = strmFile.channels == 2 ? 0 : 64;
        right.pan = strmFile.channels == 2 ? 127 : 64;
        left.volume = right.volume = std::clamp(e.volume, 0, 127) + 1;
        strm.setMix(0, left);
        strm.setMix(1, right);
        strm.setMaster(128);
        strm.setGain(gain);
        strm.play(&strmFile);
        useStrm = true;
        paused = false;
        return true;
    }

    std::lock_guard<std::mutex> g(lock);
    useSynth = useStrm = false;
    paused = false;
    image.assign(kImageSize, 0);
    used = 0x100;
    u32 seqBase = 0, seqOffset = 0, bank = 0;
    int volume = 127;
    bool hold = false;
    if (type == "seq" && num.size() == 2 && num[1] < (int)sd->seqs.size())
    {
        const SoundCatalog::SeqEntry& e = sd->seqs[num[1]];
        if (!e.fileSize) { error = "롬에서 이 시퀀스를 찾지 못했습니다."; return false; }
        const u32 file = appendRom(cat, e.fileOff, e.fileSize);
        if (!file) { error = "시퀀스를 불러오지 못했습니다."; return false; }
        seqBase = file + rd32(&image[file - kBase + 0x18]);
        bank = loadBank(cat, *sd, e.bank);
        volume = e.volume;
    }
    else if (type == "ssar" && num.size() == 3 && num[1] < (int)sd->ssars.size())
    {
        const SoundCatalog::SsarArc& a = sd->ssars[num[1]];
        if (!a.fileSize || num[2] < 0 || num[2] >= (int)a.seqs.size()) { error = "롬에서 이 소리를 찾지 못했습니다."; return false; }
        const u32 file = appendRom(cat, a.fileOff, a.fileSize);
        if (!file) { error = "효과음 묶음을 불러오지 못했습니다."; return false; }
        seqBase = file + a.dataOff;
        seqOffset = a.seqs[num[2]].offset;
        bank = loadBank(cat, *sd, a.seqs[num[2]].bank);
        volume = a.seqs[num[2]].volume;
        hold = true;
    }
    if (!seqBase || !bank)
    {
        error = "악기(뱅크)나 파형을 롬에서 불러오지 못했습니다.";
        return false;
    }
    synth.reset();
    synth.setMemory(image.data(), kImageSize - 1);
    if (!synth.prepare(0, seqBase, seqOffset, bank))
    {
        error = "재생기를 준비하지 못했습니다.";
        return false;
    }
    synth.setHoldOneShots(0, hold);
    synth.setPlayerVolume(0, volume);
    synth.setPlayerGain(0, gain);
    synth.start(0);
    useSynth = true;
    return true;
}

void SoundPreview::stop()
{
    std::lock_guard<std::mutex> g(lock);
    if (useSynth) synth.reset();
    if (useStrm) strm.reset();
    useSynth = useStrm = false;
    paused = false;
}

void SoundPreview::setPaused(bool on)
{
    std::lock_guard<std::mutex> g(lock);
    paused = on;
}

bool SoundPreview::isPaused()
{
    std::lock_guard<std::mutex> g(lock);
    return paused;
}

bool SoundPreview::isPlaying()
{
    std::lock_guard<std::mutex> g(lock);
    return (useSynth && synth.isActive()) || (useStrm && strm.isActive());
}

double SoundPreview::lengthSeconds()
{
    std::lock_guard<std::mutex> g(lock);
    if (useStrm && strmFile.rate > 0) return (double)strmFile.samples / strmFile.rate;
    return 0;
}
