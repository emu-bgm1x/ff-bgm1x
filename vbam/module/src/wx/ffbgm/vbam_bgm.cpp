// ff-bgm1x: 배속과 관계없이 GBA BGM·효과음을 1배속으로 낸다 (VBA-M 연결부). 설명은 vbam_bgm.h
#include "wx/ffbgm/vbam_bgm.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
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
#include "wx/ffbgm/sound_catalog.h"
#include "wx/wxvbam.h"

// gbaSound.cpp
extern long soundSampleRate;
// gb.cpp, gbSound.cpp
uint8_t gbReadMemory(uint16_t address);
void gbSoundSetFfbgmMuteMask(int mask);
void gbSoundSetFfbgmGain(int channel, int q8);
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
SoundCatalog g_catalog;              // 소리 분류 (게임별 파일)
std::unordered_map<uint32_t, int> g_songNumbers;  // GBA 곡 머리 -> 곡 번호 (분류 창이 처음 쓸 때 찾는다)
bool g_songNumbersDone = false;
bool g_catalogInit = false;

void InitCatalog() {
    if (g_catalogInit)
        return;
    g_catalogInit = true;
    g_catalog.SetStorageDir(SoundCatalog::DefaultStorageDir());
    g_live.setCatalog(&g_catalog, true);
    g_rec.setCatalog(&g_catalog, false);
    g_live.setSongNumbers(&g_songNumbers);
    g_rec.setSongNumbers(&g_songNumbers);
}

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
    void setChannelGain(int ch, int q8) override { gbSoundSetFfbgmGain(ch, q8); }
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
    PreviewStop();
    InitCatalog();
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_live.reset();
        g_rec.reset();
        g_romImage.loadFromMemory(rom, size);
        g_cands = findMPlayTables(g_romImage);
        g_songNumbers.clear();
        g_songNumbersDone = false;
        g_catalog.SetGame(g_romImage.gameCode(), g_romImage.gameTitle());
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
    PreviewStop();
    InitCatalog();
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
        if (rom && size && QGBA::detectGbDriver(data, profile)) {
            g_gbDriver.reset(new QGBA::GbDriverBgm(data, profile));
            g_gbDriver->setCatalog(&g_catalog);
            // 게임 코드: 롬 머리의 제목(0x134~0x143)과 전체 체크섬(0x14E). 빈칸 등은 _ 로 (mGBA 판과 같음)
            std::string title;
            for (size_t i = 0x134; i < 0x144 && i < size; ++i) {
                const uint8_t c = rom[i];
                if (c == 0)
                    break;
                title.push_back(c >= 0x21 && c < 0x7F ? static_cast<char>(c) : '_');
            }
            char sum[8] = {};
            if (size > 0x14F)
                std::snprintf(sum, sizeof(sum), "%02X%02X", rom[0x14E], rom[0x14F]);
            g_catalog.SetGame("GB-" + title + "-" + sum, title);
        }
        g_active = g_gbDriver != nullptr;
    }
    gbSoundSetFfbgmMuteMask(0);
}

