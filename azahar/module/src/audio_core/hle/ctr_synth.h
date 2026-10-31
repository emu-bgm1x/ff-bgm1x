// ff-bgm1x: 3DS(NW4C) 사운드 아카이브의 시퀀스(CSEQ) 재생기.
//
// 롬의 사운드 아카이브(BCSAR) 를 통째로 받아, 시퀀스·악기 뱅크(CBNK)·파형 아카이브(CWAR/CWAV) 를 직접 읽어 연주한다.
// 게임 속도와 무관하게 실제 시간 기준으로 돌린다 (배속 중 효과음을 1배속으로 내는 데 쓴다).
// 시퀀스 명령은 NW4C 의 명령표(시퀀스 데이터는 빅엔디언), 엔벨로프·음량 곡선은 NDS 표준 드라이버 방식을 따른다.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"

namespace AudioCore::HLE {

class CtrSynth {
public:
    // 사운드 아카이브를 읽어 사운드·뱅크·파형 아카이브 표를 만든다
    bool Load(std::vector<u8> bcsar);
    bool Loaded() const { return !sounds.empty(); }

    int FindSound(const std::string& name) const;  // 없으면 -1
    std::size_t SoundCount() const { return sounds.size(); }
    const std::string& SoundName(int sound) const { return sounds[sound].name; }
    bool IsSequence(int sound) const { return sounds[sound].seq_data != 0; }
    u32 SoundPlayer(int sound) const { return sounds[sound].player; }
    // CSEQ 파일 내용 전체 (게임 메모리의 시퀀스와 맞춰 보는 데 쓴다)
    const u8* SequenceFile(int sound, std::size_t& size) const;
    u32 SoundStart(int sound) const { return sounds[sound].start; }
    // 효과음 플레이어(이름에 bgm·strm·pv·voice 가 없는 플레이어)로 나오는 시퀀스인지
    bool IsSfx(int sound) const;
    // CSEQ 파일 전체의 FNV-1a 해시로 그 파일을 쓰는 시퀀스 사운드들을 찾는다
    static u64 FileHash(const u8* p, std::size_t size);
    const std::vector<int>* SoundsWithFile(u64 hash) const;

    static constexpr int kSlots = 16;
    bool Start(int slot, int sound);
    void Stop(int slot);        // 소리를 놓는다 (릴리스)
    bool Active(int slot) const;  // 트랙이나 소리 나는 채널이 남아 있는지
    int SlotSound(int slot) const { return slot_states[slot].sound; }
    u64 SlotAge(int slot) const { return rendered - slot_states[slot].start_rendered; }  // 시작한 뒤 만든 출력 샘플 수
    // 빈 슬롯 (없으면 가장 오래된 슬롯)
    int FreeSlot() const;
    void SetVolume(float v) { master = v; }

    // 스테레오 s16 을 frames 개 만들어 out 에 더한다
    void Render(s16* out, int frames, u32 sample_rate);

private:
    struct Sound {
        std::string name;
        u32 player = 0;
        int volume = 127;  // 사운드 음량 (0~127, 선형)
        std::size_t seq_file = 0, seq_file_size = 0;
        std::size_t seq_data = 0, seq_data_size = 0;  // CSEQ DATA 블록 본문 (0 이면 시퀀스 아님)
        u32 start = 0;                                // 시작 위치 (DATA 기준)
        std::vector<u32> banks;
    };
    struct Wave {
        u32 rate = 0;
        bool loop = false;
        u32 loop_start = 0, frames = 0;
        std::vector<std::vector<s16>> pcm;  // 채널별
    };
    struct Region {
        std::shared_ptr<const Wave> wave;
        int orig_key = 60, volume = 127, pan = 64;
        float pitch = 1.0f;
        int attack = 127, decay = 127, sustain = 127, hold = 0, release = 127;
    };
    struct Track {
        bool active = false, note_wait = true, mute = false, tie = false, porta = false, cmp = true;
        int pan = 0, pitch_bend = 0, volume = 127, expression = 127, bend_range = 2, prio = 64;
        int porta_key = 60, porta_time = 0, transpose = 0;
        int attack = -1, decay = -1, sustain = -1, release = -1;
        int prg = 0, bank_select = 0;
        int sweep_pitch = 0, mod_type = 0, mod_speed = 16, mod_depth = 0, mod_range = 1, mod_delay = 0;
        u32 pos = 0;
        s32 wait = 0;
        u32 call_stack[3] = {};
        u8 loop_count[3] = {};
        int call_depth = 0;
        int last_channel = -1;
    };
    struct Slot {
        bool active = false;
        int sound = -1;
        int volume = 127, tempo = 120, timebase = 48;
        float sound_volume = 1.0f;
        u64 start_rendered = 0;
        double tick_accum = 0;
        std::array<Track, 16> tracks{};
        std::array<s16, 16> vars{};
    };
    struct Channel {
        bool active = false;
        int slot = -1, track = -1, prio = 0;
        std::shared_ptr<const Wave> wave;
        double pos = 0, base_rate = 0, rate = 0;
        int key = 60, orig_key = 60, velocity = 127, region_volume = 127, region_pan = 64;
        float region_pitch = 1.0f;
        s32 length = 0;
        int env_status = 0;  // 0 어택, 1 홀드, 2 디케이, 3 서스테인, 4 릴리스
        s32 env_value = 0, attack_coef = 0, decay_coef = 0, release_coef = 0, hold_left = 0;
        int sustain = 127;
        s32 sweep_pitch = 0, sweep_length = 0, sweep_counter = 0;
        bool auto_sweep = true;
        int lfo_type = 0, lfo_speed = 16, lfo_depth = 0, lfo_range = 1, lfo_delay = 0, lfo_delay_counter = 0;
        u32 lfo_counter = 0;
        s32 user_decay = 0, user_pitch = 0, user_pan = 0;
        float vol_left = 0, vol_right = 0;
    };

    u8 Rd8(std::size_t at) const { return at < data.size() ? data[at] : 0xFF; }
    u16 Le16(std::size_t at) const;
    u32 Le32(std::size_t at) const;
    std::size_t FileOf(u32 file_id, std::size_t& size) const;
    std::shared_ptr<const Wave> LoadWave(u32 warc, u32 index);
    bool FindRegion(u32 bank, int prg, int key, int velocity, Region& out);
    bool Walk(std::size_t base, std::size_t end, u16 type, std::size_t at, int key, int velocity, Region& out,
              int depth);

    void DriverFrame();
    void SlotTick(int s);
    bool TrackStep(int s, int t);
    void NoteOn(int s, int t, int key, int velocity, s32 length);
    void ReleaseTrackChannels(int s, int t, int release);
    void CloseTrack(int s, int t);
    int AllocChannel(int prio);
    void UpdateChannel(Channel& ch);
    u16 Random();

    std::vector<u8> data;
    std::vector<Sound> sounds;
    std::unordered_map<std::string, int> sound_by_name;
    std::vector<u32> bank_files, warc_files;
    std::vector<std::string> player_names;
    std::unordered_map<u64, std::vector<int>> sounds_by_hash;
    u64 rendered = 0;
    std::vector<std::size_t> file_entries;
    std::size_t file_body = 0;
    std::unordered_map<u64, std::shared_ptr<const Wave>> wave_cache;

    std::array<Slot, kSlots> slot_states{};
    std::array<Channel, 32> channels{};
    std::array<s16, 16> global_vars{};
    u32 rng = 0x12345678;
    double frame_accum = 0;
    float master = 1.0f;
};

} // namespace AudioCore::HLE
