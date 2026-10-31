// ff-bgm1x: 배속과 관계없이 스트리밍 BGM(BCSTM, DSP-ADPCM)을 1배속으로 재생한다.
//
// 1) 게임 RomFS 의 .bcstm 파일마다 채널별 ADPCM 계수와 블록 첫 8바이트를 색인한다.
// 2) DSP 음성이 ADPCM 버퍼를 받으면 계수로 파일·채널을, 데이터로 블록 위치를 알아낸다.
// 3) 그 음성은 원본 믹스에서 빼고, 같은 파일을 직접 해독해 실제 시간 기준으로 재생한다.
//    볼륨은 원본 음성의 gain 과 믹서 볼륨을 그대로 따른다.
//
// AAC(.aac, ADTS) 를 쓰는 게임(포켓몬 X·Y 등):
// 1) RomFS 의 .aac 파일을 프레임 단위로 나눠 프레임 내용 해시로 색인한다.
// 2) DSP AAC 디코더가 프레임을 풀 때 어느 파일의 몇 번째 프레임인지, 어느 주소로 푸는지 기록한다.
// 3) 그 주소의 PCM16 버퍼를 받은 음성을 BGM 으로 보고 1)~3) 과 같이 대체 재생한다.
//    AAC 파일에는 반복 정보가 없으므로, 게임이 앞 프레임으로 되돌아가는 순간을 반복 지점으로 배운다.
#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_core/hle/ctr_synth.h"
#include "audio_core/hle/seq_tracker.h"
#include "common/common_types.h"

namespace FileSys {
class RomFSReader;
}

namespace AudioCore::HLE {

class BgmReplacer {
public:
    static BgmReplacer& Instance();

    // 메뉴에서 켜고 끈다. 끄면 원본 에뮬레이터와 똑같이 동작한다.
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    // 사용자가 일시정지했을 때만 멈춘다. 성능 때문에 에뮬레이션이 멈칫해도 BGM 은 이어진다.
    void SetEmulationRunning(bool running);
    // 연출(반복 없는 긴 곡이 나오는 장면) 중에도 배속할지. 끄면(기본) 연출 동안 배속을 잠시 멈춘다
    void SetFastForwardCutscenes(bool on);
    // "대사·효과음 1배속": BGM 위에 겹쳐 트는 소리(대사 등)를 1배속으로 낼지. 끄면 게임 속도로 둔다
    void SetVoices1x(bool on);
    // 1배속으로 내는 효과음(재생기·파형 대체)의 볼륨 (0~1)
    void SetSfxVolume(float volume);
    // 동영상 연출 감지 (어느 스레드에서든 불릴 수 있다)
    // 게임이 RomFS 의 동영상 파일(.moflex)을 읽을 때
    void OnRomFsRead(FileSys::RomFSReader& reader, u64 offset, u64 length);
    // 게임이 동영상 화면 변환(Y2R)을 한 번 할 때
    void OnVideoFrame();

    // ---- 에뮬레이션(DSP) 스레드 ----
    void Reset();
    // ADPCM 버퍼가 음성에 들어올 때. BGM 스트림이면 true (그 음성은 원본 믹스에서 뺀다)
    bool OnAdpcmBuffer(std::size_t source_id, const std::array<s16, 16>& coeffs, const u8* data,
                       u32 samples);
    // 음성이 꺼졌거나 BGM 이 아닌 버퍼를 받았을 때
    void OnSourceIdle(std::size_t source_id);
    // DSP 가 오디오 프레임 하나(160 샘플)를 만들 때마다. 배속과 무관한 게임 시간의 기준이다
    void OnDspFrame();
    // AAC 디코더가 프레임 하나를 dst0(왼쪽)/dst1(오른쪽) 로 풀기 직전
    void OnAacDecode(const u8* data, u32 size, u32 dst0, u32 dst1);
    // PCM16 버퍼가 음성에 들어올 때. BGM 을 푼 버퍼면 true
    // (게임은 반복 지점을 샘플 단위로 맞추려고 프레임 중간부터, 또는 일부만 재생하기도 한다)
    bool OnPcmBuffer(std::size_t source_id, u32 address, u32 length, u32 channels);
    void UpdateSourceGain(std::size_t source_id, const std::array<std::array<float, 4>, 3>& gain);
    void UpdateMixerVolumes(const std::array<float, 3>& volumes);