void OnUnload() {
    PreviewStop();
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
    const float sfxGain =
        SoundCatalog::PerceivedGain(std::min(std::max(OPTION(kSoundFfbgmSfxVolume).Get(), 0), 256) * 100.0f / 256.0f);
    g_live.setSfx1x(sfx1x);
    g_rec.setSfx1x(sfx1x);
    const bool ramSongs = OPTION(kSoundFfbgmRamSongs);
    g_live.setRamSongs(ramSongs);
    g_rec.setRamSongs(ramSongs);
    if (g_catalog.Active() && !g_songNumbersDone) {
        g_songNumbersDone = true;
        g_songNumbers = findSongNumbers(g_romImage);
    }
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
        // 기능이 꺼져 있어도 분류 창이 열려 있으면 나온 소리를 목록에 넣는다
        if (!g_cands.empty() && g_catalog.Collecting()) {
            ++g_frame;
            if (!g_live.located() || !g_live.stillValid(bus)) {
                if (g_frame % 30 != 0 || !g_live.locate(bus, g_cands))
                    return 0;
            }
            g_live.noteOnly(bus);
        }
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
    if (!g_active || !OPTION(kSoundFfbgmBgm1x) || OPTION(kSoundFfbgmFfCutscenes))
        return false;
    // GB/GBC: 사용자가(또는 울음소리 기본값이) 연출로 정한 소리가 나오는 동안
    if (g_gb)
        return g_gbDriver && g_gbDriver->cutscene();
    return g_gba && g_live.cutsceneActive();
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
        g_gbDriver->setRamSongs(OPTION(kSoundFfbgmRamSongs));
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

// ================================================================ 소리 분류 창

SoundCatalog* Catalog() {
    InitCatalog();
    return g_catalog.GameCode().empty() ? nullptr : &g_catalog;
}

bool CatalogUsable() {
    return g_active;
}

namespace {

std::mutex g_pvMtx;
std::unique_ptr<Rom> g_pvRom;
std::unique_ptr<Mp2kEngine> g_pvEngine;
std::unique_ptr<QGBA::GscPlayer> g_pvGsc;
bool g_pvSfx = false;
int g_pvFrames = 0;
bool g_pvPlaying = false;
bool g_pvPaused = false;
double g_pvLength = 0;
std::vector<float> g_pvBuf;
std::vector<int16_t> g_pvPcm;

#ifdef ENABLE_SDL3
SDL_AudioStream* g_pvStream = nullptr;

void SDLCALL OnPreviewAudio(void*, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0)
        return;
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0)
        return;
    if (g_pvBuf.size() < static_cast<size_t>(frames) * 2)
        g_pvBuf.resize(static_cast<size_t>(frames) * 2);
    std::fill(g_pvBuf.begin(), g_pvBuf.begin() + frames * 2, 0.0f);
    {
        std::lock_guard<std::mutex> lock(g_pvMtx);
        if (g_pvPlaying && !g_pvPaused) {
            if (g_pvGsc) {
                if (g_pvPcm.size() < static_cast<size_t>(frames) * 2)
                    g_pvPcm.resize(static_cast<size_t>(frames) * 2);
                std::fill(g_pvPcm.begin(), g_pvPcm.begin() + frames * 2, 0);
                g_pvGsc->mixInto(g_pvPcm.data(), frames, kLiveRate, 0x100);
                for (int i = 0; i < frames * 2; ++i)
                    g_pvBuf[i] = g_pvPcm[i] / 32768.0f;
                // 효과음은 끝나면 멈춘다 (시작 직후 몇 번은 아직 채널이 켜지기 전일 수 있다)
                if (g_pvSfx && ++g_pvFrames > 4 && !g_pvGsc->sfxPlaying())
                    g_pvPlaying = false;
            } else if (g_pvEngine) {
                if (!g_pvEngine->currentSong() || g_pvEngine->finished()) {
                    g_pvPlaying = false;
                } else {
                    g_pvEngine->render(g_pvBuf.data(), frames);
                    const float scale = kEngineToInt16 / 32768.0f;
                    for (int i = 0; i < frames * 2; ++i)
                        g_pvBuf[i] = std::min(1.0f, std::max(-1.0f, g_pvBuf[i] * scale));
                }
            }
        }
    }
    SDL_PutAudioStreamData(stream, g_pvBuf.data(), frames * static_cast<int>(sizeof(float) * 2));
}

bool OpenPreviewStream(std::string& error) {
    if (g_pvStream)
        return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = "오디오 장치를 열 수 없습니다.";
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kLiveRate;
    g_pvStream = SDL_OpenAudioDeviceStream(FindSdlDevice(OPTION(kSoundAudioDevice).Get()), &spec, &OnPreviewAudio, nullptr);
    if (!g_pvStream) {
        error = "오디오 장치를 열 수 없습니다.";
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_pvStream);
    return true;
}
#else
bool OpenPreviewStream(std::string& error) {
    error = "이 빌드에서는 재생할 수 없습니다 (SDL3 없음).";
    return false;
}
#endif

}  // namespace

