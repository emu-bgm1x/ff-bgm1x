// ff-bgm1x: libretro 코어용 연결 (설명은 ff_bgm1x.h).
// PC 판 vbam_bgm.cpp 와 같은 순서로 재생기를 돌리되, 1배속 소리는 SDL 스트림 대신 프론트엔드가 부르는
// ff_bgm1x_mix 로 낸다. 이 함수를 부르지 않는 프론트엔드에서는 원본 BGM 을 무음 처리하지 않는다.
#include "libretro/ff_bgm1x.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "core/gba/gbaGlobals.h"
#include "core/gba/gbaSound.h"
#include "wx/ffbgm/bgm_mirror.h"
#include "wx/ffbgm/rom.h"

namespace {

// 재생기 출력 1.0 = GBA DirectSound 표본 128 (PC 판 vbam_bgm.cpp 와 같은 환산)
constexpr float kEngineToInt16 = 128.0f * 0.66f / 256.0f * 32768.0f;

std::mutex g_mtx;  // 재생기 상태 변경(에뮬레이션 스레드) vs 출력(오디오 스레드)
Rom g_romImage;
std::vector<MPlayTableCandidate> g_cands;
BgmMultiMirror g_live(&g_romImage);
size_t g_romSize = 0;
bool g_gba = false;
uint64_t g_frame = 0;
bool g_wasEnabled = true;
int g_psgMask = 0;
int g_rate = 0;  // 지금 재생기를 맞춘 출력 표본율 (0 = 아직 출력 요청 없음)
std::vector<float> g_buf;

std::atomic<bool> g_active{false};          // MP2K 표 후보가 있다
std::atomic<bool> g_frontendMixes{false};   // 프론트엔드가 ff_bgm1x_mix 를 부른다

struct Options {
    bool enabled = true;
    bool ffCutscenes = false;
    bool sfx1x = true;
    int sfxVolume = 0x100;
} g_opts;

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

int16_t Clamp16(float v) {
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return static_cast<int16_t>(v);
}

// 대체 중인 소리가 쓰는 PSG 채널은 에뮬레이터 출력에서 끈다 (게임 메모리·진행은 그대로)
void ApplyPsgMask(int mask) {
    if (mask == g_psgMask)
        return;
    g_psgMask = mask;
    soundSetFfbgmMuteMask(mask);
}

// ffBgm1xFrame 의 본체 (g_mtx 를 잡은 채로). 돌려주는 값은 출력에서 끌 PSG 채널
int UpdateLocked() {
    const bool enabled = g_opts.enabled && g_frontendMixes && g_rate > 0;
    VbamBus bus;
    g_live.setSfx1x(g_opts.sfx1x);
    g_live.setSfxVolume(std::min(std::max(g_opts.sfxVolume, 0), 256) / 256.0f);
    if (!enabled || g_cands.empty()) {
        // 끌 때: 대체 재생을 멈추고, 무음 처리하던 게임 곡을 처음부터 다시 시작시킨다
        if (g_wasEnabled && g_live.located())
            g_live.releaseMute(bus);
        g_wasEnabled = enabled;
        return 0;
    }
    g_wasEnabled = true;
    ++g_frame;
    if (!g_live.located() || !g_live.stillValid(bus)) {
        if (g_live.located())
            g_live.reset();  // 게임이 MP2K 를 내려놓음 (모드 전환 등)
        if (g_frame % 30 != 0 || !g_live.locate(bus, g_cands))
            return 0;
    }
    if (g_frame % 60 == 1) {
        const SoundInfoSnapshot si = readSoundInfo(bus);
        if (si.valid)
            g_live.setRuntimeParams(si);
    }
    g_live.onFrame(bus, true);
    return g_live.replacedPsgMask();
}

}  // namespace

void ffBgm1xGbaLoaded(const uint8_t* rom, size_t size) {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_live.reset();
        g_romImage.loadFromMemory(rom, size);
        g_cands = findMPlayTables(g_romImage);
        g_romSize = size;
        g_gba = true;
        g_frame = 0;
        g_wasEnabled = true;
        if (g_rate > 0) {
            EngineConfig cfg;
            cfg.outRate = g_rate;
            g_live.configure(cfg);
        }
        g_active = !g_cands.empty();
    }
    ApplyPsgMask(0);
}

void ffBgm1xUnload() {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_live.reset();
        g_gba = false;
        g_active = false;
        g_cands.clear();
        g_romSize = 0;
    }
    ApplyPsgMask(0);
}

void ffBgm1xFrame() {
    if (!g_gba)
        return;
    int psgMask = 0;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        psgMask = UpdateLocked();
    }
    ApplyPsgMask(psgMask);
}

void ffBgm1xSetOptions(bool enabled, bool ffCutscenes, bool sfx1x, int sfxVolume) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_opts.enabled = enabled;
    g_opts.ffCutscenes = ffCutscenes;
    g_opts.sfx1x = sfx1x;
    g_opts.sfxVolume = sfxVolume;
}

extern "C" {

RETRO_API void ff_bgm1x_mix(int16_t* stereo, int frames, int sampleRate) {
    g_frontendMixes = true;
    if (frames <= 0 || sampleRate <= 0)
        return;
    std::lock_guard<std::mutex> lock(g_mtx);
    if (sampleRate != g_rate) {
        // 출력 표본율에 재생기를 맞춘다 (재생 중인 곡은 처음부터)
        g_rate = sampleRate;
        EngineConfig cfg;
        cfg.outRate = sampleRate;
        g_live.configure(cfg);
    }
    if (!g_gba || !g_active || !g_opts.enabled || !g_live.located())
        return;
    if (g_buf.size() < static_cast<size_t>(frames) * 2)
        g_buf.resize(static_cast<size_t>(frames) * 2);
    g_live.render(g_buf.data(), frames);
    const float scale = kEngineToInt16 * soundGetVolume();
    for (int i = 0; i < frames * 2; ++i)
        stereo[i] = Clamp16(stereo[i] + g_buf[i] * scale);
}

RETRO_API bool ff_bgm1x_speed_hold(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_gba && g_active && g_frontendMixes && g_opts.enabled && !g_opts.ffCutscenes && g_live.cutsceneActive();
}

}