    // ---- 오디오 출력 스레드 ----
    void Mix(s16* buffer, std::size_t frames, u32 sample_rate);

private:
    struct StreamFile {
        std::string name;
        u64 offset = 0;  // RomFS level3 기준
        u64 size = 0;
        u32 sample_rate = 0;
        bool loop = false;
        u32 loop_start = 0;
        u32 loop_end = 0;  // 이 위치에 닿으면 loop_start 로 (BCSTM 은 곡 끝)
        u32 frames = 0;
        bool aac = false;
        bool skip_logged = false;  // "게임 속도 그대로 둠" 로그를 남겼음
        bool chained = false;      // 끝나자마자 게임이 다음 스트림을 이어 트는 조각 곡 (실행 중에 알아냄)
        // 게임이 BGM 을 멈추지 않고 그 위에 겹쳐 트는 반복 없는 소리 (대사·효과음). 대체하지 않는다.
        // 이름의 접두어 단위로 배운다 (예: 레이튼의 EVT_)
        bool overlay = false;
        // 겹쳐 트는 소리 중 1배속으로 바꾸지 않고 게임 속도로 둘 것 (설정 파일 "native <접두어>")
        bool native = false;
        bool loop_known = false;   // 게임의 사운드 아카이브(.bcsar)에서 반복 정보를 읽었음
        bool game_reached_end = false;  // 이번 재생에서 게임이 곡 끝(마지막 프레임/블록)까지 보냄
        std::vector<u32> aac_frame_offsets;  // 파일 안 ADTS 프레임 위치 (마지막은 파일 크기)
        u32 channels = 0;
        u32 blocks = 0;
        u32 block_size = 0;
        u32 block_samples = 0;
        u32 last_block_samples = 0;
        u32 last_block_padded = 0;
        u32 data_offset = 0;  // 파일 안 DATA 섹션 위치
        std::vector<std::array<s16, 16>> coeffs;
        std::vector<std::array<s16, 2>> history;  // 채널별 초기 yn1, yn2
        std::vector<std::unordered_map<u64, u32>> block_heads;  // 채널별 블록 첫 8바이트 -> 블록 번호
        bool heads_loaded = false;
    };

    struct Binding {
        int file = -1;
        u32 channel = 0;
        std::array<std::array<float, 4>, 3> gain{};
        s64 aac_end = -1;  // AAC: 이 음성이 마지막으로 받은 버퍼의 끝 (곡 안 샘플 위치)
    };

    struct Playback {
        int file = -1;
        std::vector<std::vector<s16>> pcm;  // 채널별 전체 PCM
        double position = 0;
        bool finished = false;
        // 팡파레처럼 반복 없이 끝나는 곡: 게임이 먼저 끝내도 1배속으로 끝까지 재생한다
        bool hold_to_end = false;
        bool waiting = false;   // 팡파레가 끝나길 기다리는 중
        u64 start_frame = 0;    // 대체 재생을 시작한 DSP 프레임
        double game_pos = -1;   // 게임이 마지막으로 재생하기 시작한 버퍼 위치
        std::vector<float> last_gl, last_gr;  // 마지막 채널별 좌우 볼륨
    };

    struct AacOutput {
        int file = -1;
        u32 frame = 0;
        u32 channel = 0;
        bool restart = false;  // 게임이 이 프레임부터 곡을 새로 시작함
    };
    struct PendingLoop {
        int file = -1;
        u32 start = 0;  // 프레임 번호
        u32 end = 0;
    };

