// ff-bgm1x: 배속과 관계없이 GBA BGM·효과음을 1배속으로 낸다 (VBA-M 연결부). 설명은 vbam_bgm.h
#include "wx/ffbgm/vbam_bgm.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef ENABLE_SDL3
#include <SDL3/SDL.h>
#endif

#include "core/gba/gbaGlobals.h"
#include "core/gba/gbaSound.h"
#include "wx/audio/audio.h"
#include "wx/config/option-proxy.h"
#include "wx/ffbgm/bgm_mirror.h"
#include "wx/ffbgm/gb_driver_bgm.h"
#include "wx/ffbgm/rom.h"
#include "wx/wxvbam.h"

// gbaSound.cpp
extern long soundSampleRate;
// gb.cpp, gbSound.cpp
uint8_t gbReadMemory(uint16_t address);
void gbSoundSetFfbgmMuteMask(int mask);
extern uint8_t* gbWram;  // gbGlobals: CGB WRAM 8뱅크(뱅크 n 은 n * 0x1000), DMG 모드에서는 NULL

namespace ffbgm {

namespace {

// 재생기 출력 1.0 = GBA DirectSound 표본 128 (8비트 최대). VBA-M 은 DirectSound 표본 하나를
// Blip_Synth 음량 0.66/256 (음량 100% 기준)으로 16비트(32768)에 옮긴다 (gbaSound.cpp apply_volume).
constexpr float kEngineToInt16 = 128.0f * 0.66f / 256.0f * 32768.0f;
constexpr int kLiveRate = 48000;

std::mutex g_mtx;  // 롬·재생기 교체 vs 실시간 출력 콜백
Rom g_romImage;
std::vector<MPlayTableCandidate> g_cands;
BgmMultiMirror g_live(&g_romImage);  // 실시간 출력용 (게임 곡을 무음 처리한다)
BgmMultiMirror g_rec(&g_romImage);   // 녹화(게임 시간)용 (상태만 따라간다)
size_t g_romSize = 0;
bool g_gba = false;
uint64_t g_frame = 0;
bool g_wasEnabled = true;
int g_psgMask = 0;
bool g_recording = false;
int g_recRate = 0;
std::vector<float> g_recBuf;

std::atomic<bool> g_active{false};   // GBA: MP2K 표 후보가 있다 / GB: 지원 엔진을 찾았다
bool g_gb = false;                   // GB/GBC 게임
std::unique_ptr<QGBA::GbDriverBgm> g_gbDriver;  // GB/GBC 백엔드 (g_mtx 로 교체)
std::vector<int16_t> g_gbBuf;        // 실시간 출력 콜백 전용
std::atomic<bool> g_enabled{true};   // "Keep BGM at 1x speed"
std::atomic<float> g_gain{1.0f};     // VBA-M 음량 (설정값 / 100)

struct VbamBus final : GbaBus {
    uint8_t r8(uint32_t a) override {
        switch (a >> 24) {
            case 0x02:
                return g_workRAM ? g_workRAM[a & 0x3FFFF] : 0;
            case 0x03:
                return g_internalRAM ? g_internalRAM[a & 0x7FFF] : 0;
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0D: {
                const uint32_t o = a & 0x1FFFFFF;
                return (g_rom && o < g_romSize) ? g_rom[o] : 0;
            }
            default:
                return 0;
        }
    }
    void w8(uint32_t a, uint8_t v) override {
        if ((a >> 24) == 0x02 && g_workRAM)
            g_workRAM[a & 0x3FFFF] = v;
        else if ((a >> 24) == 0x03 && g_internalRAM)
            g_internalRAM[a & 0x7FFF] = v;
    }
};

struct VbamGbCore final : QGBA::GbMainCore {
    uint8_t r8(uint16_t address) override { return gbReadMemory(address); }
    // CGB 는 D000~DFFF 가 바뀌는 WRAM 뱅크라, 게임 설정 바이트처럼 뱅크 1 에 있는 값은 뱅크 1 에서 직접 읽는다
    uint8_t r8Bank1(uint16_t address) override {
        if (gbWram && address >= 0xD000 && address < 0xE000)
            return gbWram[0x1000 + (address - 0xD000)];
        return gbReadMemory(address);
    }
    void setChannelsDisabled(uint8_t mask) override { gbSoundSetFfbgmMuteMask(mask); }
};

int16_t Clamp16(float v) {
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return static_cast<int16_t>(v);
}

int UpdateLocked();

// GB/GBC 프레임: 백엔드가 게임 RAM 의 곡·효과음 상태를 읽고 원본 채널을 무음 처리한다.
// 숨겨진 코어는 실시간 출력 콜백(mixRealtime, 백엔드 안의 잠금)이 진행시킨다
void OnGbFrame();

// 대체 중인 소리가 쓰는 PSG 채널은 에뮬레이터 출력에서 끈다 (게임 메모리·진행은 그대로)
void ApplyPsgMask(int mask) {
    if (mask == g_psgMask)
        return;
    g_psgMask = mask;
    soundSetFfbgmMuteMask(mask);
}

#ifdef ENABLE_SDL3
SDL_AudioStream* g_stream = nullptr;
wxString g_streamDeviceId = wxT("\x01");  // 지금 스트림을 연 VBA-M 장치 설정값 (처음엔 일부러 맞지 않는 값)
std::vector<float> g_cbBuf;

void SDLCALL OnAudio(void*, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0)
        return;
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0)
        return;
    if (g_cbBuf.size() < static_cast<size_t>(frames) * 2)
        g_cbBuf.resize(static_cast<size_t>(frames) * 2);
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        // 에뮬레이션이 멈춰 있으면(일시정지) 재생기도 진행하지 않는다
        if (g_gb && g_gbDriver && g_enabled && !soundPaused) {
            if (g_gbBuf.size() < static_cast<size_t>(frames) * 2)
                g_gbBuf.resize(static_cast<size_t>(frames) * 2);
            std::fill(g_gbBuf.begin(), g_gbBuf.begin() + frames * 2, 0);
            g_gbDriver->mixRealtime(g_gbBuf.data(), frames, kLiveRate, static_cast<int>(0x100 * g_gain.load()));
            for (int i = 0; i < frames * 2; ++i)
                g_cbBuf[i] = g_gbBuf[i] / 32768.0f;
        } else if (!g_gb && g_active && g_enabled && !soundPaused) {
            g_live.render(g_cbBuf.data(), frames);
            const float scale = kEngineToInt16 * g_gain.load() / 32768.0f;
            for (int i = 0; i < frames * 2; ++i)
                g_cbBuf[i] = std::min(1.0f, std::max(-1.0f, g_cbBuf[i] * scale));
        } else {
            std::fill(g_cbBuf.begin(), g_cbBuf.begin() + frames * 2, 0.0f);
        }
    }
    SDL_PutAudioStreamData(stream, g_cbBuf.data(), frames * static_cast<int>(sizeof(float) * 2));
}

