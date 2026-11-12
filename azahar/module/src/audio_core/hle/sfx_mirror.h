// ff-bgm1x: 효과음 음성 미러. 배속·느린 속도에서 게임 DSP 음성의 소리를 실제 시간 기준(1배속)으로 다시 낸다.
//
// 음성 하나가 소리 하나를 시작하면(켜짐, 리셋, 새 임베디드 버퍼) 그때의 속도에 따라 두 방식 중 하나로 낸다.
// 어느 쪽이든 원본 믹스에서는 그 소리를 뺀다. 사운드 라이브러리(시퀀스, 뱅크)는 해석하지 않는다.
//
// - 녹음 (배속 중): 음성이 오디오 프레임(160 샘플)마다 만드는 실제 출력(해독·음높이·필터 적용 후)에
//   게임이 준 볼륨(믹스별 gain × 믹서 볼륨)을 곱해 게임 시간 그대로 쌓고, 출력 쪽에서 원래 샘플레이트로 재생한다.
//   게임이 소리를 1배속보다 빨리 만들어 쌓이므로, 아직 게임이 내고 있는 소리가 일정 시간 넘게 밀리면
//   앞부분을 건너뛰어 따라잡는다 (짧은 효과음은 이 한도 안이라 끝까지 1배속으로 난다).
// - 앞당겨 해독 (느린 속도): 게임이 소리를 실제 시간보다 느리게 만들어 녹음으로는 끊기므로, 음성이 버퍼를
//   받는 순간 해독된 버퍼 전체를 받아 두고 음성의 재생 배율(음높이)대로 실제 시간에 낸다. 볼륨·음높이는
//   게임이 바꾼 때부터 따른다. 반복 버퍼는 게임이 음성을 끌 때까지 반복한다.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "audio_core/audio_types.h"
#include "audio_core/interpolate.h"
#include "common/common_types.h"

namespace AudioCore::HLE {

class SfxMirror {
public:
    static constexpr std::size_t kSources = 24;
    enum class Mode { Record, Ahead };

    // ---- 에뮬레이션(DSP) 스레드 ----
    // 음성 source 가 새 소리를 시작했다 (앞의 소리는 쌓인 만큼 끝까지 낸다)
    void Begin(std::size_t source, Mode mode);
    // 음성 source 의 소리가 끝났다 (게임이 음성을 껐다)
    void End(std::size_t source);
    bool Active(std::size_t source) const;
    // 녹음: 이번 프레임 출력. gain: 믹스 3개 × 4채널(앞 L,R, 뒤 L,R), mixer_volume: 믹스별 볼륨.
    // 이 음성이 녹음 중이 아니면(Clear 등) false
    bool Feed(std::size_t source, const StereoFrame16& frame, const std::array<std::array<float, 4>, 3>& gain,
              const std::array<float, 3>& mixer_volume);
    // 앞당겨 해독: 음성이 받은 버퍼의 해독 결과 (loop: 반복 버퍼)
    void AheadBuffer(std::size_t source, const AudioInterp::StereoBuffer16& pcm, bool loop);
    // 앞당겨 해독: 지금 게임이 준 볼륨과 재생 배율. 이 음성이 미러 중이 아니면 false
    bool AheadUpdate(std::size_t source, const std::array<std::array<float, 4>, 3>& gain,
                     const std::array<float, 3>& mixer_volume, float rate);

    // ---- 오디오 출력 스레드 ----
    // 쌓인 소리를 1배속으로 buffer 에 더한다
    void Mix(s16* buffer, std::size_t frames, u32 sample_rate, float volume);

    // 어느 스레드에서든
    void Clear();  // 모든 소리를 바로 멈춘다

private:
    struct Event {
        Mode mode = Mode::Record;
        std::vector<float> data;  // 스테레오 (L, R 번갈아). 녹음: 볼륨 적용, 원래 샘플레이트. 앞당김: 해독 그대로
        double pos = 0;           // 재생 위치 (샘플 단위, data 앞에서 지운 만큼 빼 둔다)
        bool open = true;         // 게임이 아직 이 소리를 내고 있다
        // 앞당겨 해독
        float gl = 0, gr = 0;  // 지금 볼륨
        float rate = 1.0f;     // 재생 배율 (원래 샘플레이트 대비)
        bool loop = false;     // 마지막 버퍼가 반복 버퍼
        std::size_t loop_start = 0;  // 그 버퍼의 시작 (샘플)
    };

    void EndLocked(std::size_t source);

    mutable std::mutex mutex;
    std::array<std::shared_ptr<Event>, kSources> current{};  // 음성마다 지금 쌓고 있는 소리 (mutex)
    std::vector<std::shared_ptr<Event>> events;  // 재생할 소리 (mutex)
};

} // namespace AudioCore::HLE