    // ---- 효과음 1배속 ----
    // 사운드 아카이브에서 이름에 System 이 들어간 플레이어로 나오는 시퀀스 효과음의 악기 파형을 색인한다.
    // 게임이 그 파형을 DSP 음성에 넣으면 원본은 빼고, 같은 파형을 1배속으로 끝까지 낸다
    struct SfxWave {
        std::string name;  // 그 파형을 쓰는 효과음 이름 (기록용)
        bool system = false;  // 시스템 효과음 플레이어의 파형 (트랙 칸을 찾기 전에도 파형 단위로 바꿔 낸다)
        u32 sample_rate = 0;
        u32 frames = 0;
        std::vector<u64> data_offset;  // 채널별 표본 시작 (RomFS level3 기준)
        std::vector<std::array<s16, 16>> coeffs;
        std::vector<std::array<s16, 2>> history;
    };
    struct SfxPlay {
        int wave = -1;
        u32 channel = 0;
        int source = -1;  // 원본 음성 (게임이 그 음성을 놓으면 -1, 마지막 볼륨 유지)
        std::shared_ptr<const std::vector<s16>> pcm;
        double position = 0;
        float gl = 0, gr = 0;
    };
    void IndexSystemSfx(FileSys::RomFSReader& romfs, u64 offset, u64 size);
    std::shared_ptr<const std::vector<s16>> SfxPcm(int wave, u32 channel);
    void StartSfx(std::size_t source_id, int wave, u32 channel);
    static u64 SfxKey(u64 coeff_key, u64 head) {
        return coeff_key * 0x9E3779B97F4A7C15ULL ^ head;
    }

    bool BuildIndex();
    void IndexAacFile(FileSys::RomFSReader& romfs, StreamFile& f);
    // .bcsar 의 스트림 사운드 정보에서 외부 스트림 파일별 반복 지점을 읽는다
    void ReadArchiveLoops(FileSys::RomFSReader& romfs, u64 offset, u64 size);
    void LoadBlockHeads(StreamFile& f);
    bool StartPlayback(int file, u32 block);
    bool DecodeAac(const StreamFile& f, const std::vector<u8>& raw, Playback& p);
    // 설정 파일 <타이틀ID>.txt: 실행 파일 옆 ff-bgm1x 폴더(읽기 전용 기본값, 배포용)와
    // 사용자 설정 폴더의 ff-bgm1x 폴더(실행 중 배운 내용을 저장) 두 곳에서 읽는다
    void LoadSongSettings();
    void LoadSongSettingsFile(const std::string& path);
    void SaveChainedSong(const std::string& name);
    static u64 FrameHash(const u8* data, u32 size);
    // 반복 없는 짧은 곡(팡파레): 게임이 먼저 끝내도 1배속으로 끝까지 재생한다
    static bool IsFanfare(const StreamFile& f);
    static bool IsEvolutionSong(const StreamFile& f);
    static std::string NamePrefix(const std::string& name);
    // 접두어가 같은 반복 없는 곡을 모두 겹쳐 트는 소리로 표시한다. 표시한 곡 수를 돌려준다
    std::size_t MarkOverlayPrefix(const std::string& prefix);
    void SaveSongSetting(const std::string& kind, const std::string& name);
    void StopUnboundPlaybacks();
    static u64 CoeffKey(const std::array<s16, 16>& coeffs);