void CloseStream() {
    if (g_stream) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    }
}

// VBA-M 오디오 설정에서 고른 장치(드라이버별 ID)의 이름으로 SDL 장치를 찾는다. 없으면 기본 장치
SDL_AudioDeviceID FindSdlDevice(const wxString& configured_id) {
    if (configured_id.empty())
        return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    wxString name;
    for (const audio::AudioDevice& d : audio::EnumerateAudioDevices(OPTION(kSoundAudioAPI).Get())) {
        if (d.id == configured_id) {
            name = d.name;
            break;
        }
    }
    if (name.empty())
        return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    for (int i = 0; ids && i < count; ++i) {
        const char* n = SDL_GetAudioDeviceName(ids[i]);
        if (n && wxString::FromUTF8(n) == name) {
            found = ids[i];
            break;
        }
    }
    SDL_free(ids);
    return found;
}

// 장치 설정이 바뀌었거나 아직 열지 않았으면 (다시) 연다
void EnsureStream() {
    const wxString& want = OPTION(kSoundAudioDevice).Get();
    if (g_stream && want == g_streamDeviceId)
        return;
    CloseStream();
    g_streamDeviceId = want;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
        return;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kLiveRate;
    g_stream = SDL_OpenAudioDeviceStream(FindSdlDevice(want), &spec, &OnAudio, nullptr);
    if (g_stream)
        SDL_ResumeAudioStreamDevice(g_stream);
}
#else
void EnsureStream() {}
void CloseStream() {}
#endif

}  // namespace

