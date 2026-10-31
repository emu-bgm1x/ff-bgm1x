// ff-bgm1x: 효과음 음성 미러. 배속 중에 게임 DSP 음성의 실제 출력을 녹음해 두고 실제 시간 기준(1배속)으로 다시 낸다.
//
// DSP 음성(Source)은 오디오 프레임(160 샘플)마다 해독·음높이·필터까지 적용한 출력을 만든다. 음성 하나가 소리 하나를
// 시작하면(켜짐, 리셋, 새 임베디드 버퍼) 그때부터의 출력에 게임이 준 볼륨(믹스별 gain × 믹서 볼륨)을 곱해 게임 시간
// 그대로 쌓고, 원본 믹스에서는 뺀다. 쌓인 소리는 출력 쪽에서 원래 샘플레이트로 재생하므로 음높이는 그대로이고,
// 볼륨·페이드·멈춤은 게임이 낸 그대로다. 사운드 라이브러리(시퀀스, 뱅크)는 해석하지 않는다.
//
// 배속 중에는 게임이 소리를 1배속보다 빨리 만들어 쌓이므로, 아직 게임이 내고 있는 소리가 일정 시간 넘게 밀리면
// 앞부분을 건너뛰어 따라잡는다 (짧은 효과음은 이 한도 안이라 끝까지 1배속으로 난다).
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "audio_core/audio_types.h"
#include "common/common_types.h"

namespace AudioCore::HLE {

class SfxMirror {
public:
    static constexpr std::size_t kSources = 24;

    // ---- 에뮬레이션(DSP) 스레드 ----
    // 음성 source 가 새 소리를 시작했다 (앞의 소리는 쌓인 만큼 끝까지 낸다)
    void Begin(std::size_t source);
    // 음성 source 의 소리가 끝났다 (게임이 음성을 껐다)
    void End(std::size_t source);
    bool Active(std::size_t source) const;
    // 이번 프레임 출력. gain: 믹스 3개 × 4채널(앞 L,R, 뒤 L,R), mixer_volume: 믹스별 볼륨.
    // 이 음성이 녹음 중이 아니면(Clear 등) false
    bool Feed(std::size_t source, const StereoFrame16& frame, const std::array<std::array<float, 4>, 3>& gain,
              const std::array<float, 3>& mixer_volume);

    // ---- 오디오 출력 스레드 ----
    // 쌓인 소리를 1배속으로 buffer 에 더한다
    void Mix(s16* buffer, std::size_t frames, u32 sample_rate, float volume);

    // 어느 스레드에서든
    void Clear();  // 모든 소리를 바로 멈춘다

private:
    struct Event {
        std::vector<float> data;  // 원래 샘플레이트(32728Hz)의 스테레오 (L, R 번갈아)
        double pos = 0;           // 재생 위치 (샘플 단위, data 앞에서 지운 만큼 빼 둔다)
        bool open = true;         // 게임이 아직 이 소리를 내고 있다
    };

    void EndLocked(std::size_t source);

    mutable std::mutex mutex;
    std::array<std::shared_ptr<Event>, kSources> current{};  // 음성마다 지금 쌓고 있는 소리 (mutex)
    std::vector<std::shared_ptr<Event>> events;  // 재생할 소리 (mutex)
};

} // namespace AudioCore::HLE
