// ff-bgm1x: 효과음 음성 미러. 설명은 sfx_mirror.h
#include "audio_core/hle/sfx_mirror.h"

#include <algorithm>

namespace AudioCore::HLE {

namespace {
// 녹음: 아직 게임이 내고 있는 소리가 이만큼 넘게 밀리면 따라잡는다 (원래 샘플레이트 기준 2.5초)
constexpr double kMaxLag = native_sample_rate * 2.5;
// 재생이 이만큼 지나면 앞부분 데이터를 지운다
constexpr std::size_t kTrimSamples = native_sample_rate * 2;

void StereoGain(const std::array<std::array<float, 4>, 3>& gain, const std::array<float, 3>& mixer_volume,
                float& gl, float& gr) {
    // 믹서와 같은 방식으로 스테레오로 접는다: 왼쪽 = 앞 L + 뒤 L, 오른쪽 = 앞 R + 뒤 R (믹스별 볼륨을 곱해 합침)
    gl = 0;
    gr = 0;
    for (int m = 0; m < 3; ++m) {
        gl += mixer_volume[m] * (gain[m][0] + gain[m][2]);
        gr += mixer_volume[m] * (gain[m][1] + gain[m][3]);
    }
}
} // namespace

void SfxMirror::Begin(std::size_t source, Mode mode) {
    if (source >= kSources) return;
    auto e = std::make_shared<Event>();
    e->mode = mode;
    e->data.reserve(samples_per_frame * 2 * 64);
    std::lock_guard lock(mutex);
    EndLocked(source);
    current[source] = e;
    events.push_back(std::move(e));
}

void SfxMirror::End(std::size_t source) {
    if (source >= kSources) return;
    std::lock_guard lock(mutex);
    EndLocked(source);
}

void SfxMirror::EndLocked(std::size_t source) {
    if (!current[source]) return;
    current[source]->open = false;
    current[source] = nullptr;
}

bool SfxMirror::Active(std::size_t source) const {
    if (source >= kSources) return false;
    std::lock_guard lock(mutex);
    return current[source] != nullptr;
}

bool SfxMirror::Feed(std::size_t source, const StereoFrame16& frame,
                     const std::array<std::array<float, 4>, 3>& gain, const std::array<float, 3>& mixer_volume) {
    if (source >= kSources) return false;
    float gl, gr;
    StereoGain(gain, mixer_volume, gl, gr);
    std::lock_guard lock(mutex);
    if (!current[source]) return false;
    auto& data = current[source]->data;
    for (const auto& s : frame) {
        data.push_back(s[0] * gl);
        data.push_back(s[1] * gr);
    }
    return true;
}

void SfxMirror::AheadBuffer(std::size_t source, const AudioInterp::StereoBuffer16& pcm, bool loop) {
    if (source >= kSources) return;
    std::lock_guard lock(mutex);
    Event* e = current[source].get();
    if (!e) return;
    e->loop = loop;
    e->loop_start = e->data.size() / 2;
    for (const auto& s : pcm) {
        e->data.push_back(s[0]);
        e->data.push_back(s[1]);
    }
}

bool SfxMirror::AheadUpdate(std::size_t source, const std::array<std::array<float, 4>, 3>& gain,
                            const std::array<float, 3>& mixer_volume, float rate) {
    if (source >= kSources) return false;
    float gl, gr;
    StereoGain(gain, mixer_volume, gl, gr);
    std::lock_guard lock(mutex);
    Event* e = current[source].get();
    if (!e) return false;
    e->gl = gl;
    e->gr = gr;
    e->rate = rate > 0 ? rate : 1.0f;
    return true;
}

void SfxMirror::Mix(s16* buffer, std::size_t frames, u32 sample_rate, float volume) {
    if (sample_rate == 0) return;
    const double base_step = static_cast<double>(native_sample_rate) / sample_rate;
    std::lock_guard lock(mutex);
    for (auto it = events.begin(); it != events.end();) {
        Event& e = **it;
        const bool ahead = e.mode == Mode::Ahead;
        const double step = ahead ? base_step * e.rate : base_step;
        const float vl = ahead ? e.gl * volume : volume;
        const float vr = ahead ? e.gr * volume : volume;
        // 녹음: 게임이 아직 내는 소리가 많이 밀렸으면 따라잡는다
        if (!ahead && e.open) {
            const double n = static_cast<double>(e.data.size() / 2);
            if (n - e.pos > kMaxLag) e.pos = n - kMaxLag / 2;
        }
        bool done = false;
        for (std::size_t i = 0; i < frames; ++i) {
            std::size_t idx = static_cast<std::size_t>(e.pos);
            if ((idx + 1) * 2 + 1 >= e.data.size()) {
                if (ahead && e.loop && e.open && e.data.size() / 2 > e.loop_start + 1) {
                    // 반복 버퍼: 게임이 음성을 끌 때까지 그 버퍼를 되풀이한다
                    e.pos = static_cast<double>(e.loop_start) + (e.pos - idx);
                    idx = e.loop_start;
                } else {
                    // 쌓인 만큼 다 냈다: 게임이 아직 내는 중이면 다음 데이터를 기다리고, 끝났으면 지운다
                    done = !e.open;
                    break;
                }
            }
            const float frac = static_cast<float>(e.pos - idx);
            const float l = e.data[idx * 2] + (e.data[idx * 2 + 2] - e.data[idx * 2]) * frac;
            const float r = e.data[idx * 2 + 1] + (e.data[idx * 2 + 3] - e.data[idx * 2 + 1]) * frac;
            buffer[i * 2] = static_cast<s16>(std::clamp(buffer[i * 2] + l * vl, -32768.0f, 32767.0f));
            buffer[i * 2 + 1] = static_cast<s16>(std::clamp(buffer[i * 2 + 1] + r * vr, -32768.0f, 32767.0f));
            e.pos += step;
        }
        if (done || (ahead && e.loop && !e.open)) {
            // 끝났거나, 게임이 끈 반복 소리
            it = events.erase(it);
            continue;
        }
        if (e.pos > kTrimSamples && !(ahead && e.loop)) {
            const std::size_t cut = static_cast<std::size_t>(e.pos) - 1;
            e.data.erase(e.data.begin(), e.data.begin() + static_cast<std::ptrdiff_t>(cut * 2));
            e.pos -= static_cast<double>(cut);
            if (e.loop_start >= cut) e.loop_start -= cut;
        }
        ++it;
    }
}

void SfxMirror::Clear() {
    std::lock_guard lock(mutex);
    for (auto& e : events) e->open = false;
    events.clear();
    for (auto& c : current) c = nullptr;
}

} // namespace AudioCore::HLE