void OnGbaLoaded(const uint8_t* rom, size_t size) {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_live.reset();
        g_rec.reset();
        g_romImage.loadFromMemory(rom, size);
        g_cands = findMPlayTables(g_romImage);
        g_romSize = size;
        g_gba = true;
        g_gb = false;
        g_gbDriver.reset();
        g_frame = 0;
        g_wasEnabled = true;
        g_recording = false;
        EngineConfig cfg;
        cfg.outRate = kLiveRate;
        g_live.configure(cfg);
        g_active = !g_cands.empty();
    }
    ApplyPsgMask(0);
}

void OnGbLoaded(const uint8_t* rom, size_t size) {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_live.reset();
        g_rec.reset();
        g_cands.clear();
        g_gba = false;
        g_gb = true;
        g_recording = false;
        g_gbDriver.reset();
        std::vector<uint8_t> data(rom, rom + size);
        QGBA::GbDriverProfile profile;
        if (rom && size && QGBA::detectGbDriver(data, profile))
            g_gbDriver.reset(new QGBA::GbDriverBgm(data, profile));
        g_active = g_gbDriver != nullptr;
    }
    gbSoundSetFfbgmMuteMask(0);
}

void OnUnload() {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_gb = false;
        g_gbDriver.reset();
        g_live.reset();
        g_rec.reset();
        g_gba = false;
        g_active = false;
        g_cands.clear();
        g_romSize = 0;
    }
    ApplyPsgMask(0);
    gbSoundSetFfbgmMuteMask(0);
    CloseStream();
}

void OnFrame() {
    if (g_gb) {
        OnGbFrame();
        return;
    }
    if (!g_gba)
        return;
    const bool enabled = OPTION(kSoundFfbgmBgm1x);
    const bool sfx1x = OPTION(kSoundFfbgmSfx1x);
    const float sfxGain = std::min(std::max(OPTION(kSoundFfbgmSfxVolume).Get(), 0), 256) / 256.0f;
    g_live.setSfx1x(sfx1x);
    g_rec.setSfx1x(sfx1x);
    g_live.setSfxVolume(sfxGain);
    g_rec.setSfxVolume(sfxGain);
    g_gain = OPTION(kSoundVolume).Get() / 100.0f;
    g_enabled = enabled;
    if (g_active)
        EnsureStream();

    // 재생기 상태를 바꾸는 동안 실시간 출력 콜백(SDL 오디오 스레드)이 같은 재생기를 쓰지 않게 한다.
    // BgmMultiMirror::onFrame/stopAll/setRuntimeParams 는 내부 잠금 없이 재생기(엔진)를 고치므로,
    // 콜백의 render 와 겹치면 메모리가 깨질 수 있다 (같은 롬을 다시 열 때 힙 손상으로 종료된 적 있음).
    // EnsureStream(SDL 호출)은 이 잠금 밖에서 한다: 스트림을 닫는 쪽이 콜백 끝을 기다리므로 교착을 피한다.
    int psgMask = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        psgMask = UpdateLocked();
    }
    ApplyPsgMask(psgMask);
}