bool PreviewPlay(const std::string& key, std::string& error) {
    if (!OpenPreviewStream(error))
        return false;
    std::unique_ptr<Rom> rom;
    std::unique_ptr<Mp2kEngine> eng;
    std::unique_ptr<QGBA::GscPlayer> gsc;
    bool sfx = false;
    double length = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_gb && g_gbDriver) {
            const QGBA::GscTables& tables = g_gbDriver->profile().tables;
            const bool song = key.rfind("gbsong:", 0) == 0;
            const bool cry = key.rfind("cry:", 0) == 0;
            sfx = key.rfind("gbsfx:", 0) == 0 || cry;
            if (!song && !sfx) {
                error = "알 수 없는 이름표입니다.";
                return false;
            }
            gsc.reset(new QGBA::GscPlayer(g_gbDriver->rom(), tables, false));
            if (cry) {
                int id = -1, pitch = 0, len = 0;
                if (!tables.cries || !g_gbDriver->lastCry(id, pitch, len)) {
                    error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
                    return false;
                }
                gsc->requestCry(static_cast<uint16_t>(id), static_cast<uint16_t>(pitch), static_cast<uint16_t>(len), nullptr);
            } else if (song) {
                gsc->requestSong(static_cast<uint16_t>(std::atoi(key.c_str() + 7)));
            } else {
                gsc->requestSfx(static_cast<uint16_t>(std::atoi(key.c_str() + 6)));
            }
        } else if (g_gba && !g_romImage.data.empty()) {
            // 재생 중에 게임을 바꿔도 안전하게 롬을 복사해 쓴다
            rom.reset(new Rom());
            rom->data = g_romImage.data;
            eng.reset(new Mp2kEngine(rom.get()));
            EngineConfig cfg;
            cfg.outRate = kLiveRate;
            eng->configure(cfg);
            const SoundInfoSnapshot si = g_live.lastSoundInfo();
            if (si.valid)
                eng->setRuntimeParams(si.maxChans, si.masterVolume, si.reverb, si.samplesPerVBlank, si.dmaPeriod);
            if (key.rfind("song:", 0) == 0) {
                const uint32_t header = static_cast<uint32_t>(std::strtoul(key.c_str() + 5, nullptr, 16));
                if (!rom->valid(header, 8) || rom->u8(header) == 0) {
                    error = "이 게임의 롬에서 이 곡을 찾지 못했습니다.";
                    return false;
                }
                eng->start(header);
                length = isLoopingSong(*rom, header) ? 0 : songSeconds(*rom, header);
            } else if (key.rfind("cry:", 0) == 0) {
                uint32_t header = 0;
                std::shared_ptr<const RamImage> ram;
                if (!g_live.lastRamSong(header, ram)) {
                    error = "\"RAM 곡 읽기\" 를 켜고 울음소리를 1배속으로 정한 뒤 게임에서 울음소리가 한 번 나와야 재생할 수 있습니다.";
                    return false;
                }
                eng->startWithRam(header, std::move(ram));
            } else {
                error = "알 수 없는 이름표입니다.";
                return false;
            }
        } else {
            error = "게임이 실행 중일 때 재생할 수 있습니다.";
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(g_pvMtx);
    g_pvEngine.reset();
    g_pvGsc.reset();
    g_pvRom = std::move(rom);
    g_pvEngine = std::move(eng);
    g_pvGsc = std::move(gsc);
    g_pvSfx = sfx;
    g_pvFrames = 0;
    g_pvPlaying = true;
    g_pvPaused = false;
    g_pvLength = length;
    return true;
}

void PreviewStop() {
    std::lock_guard<std::mutex> lock(g_pvMtx);
    g_pvEngine.reset();
    g_pvGsc.reset();
    g_pvRom.reset();
    g_pvPlaying = false;
    g_pvPaused = false;
}

void PreviewSetPaused(bool paused) {
    std::lock_guard<std::mutex> lock(g_pvMtx);
    if (g_pvPlaying)
        g_pvPaused = paused;
}

bool PreviewPlaying() {
    std::lock_guard<std::mutex> lock(g_pvMtx);
    return g_pvPlaying;
}

bool PreviewPaused() {
    std::lock_guard<std::mutex> lock(g_pvMtx);
    return g_pvPlaying && g_pvPaused;
}

double PreviewLength() {
    std::lock_guard<std::mutex> lock(g_pvMtx);
    return g_pvLength;
}

}  // namespace ffbgm