    std::atomic<bool> enabled{true};
    std::atomic<bool> emulation_running{true};
    std::atomic<bool> ff_cutscenes{false};
    std::atomic<bool> voices_1x{true};
    std::atomic<float> sfx_volume{1.0f};
    // 시퀀스 효과음 1배속: 게임 메모리에서 효과음 시퀀스 시작을 감지해 롬의 같은 시퀀스를 자체 재생기로 1배속 연주하고,
    // 원본 효과음 음성은 끈다. 트랙 칸을 찾기 전에는 시스템 효과음만 파형 단위로 바꿔 낸다
    std::vector<std::unique_ptr<CtrSynth>> seq_synths;  // 사운드 아카이브마다 (mutex)
    SeqTracker seq_tracker;                             // 에뮬레이션 스레드
    std::vector<SeqTracker::Start> seq_starts;
    std::atomic<u32> seq_rate{0};  // 출력 샘플레이트 (Mix 가 알려 줌)
    void StartSeqSfx(const SeqTracker::Start& start);
    void StopSeqSfx();
    std::mutex mutex;
    bool index_built = false;
    bool index_ok = false;
    std::vector<StreamFile> files;
    std::unordered_multimap<u64, std::pair<int, u32>> coeff_index;  // 계수 -> (파일, 채널)
    std::array<Binding, 24> bindings{};
    std::vector<SfxWave> sfx_waves;
    std::unordered_map<u64, std::pair<int, u32>> sfx_index;  // (계수, 첫 8바이트) -> (파형, 채널)
    std::unordered_map<u64, std::shared_ptr<const std::vector<s16>>> sfx_cache;
    std::vector<SfxPlay> sfx_plays;
    std::array<std::pair<int, u32>, 24> sfx_source{};  // 음성 -> 지금 대체 중인 (파형, 채널), 없으면 파형 -1
    std::unordered_map<int, Playback> playbacks;
    std::array<float, 3> mixer_volume{1.0f, 0.0f, 0.0f};

    // 이어 붙이는 곡 감지 (에뮬레이션 스레드 전용)
    std::atomic<u64> dsp_frames{0};
    int natural_end_file = -1;     // 마지막 블록까지 재생이 시작된 반복 없는 곡
    double natural_end_time = 0;   // 그 곡이 끝날 게임 시각 (초)
    u64 natural_end_set_frame = 0;
    // 방금 대체 재생을 멈춘 곡 (곧바로 같은 박자 위치에서 이어지는 다른 곡을 1배속 위치에 맞추려고)
    struct LastStop {
        int file = -1;
        double our_sec = 0;    // 멈출 때 1배속 재생 위치 (초)
        double game_sec = -1;  // 게임이 마지막으로 재생하기 시작한 버퍼 위치 (초)
        u64 frame = 0;         // 멈춘 DSP 프레임
    } last_stop;
    // 동영상 연출 감지
    struct VideoRange {
        u64 start = 0;
        u64 end = 0;
        std::string name;
    };
    void BuildVideoRanges(FileSys::RomFSReader& reader);
    void MarkVideo(u64 hold_frames);
    std::mutex video_mutex;
    std::set<std::string> overlay_prefixes;  // 이번 실행에서 배운 '겹쳐 트는 소리' 접두어
    std::vector<VideoRange> video_ranges;
    bool video_ranges_built = false;
    std::atomic<bool> video_scanning{false};
    std::atomic<u64> video_until_frame{0};     // 이 DSP 프레임까지 동영상 재생 중으로 본다
    std::atomic<bool> video_file_seen{false};  // 동영상 파일을 읽은 뒤에만 Y2R 을 동영상으로 본다
    double cutscene_until = 0;       // 이 게임 시각(초)까지 연출 음악이 재생 중
    std::string settings_user_path;  // 배운 내용을 덧붙일 파일

    // AAC (에뮬레이션 스레드 전용)
    std::unordered_multimap<u64, std::pair<int, u32>> aac_index;  // 프레임 해시 -> (파일, 프레임)
    std::unordered_map<u32, AacOutput> aac_outputs;               // 디코더 출력 주소 -> 내용
    int aac_last_file = -1;
    u32 aac_last_frame = 0;
    bool aac_last_restart = false;
    std::vector<std::pair<int, u32>> aac_last_cands;  // 직전 프레임과 내용이 같은 (파일, 프레임) 들
    PendingLoop aac_pending_loop;
};

} // namespace AudioCore::HLE