namespace {

// OnFrame 의 본체 (g_mtx 를 잡은 채로). 돌려주는 값은 출력에서 끌 PSG 채널
int UpdateLocked() {
    const bool enabled = g_enabled;
    VbamBus bus;
    if (!enabled || g_cands.empty()) {
        // 끌 때: 대체 재생을 멈추고, 무음 처리하던 게임 곡을 처음부터 다시 시작시킨다
        if (g_wasEnabled && g_live.located()) {
            g_live.releaseMute(bus);
            g_rec.stopAll();
        }
        g_wasEnabled = enabled;
        return 0;
    }
    g_wasEnabled = true;
    ++g_frame;
    if (!g_live.located() || !g_live.stillValid(bus)) {
        if (g_live.located()) {
            // 게임이 MP2K 를 내려놓음 (모드 전환 등): 대체 재생도 멈춘다
            g_live.reset();
            g_rec.reset();
        }
        if (g_frame % 30 != 0 || !g_live.locate(bus, g_cands))
            return 0;
        g_rec.locate(bus, g_cands);
    }
    if (g_frame % 60 == 1) {
        const SoundInfoSnapshot si = readSoundInfo(bus);
        if (si.valid) {
            g_live.setRuntimeParams(si);
            g_rec.setRuntimeParams(si);
        }
    }
    g_live.onFrame(bus, true);

    // 녹화: 녹화를 시작할 때 녹화용 재생기를 녹화 표본율로 맞추고 처음부터 따라간다
    GameArea* panel = wxGetApp().frame ? wxGetApp().frame->GetPanel() : nullptr;
    const bool rec = panel && panel->IsRecording();
    if (rec && (!g_recording || g_recRate != static_cast<int>(soundSampleRate))) {
        EngineConfig cfg;
        cfg.outRate = static_cast<int>(soundSampleRate);
        g_rec.configure(cfg);
        g_rec.stopAll();
        g_recRate = cfg.outRate;
    }
    g_recording = rec;
    if (rec)
        g_rec.onFrame(bus, false);

    return g_live.replacedPsgMask();
}

}  // namespace

bool SpeedHold() {
    // GB/GBC 는 연출 판정이 없다 (mGBA 판과 같음)
    return g_gba && g_active && OPTION(kSoundFfbgmBgm1x) && !OPTION(kSoundFfbgmFfCutscenes) &&
           g_live.cutsceneActive();
}

void MixRecording(int16_t* stereo, int frames) {
    if (g_gb) {
        if (g_gbDriver && g_enabled && g_recording && frames > 0)
            g_gbDriver->mixRecording(stereo, frames, static_cast<int>(soundSampleRate),
                                     static_cast<int>(0x100 * g_gain.load()));
        return;
    }
    if (!g_gba || !g_active || !g_enabled || !g_recording || frames <= 0)
        return;
    if (g_recBuf.size() < static_cast<size_t>(frames) * 2)
        g_recBuf.resize(static_cast<size_t>(frames) * 2);
    g_rec.render(g_recBuf.data(), frames);
    const float scale = kEngineToInt16 * g_gain.load();
    for (int i = 0; i < frames * 2; ++i)
        stereo[i] = Clamp16(stereo[i] + g_recBuf[i] * scale);
}

namespace {

void OnGbFrame() {
    const bool enabled = OPTION(kSoundFfbgmBgm1x);
    const bool sfx1x = OPTION(kSoundFfbgmSfx1x);
    g_gain = OPTION(kSoundVolume).Get() / 100.0f;
    g_enabled = enabled;
    if (!g_gbDriver)
        return;
    EnsureStream();
    VbamGbCore core;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_gbDriver->onFrame(core, enabled, sfx1x);
        // 녹화: 시작할 때 녹화용 숨겨진 코어를 새로 만들고, 끝나면 버린다
        GameArea* panel = wxGetApp().frame ? wxGetApp().frame->GetPanel() : nullptr;
        const bool rec = panel && panel->IsRecording();
        if (rec && !g_recording)
            g_gbDriver->startRecording();
        else if (!rec && g_recording)
            g_gbDriver->stopRecording();
        g_recording = rec;
    }
}

}  // namespace

}  // namespace ffbgm
