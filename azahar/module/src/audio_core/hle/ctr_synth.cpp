// ff-bgm1x: 3DS(NW4C) 시퀀스 재생기. 설명은 ctr_synth.h
#include "audio_core/hle/ctr_synth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace AudioCore::HLE {

namespace {

constexpr s32 kEnvMin = -92544;  // 가장 작은 음량(-723, 0.1dB 단위)을 7비트 올린 값
// 사운드 스레드가 한 번 도는 주기(초): DSP 오디오 프레임 하나 (160 샘플, 32728Hz)
constexpr double kDriverPeriod = 160.0 / 32728.0;
// 원본 출력과 맞춘 공통 배율: 오메가루비 효과음 6개를 녹음과 비교하면 원본 크기는 (사운드 음량을 곱하기 전
// 이 재생기 크기) x (사운드 음량/127) / 0.7 이었다 (2026-10-31 측정)
constexpr float kOutputGain = 1.0f / 0.7f;
// 어택 값 109~127 에 쓰는 계수 (127 에서 0 까지 역순으로 찾는다) - NDS 표준 드라이버와 같다
const u8 kAttackTable[19] = {0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54,
                             0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F};

// 0~127 의 음량 값을 0.1dB 단위로 (제곱 곡선: 40*log10)
s32 DecibelSquare(int v) {
    static s16 table[128];
    static bool built = false;
    if (!built) {
        table[0] = -32768;
        for (int i = 1; i < 128; i++)
            table[i] = static_cast<s16>(std::max(-723.0, std::round(400.0 * std::log10(i / 127.0))));
        built = true;
    }
    return table[std::clamp(v, 0, 127)];
}

s32 SinIdx(int index) {
    static s8 table[128];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 128; i++)
            table[i] = static_cast<s8>(std::lround(127.0 * std::sin(i * 2.0 * 3.14159265358979323846 / 128.0)));
        built = true;
    }
    return table[index & 127];
}

s32 AttackCoef(int attack) {
    attack = std::clamp(attack, 0, 127);
    return attack < 109 ? 255 - attack : kAttackTable[127 - attack];
}

s32 DecayCoef(int value) {
    value = std::clamp(value, 0, 127);
    if (value == 127) return 0xFFFF;
    if (value == 126) return 0x3C00;
    if (value < 50) return value * 2 + 1;
    return 0x1E00 / (126 - value);
}

} // namespace

u16 CtrSynth::Le16(std::size_t at) const {
    return at + 2 <= data.size() ? static_cast<u16>(data[at] | (data[at + 1] << 8)) : 0;
}

u32 CtrSynth::Le32(std::size_t at) const {
    return at + 4 <= data.size() ? static_cast<u32>(data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) |
                                                    (static_cast<u32>(data[at + 3]) << 24))
                                 : 0;
}

std::size_t CtrSynth::FileOf(u32 file_id, std::size_t& size) const {
    size = 0;
    if (file_id >= file_entries.size()) return 0;
    const std::size_t e = file_entries[file_id];
    if (Le16(e) != 0x220C) return 0;  // 아카이브 안에 든 파일만
    const std::size_t loc = e + static_cast<s32>(Le32(e + 4));
    const s32 off = static_cast<s32>(Le32(loc + 4));
    const u32 len = Le32(loc + 8);
    if (off < 0 || len == 0xFFFFFFFF || file_body + off + len > data.size()) return 0;
    size = len;
    return file_body + off;
}

bool CtrSynth::Load(std::vector<u8> bcsar) {
    data = std::move(bcsar);
    sounds.clear();
    sound_by_name.clear();
    bank_files.clear();
    warc_files.clear();
    file_entries.clear();
    wave_cache.clear();
    if (data.size() < 0x40 || std::memcmp(data.data(), "CSAR", 4) != 0) return false;
    auto blocks = [&](std::size_t base) {
        std::unordered_map<u16, std::size_t> out;
        const u16 nb = Le16(base + 0x10);
        for (u16 i = 0; i < nb && i < 16; ++i) out[Le16(base + 0x14 + 12 * i)] = base + Le32(base + 0x18 + 12 * i);
        return out;
    };
    const auto sar = blocks(0);
    if (!sar.count(0x2000) || !sar.count(0x2001) || !sar.count(0x2002)) return false;

    std::vector<std::string> strings;
    {
        const std::size_t body = sar.at(0x2000) + 8;
        std::size_t tab = 0;
        for (int i = 0; i < 2; ++i)
            if (Le16(body + 8 * i) == 0x2400) tab = body + static_cast<s32>(Le32(body + 8 * i + 4));
        const u32 cnt = tab ? Le32(tab) : 0;
        for (u32 i = 0; i < cnt && tab + 16 + 12 * i <= data.size(); ++i) {
            const std::size_t so = tab + static_cast<s32>(Le32(tab + 4 + 12 * i + 4));
            std::string str;
            for (std::size_t k = so; k < data.size() && data[k] && str.size() < 128; ++k)
                str.push_back(static_cast<char>(data[k]));
            strings.push_back(std::move(str));
        }
    }
    std::unordered_map<u16, std::size_t> tables;
    const std::size_t info = sar.at(0x2001) + 8;
    for (int i = 0; i < 8; ++i) tables[Le16(info + 8 * i)] = info + static_cast<s32>(Le32(info + 8 * i + 4));
    auto entries = [&](u16 type) {
        std::vector<std::size_t> out;
        if (!tables.count(type)) return out;
        const std::size_t t = tables[type];
        for (u32 i = 0; i < Le32(t) && t + 12 + 8 * i <= data.size(); ++i)
            out.push_back(t + static_cast<s32>(Le32(t + 4 + 8 * i + 4)));
        return out;
    };
    file_entries = entries(0x2106);
    file_body = sar.at(0x2002) + 8;
    for (std::size_t e : entries(0x2101)) bank_files.push_back(Le32(e));
    player_names.clear();
    for (std::size_t e : entries(0x2102)) {
        std::string nm;
        if ((Le32(e + 4) & 1) && Le32(e + 8) < strings.size()) nm = strings[Le32(e + 8)];
        for (char& c : nm) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        player_names.push_back(std::move(nm));
    }
    for (std::size_t e : entries(0x2103)) warc_files.push_back(Le32(e));

    for (std::size_t e : entries(0x2100)) {
        Sound s;
        const u32 opt = Le32(e + 0x14);
        if ((opt & 1) && Le32(e + 0x18) < strings.size()) s.name = strings[Le32(e + 0x18)];
        s.player = Le32(e + 4) & 0xFFFFFF;
        s.volume = Rd8(e + 8);
        if (Le16(e + 0xC) == 0x2203) {
            const std::size_t det = e + static_cast<s32>(Le32(e + 0x10));
            std::size_t len = 0;
            const std::size_t seq = FileOf(Le32(e), len);
            if (seq && len >= 0x40 && std::memcmp(data.data() + seq, "CSEQ", 4) == 0) {
                const u16 nb = Le16(seq + 0x10);
                for (u16 i = 0; i < nb && i < 4; ++i) {
                    if (Le16(seq + 0x14 + 12 * i) != 0x5000) continue;
                    const u32 off = Le32(seq + 0x18 + 12 * i), sz = Le32(seq + 0x1C + 12 * i);
                    if (off + sz <= len && sz > 8) {
                        s.seq_file = seq;
                        s.seq_file_size = len;
                        s.seq_data = seq + off + 8;
                        s.seq_data_size = sz - 8;
                    }
                }
                s.start = Le32(det + 0x10);
                const std::size_t bt = det + static_cast<s32>(Le32(det + 4));
                for (u32 k = 0; k < Le32(bt) && k < 4; ++k) s.banks.push_back(Le32(bt + 4 + 4 * k) & 0xFFFFFF);
            }
        }
        if (!s.name.empty()) sound_by_name.emplace(s.name, static_cast<int>(sounds.size()));
        if (s.seq_file) sounds_by_hash[FileHash(data.data() + s.seq_file, s.seq_file_size)].push_back(
            static_cast<int>(sounds.size()));
        sounds.push_back(std::move(s));
    }
    return !sounds.empty();
}

u64 CtrSynth::FileHash(const u8* p, std::size_t size) {
    u64 h = 0xCBF29CE484222325ULL;
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 0x100000001B3ULL;
    return h;
}

const std::vector<int>* CtrSynth::SoundsWithFile(u64 hash) const {
    auto it = sounds_by_hash.find(hash);
    return it == sounds_by_hash.end() ? nullptr : &it->second;
}

bool CtrSynth::IsSfx(int sound) const {
    if (sound < 0 || static_cast<std::size_t>(sound) >= sounds.size() || !sounds[sound].seq_data) return false;
    const u32 p = sounds[sound].player;
    if (p >= player_names.size()) return true;
    const std::string& nm = player_names[p];
    for (const char* bad : {"bgm", "strm", "pv", "voice"})
        if (nm.find(bad) != std::string::npos) return false;
    return true;
}

int CtrSynth::FreeSlot() const {
    int oldest = 0;
    for (int s = 0; s < kSlots; ++s) {
        if (!Active(s)) return s;
        if (slot_states[s].start_rendered < slot_states[oldest].start_rendered) oldest = s;
    }
    return oldest;
}

int CtrSynth::FindSound(const std::string& name) const {
    auto it = sound_by_name.find(name);
    return it == sound_by_name.end() ? -1 : it->second;
}

const u8* CtrSynth::SequenceFile(int sound, std::size_t& size) const {
    size = 0;
    if (sound < 0 || static_cast<std::size_t>(sound) >= sounds.size() || !sounds[sound].seq_file) return nullptr;
    size = sounds[sound].seq_file_size;
    return data.data() + sounds[sound].seq_file;
}

std::shared_ptr<const CtrSynth::Wave> CtrSynth::LoadWave(u32 warc, u32 index) {
    const u64 key = (static_cast<u64>(warc) << 32) | index;
    if (auto it = wave_cache.find(key); it != wave_cache.end()) return it->second;
    wave_cache[key] = nullptr;
    if (warc >= warc_files.size()) return nullptr;
    std::size_t war_len = 0;
    const std::size_t war = FileOf(warc_files[warc], war_len);
    if (!war || std::memcmp(data.data() + war, "CWAR", 4) != 0) return nullptr;
    std::size_t winfo = 0, wfile = 0;
    for (u16 i = 0; i < Le16(war + 0x10) && i < 4; ++i) {
        const u16 id = Le16(war + 0x14 + 12 * i);
        if (id == 0x6800) winfo = war + Le32(war + 0x18 + 12 * i) + 8;
        if (id == 0x6801) wfile = war + Le32(war + 0x18 + 12 * i) + 8;
    }
    if (!winfo || !wfile || index >= Le32(winfo)) return nullptr;
    const std::size_t cwav = wfile + static_cast<s32>(Le32(winfo + 4 + 12 * index + 4));
    if (cwav + 0x40 > data.size() || std::memcmp(data.data() + cwav, "CWAV", 4) != 0) return nullptr;
    std::size_t ci = 0, body = 0;
    for (u16 i = 0; i < Le16(cwav + 0x10) && i < 4; ++i) {
        const u16 id = Le16(cwav + 0x14 + 12 * i);
        if (id == 0x7000) ci = cwav + Le32(cwav + 0x18 + 12 * i) + 8;
        if (id == 0x7001) body = cwav + Le32(cwav + 0x18 + 12 * i) + 8;
    }
    if (!ci || !body) return nullptr;
    auto w = std::make_shared<Wave>();
    const u8 encoding = Rd8(ci);
    w->loop = Rd8(ci + 1) != 0;
    w->rate = Le32(ci + 4);
    w->loop_start = Le32(ci + 8);
    w->frames = Le32(ci + 12);
    const std::size_t ct = ci + 0x14;
    const u32 nch = std::min<u32>(Le32(ct), 2);
    if (!w->rate || !w->frames || !nch || w->frames > 0x1000000) return nullptr;
    for (u32 c = 0; c < nch; ++c) {
        const std::size_t chi = ct + static_cast<s32>(Le32(ct + 4 + 8 * c + 4));
        const std::size_t samples = body + static_cast<s32>(Le32(chi + 4));
        std::vector<s16> pcm(w->frames);
        if (encoding == 0) {  // PCM8
            for (u32 i = 0; i < w->frames; ++i) pcm[i] = static_cast<s16>(static_cast<s8>(Rd8(samples + i)) << 8);
        } else if (encoding == 1) {  // PCM16
            for (u32 i = 0; i < w->frames; ++i) pcm[i] = static_cast<s16>(Le16(samples + 2 * i));
        } else if (encoding == 2) {  // DSP-ADPCM: 8바이트 프레임에 14샘플
            const std::size_t ai = chi + static_cast<s32>(Le32(chi + 12));
            s16 coef[16];
            for (int k = 0; k < 16; ++k) coef[k] = static_cast<s16>(Le16(ai + 2 * k));
            s32 h1 = static_cast<s16>(Le16(ai + 34)), h2 = static_cast<s16>(Le16(ai + 36));
            for (u32 i = 0; i < w->frames; ++i) {
                const std::size_t frame = samples + (i / 14) * 8;
                const u8 ps = Rd8(frame);
                const int n = i % 14;
                const u8 b = Rd8(frame + 1 + n / 2);
                s32 nib = (n & 1) ? (b & 0xF) : (b >> 4);
                if (nib >= 8) nib -= 16;
                const int pred = (ps >> 4) & 7, scale = ps & 0xF;
                const s32 v = ((nib << scale) << 11) + coef[pred * 2] * h1 + coef[pred * 2 + 1] * h2 + 1024;
                const s32 smp = std::clamp(v >> 11, -32768, 32767);
                h2 = h1;
                h1 = smp;
                pcm[i] = static_cast<s16>(smp);
            }
        } else {
            return nullptr;  // IMA-ADPCM 등: 아직 다루지 않는다
        }
        w->pcm.push_back(std::move(pcm));
    }
    if (w->loop_start >= w->frames) w->loop = false;
    std::shared_ptr<const Wave> out = w;
    wave_cache[key] = out;
    return out;
}

// 뱅크의 악기 구조를 따라 내려가 (키, 세기) 에 맞는 속도 영역을 찾는다. 참조 위치는 그 구조의 시작 기준이다
bool CtrSynth::Walk(std::size_t base, std::size_t end, u16 type, std::size_t at, int key, int velocity,
                    Region& out, int depth) {
    if ((depth & 0xFF) > 8 || at + 8 > end) return false;
    // 표에서 고를 값: 악기 단계에서는 키, 키 영역 단계(0x5901 아래)에서는 세기
    const bool by_velocity = depth >= 0x100;
    const int pick = by_velocity ? velocity : key;
    const int next_depth = (depth & 0xFF) + 1;
    switch (type) {
    case 0x5900:
    case 0x6000:
        return Walk(base, end, Le16(at), at + static_cast<s32>(Le32(at + 4)), key, velocity, out,
                    next_depth | (depth & 0x100));
    case 0x5901:
        return Walk(base, end, Le16(at), at + static_cast<s32>(Le32(at + 4)), key, velocity, out,
                    next_depth | 0x100);
    case 0x6001: {  // 범위 표: u32 개수, u8 경계[개수](4바이트 정렬), 참조[개수]
        const u32 cnt = Le32(at);
        if (cnt == 0 || cnt > 128) return false;
        const std::size_t refs = at + 4 + ((cnt + 3) & ~3u);
        for (u32 k = 0; k < cnt; ++k) {
            if (pick <= Rd8(at + 4 + k)) {
                return Walk(base, end, Le16(refs + 8 * k), at + static_cast<s32>(Le32(refs + 8 * k + 4)), key,
                            velocity, out, next_depth | (depth & 0x100));
            }
        }
        return false;
    }
    case 0x6002: {  // 번호 표: s8 처음, s8 끝, u16, 참조[끝-처음+1]
        const int lo = static_cast<s8>(Rd8(at)), hi = static_cast<s8>(Rd8(at + 1));
        if (pick < lo || pick > hi) return false;
        const std::size_t ref = at + 4 + 8 * (pick - lo);
        return Walk(base, end, Le16(ref), at + static_cast<s32>(Le32(ref + 4)), key, velocity, out,
                    next_depth | (depth & 0x100));
    }
    case 0x5902: {  // 속도 영역: u32 파형 id 번호, u32 플래그, 켜진 비트 순서대로 u32 값
        const u32 wave_id = Le32(at);
        const u32 flags = Le32(at + 4);
        std::size_t v = at + 8;
        u32 adshr_off = 0;
        for (int bit = 0; bit < 32; ++bit) {
            if (!(flags & (1u << bit))) continue;
            const u32 val = Le32(v);
            v += 4;
            switch (bit) {
            case 0: out.orig_key = val & 0x7F; break;
            case 1: out.volume = val & 0x7F; break;
            case 2: out.pan = val & 0x7F; break;
            case 3: std::memcpy(&out.pitch, &val, 4); break;
            case 9: adshr_off = val; break;
            default: break;
            }
        }
        if (adshr_off) {
            const std::size_t ref = at + adshr_off;
            const std::size_t a = ref + static_cast<s32>(Le32(ref + 4));
            out.attack = Rd8(a);
            out.decay = Rd8(a + 1);
            out.sustain = Rd8(a + 2);
            out.hold = Rd8(a + 3);
            out.release = Rd8(a + 4);
        }
        // 파형 id 표는 FindRegion 이 넘겨 준 base(뱅크 INFO 본문) 에서 찾는다
        std::size_t wave_ids = 0;
        for (int r = 0; r < 2; ++r)
            if (Le16(base + 8 * r) == 0x0100) wave_ids = base + static_cast<s32>(Le32(base + 8 * r + 4));
        if (!wave_ids || wave_id >= Le32(wave_ids)) return false;
        const u32 warc = Le32(wave_ids + 4 + 8 * wave_id) & 0xFFFFFF;
        const u32 index = Le32(wave_ids + 8 + 8 * wave_id);
        out.wave = LoadWave(warc, index);
        return out.wave != nullptr;
    }
    default:
        return false;
    }
}

bool CtrSynth::FindRegion(u32 bank, int prg, int key, int velocity, Region& out) {
    if (bank >= bank_files.size()) return false;
    std::size_t len = 0;
    const std::size_t bnk = FileOf(bank_files[bank], len);
    if (!bnk || std::memcmp(data.data() + bnk, "CBNK", 4) != 0) return false;
    const std::size_t info = bnk + Le32(bnk + 0x18) + 8;
    std::size_t insts = 0;
    for (int r = 0; r < 2; ++r)
        if (Le16(info + 8 * r) == 0x0101) insts = info + static_cast<s32>(Le32(info + 8 * r + 4));
    if (!insts || prg < 0 || static_cast<u32>(prg) >= Le32(insts)) return false;
    const std::size_t ref = insts + 4 + 8 * prg;
    return Walk(info, bnk + len, Le16(ref), insts + static_cast<s32>(Le32(ref + 4)), key, velocity, out, 0);
}

u16 CtrSynth::Random() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<u16>(rng >> 16);
}

bool CtrSynth::Start(int s, int sound) {
    if (s < 0 || s >= kSlots || sound < 0 || static_cast<std::size_t>(sound) >= sounds.size()) return false;
    const Sound& snd = sounds[sound];
    if (!snd.seq_data || snd.start >= snd.seq_data_size) return false;
    for (int t = 0; t < 16; ++t) ReleaseTrackChannels(s, t, -1);
    Slot& sl = slot_states[s];
    sl = Slot();
    sl.sound = sound;
    sl.sound_volume = snd.volume / 127.0f;
    sl.start_rendered = rendered;
    sl.vars.fill(-1);
    sl.tracks[0].active = true;
    sl.tracks[0].pos = static_cast<u32>(snd.start);
    sl.active = true;
    return true;
}

void CtrSynth::Stop(int s) {
    if (s < 0 || s >= kSlots) return;
    for (int t = 0; t < 16; ++t) CloseTrack(s, t);
    for (Channel& ch : channels) {
        if (ch.active && ch.slot == s && ch.env_status != 4) ch.env_status = 4;
    }
    slot_states[s].active = false;
}

bool CtrSynth::Active(int s) const {
    if (s < 0 || s >= kSlots) return false;
    if (slot_states[s].active) return true;
    for (const Channel& ch : channels)
        if (ch.active && ch.slot == s) return true;
    return false;
}

void CtrSynth::ReleaseTrackChannels(int s, int t, int release) {
    for (Channel& ch : channels) {
        if (!ch.active || ch.slot != s || ch.track != t || ch.env_status == 4) continue;
        if (release >= 0) ch.release_coef = DecayCoef(release);
        ch.env_status = 4;
        ch.prio = 1;
    }
    slot_states[s].tracks[t].last_channel = -1;
}

void CtrSynth::CloseTrack(int s, int t) {
    // NW4C 의 효과음은 "길이 0(끝까지) 음표 + 바로 끝" 이 흔하다. 트랙이 끝나도 울리는 소리는 놓지 않는다
    slot_states[s].tracks[t].active = false;
    slot_states[s].tracks[t].last_channel = -1;
}

int CtrSynth::AllocChannel(int prio) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        if (!channels[i].active) return i;
        if (best < 0 || channels[i].prio < channels[best].prio) best = i;
    }
    return channels[best].prio <= prio ? best : -1;
}

void CtrSynth::NoteOn(int s, int t, int key, int velocity, s32 length) {
    Slot& sl = slot_states[s];
    Track& tr = sl.tracks[t];
    const Sound& snd = sounds[sl.sound];
    int ci = -1;
    if (tr.tie && tr.last_channel >= 0) {
        Channel& old = channels[tr.last_channel];
        if (old.active && old.slot == s && old.track == t && old.env_status != 4) {
            ci = tr.last_channel;
            old.key = key;
            old.velocity = velocity;
        }
    }
    if (ci < 0) {
        Region r;
        const u32 bank_index = static_cast<u32>(std::clamp(tr.bank_select, 0, 3));
        if (bank_index >= snd.banks.size() || !FindRegion(snd.banks[bank_index], tr.prg, key, velocity, r)) return;
        ci = AllocChannel(std::min(255, tr.prio));
        if (ci < 0) return;
        Channel c;
        c.active = true;
        c.slot = s;
        c.track = t;
        c.prio = std::min(255, tr.prio);
        c.wave = r.wave;
        c.base_rate = r.wave->rate;
        c.key = key;
        c.orig_key = r.orig_key;
        c.velocity = velocity;
        c.region_volume = r.volume;
        c.region_pan = r.pan;
        c.region_pitch = r.pitch > 0 ? r.pitch : 1.0f;
        c.length = tr.tie ? -1 : length;
        c.attack_coef = AttackCoef(tr.attack >= 0 ? tr.attack : r.attack);
        c.decay_coef = DecayCoef(tr.decay >= 0 ? tr.decay : r.decay);
        c.sustain = tr.sustain >= 0 ? tr.sustain : r.sustain;
        c.release_coef = DecayCoef(tr.release >= 0 ? tr.release : r.release);
        c.hold_left = r.hold;
        c.env_value = kEnvMin;
        c.env_status = 0;
        channels[ci] = c;
    }
    Channel& ch = channels[ci];
    ch.sweep_pitch = tr.sweep_pitch;
    if (tr.porta) ch.sweep_pitch += (tr.porta_key - key) << 6;
    if (tr.porta_time == 0) {
        ch.sweep_length = length;
        ch.auto_sweep = false;
    } else {
        ch.sweep_length = (tr.porta_time * tr.porta_time * std::abs(ch.sweep_pitch)) >> 11;
        ch.auto_sweep = true;
    }
    ch.sweep_counter = 0;
    ch.lfo_type = tr.mod_type;
    ch.lfo_speed = tr.mod_speed;
    ch.lfo_depth = tr.mod_depth;
    ch.lfo_range = tr.mod_range;
    ch.lfo_delay = tr.mod_delay;
    tr.last_channel = ci;
}

bool CtrSynth::TrackStep(int s, int t) {
    Slot& sl = slot_states[s];
    Track& tr = sl.tracks[t];
    const Sound& snd = sounds[sl.sound];
    if (tr.wait > 0) {
        tr.wait--;
        if (tr.wait > 0) return true;
    }
    // 시퀀스 데이터는 빅엔디언
    auto u8at = [&]() -> u32 { return tr.pos < snd.seq_data_size ? data[snd.seq_data + tr.pos++] : (tr.pos++, 0xFF); };
    auto s16at = [&]() {
        const u32 hi = u8at(), lo = u8at();
        return static_cast<s32>(static_cast<s16>((hi << 8) | lo));
    };
    auto u24at = [&]() {
        const u32 a = u8at(), b = u8at(), c = u8at();
        return (a << 16) | (b << 8) | c;
    };
    auto varlen = [&]() {
        s32 v = 0;
        for (int i = 0; i < 4; i++) {
            const u32 b = u8at();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    };
    auto var = [&](int n) -> s16& {
        n &= 31;
        return n < 16 ? sl.vars[n] : global_vars[n - 16];
    };

    for (int guard = 0; guard < 4096 && tr.wait == 0; guard++) {
        bool run = true;
        int arg_kind = 0;  // 0 보통, 1 난수(min,max), 2 변수
        int time_kind = 0; // 시간 접두: 1 값(s16), 2 난수(s16 두 개), 3 변수(u8). 뒤에 붙는 값은 읽고 버린다
        u32 cmd = u8at();
        if (cmd == 0xA2) {
            run = tr.cmp;
            cmd = u8at();
        }
        if (cmd == 0xA0) {
            arg_kind = 1;
            cmd = u8at();
        } else if (cmd == 0xA1) {
            arg_kind = 2;
            cmd = u8at();
        } else if (cmd >= 0xA3 && cmd <= 0xA5) {
            time_kind = static_cast<int>(cmd - 0xA2);
            cmd = u8at();
        }
        auto last_arg = [&](int kind) -> s32 {  // kind: 0 u8, 1 s16, 2 가변 길이, 3 s8, 4 u16
            if (arg_kind == 1) {
                const s32 lo = s16at(), hi = s16at();
                const s32 span = hi - lo + 1;
                return span > 0 ? lo + static_cast<s32>((static_cast<u32>(Random()) * static_cast<u32>(span)) >> 16)
                                : lo;
            }
            if (arg_kind == 2) return var(static_cast<int>(u8at()));
            switch (kind) {
            case 0: return static_cast<s32>(u8at());
            case 1: return s16at();
            case 2: return varlen();
            case 3: return static_cast<s8>(u8at());
            default: { const u32 hi = u8at(), lo = u8at(); return static_cast<s32>((hi << 8) | lo); }
            }
        };
        auto skip_time = [&]() {
            if (time_kind == 1) s16at();
            else if (time_kind == 2) { s16at(); s16at(); }
            else if (time_kind == 3) u8at();
        };

        if (cmd < 0x80) {
            const int velocity = static_cast<int>(u8at());
            const s32 length = last_arg(2);
            skip_time();
            const int key = std::clamp(static_cast<int>(cmd) + tr.transpose, 0, 127);
            if (!run) continue;
            if (!tr.mute) NoteOn(s, t, key, velocity, length);
            tr.porta_key = key;
            if (tr.note_wait) tr.wait = length;
            continue;
        }
        switch (cmd) {
        case 0x80: { const s32 v = last_arg(2); skip_time(); if (run) tr.wait = v; break; }
        case 0x81: { const s32 v = last_arg(2); skip_time(); if (run) tr.prg = v & 0xFFFF; break; }
        case 0x88: {
            const int n = static_cast<int>(u8at()) & 15;
            const u32 off = u24at();
            if (run && n != t) {
                sl.tracks[n] = Track();
                sl.tracks[n].active = true;
                sl.tracks[n].pos = off;
            }
            break;
        }
        case 0x89: { const u32 off = u24at(); if (run) tr.pos = off; break; }
        case 0x8A: {
            const u32 off = u24at();
            if (run && tr.call_depth < 3) {
                tr.call_stack[tr.call_depth++] = tr.pos;
                tr.pos = off;
            }
            break;
        }
        case 0xE0: { const s32 v = last_arg(1); skip_time(); if (run) tr.mod_delay = v; break; }
        case 0xE1: { const s32 v = last_arg(1); skip_time(); if (run && v > 0) sl.tempo = v; break; }
        case 0xE3: { const s32 v = last_arg(1); skip_time(); if (run) tr.sweep_pitch = v; break; }
        case 0xE4: { last_arg(1); skip_time(); break; }  // 모듈레이션 주기: 다루지 않음
        case 0xF0: {
            const u32 sub = u8at();
            if ((sub >= 0x80 && sub <= 0x8B) || (sub >= 0x90 && sub <= 0x95)) {
                s16& v = var(static_cast<int>(u8at()));
                const s32 a = last_arg(1);
                skip_time();
                if (!run) break;
                switch (sub) {
                case 0x80: v = static_cast<s16>(a); break;
                case 0x81: v = static_cast<s16>(v + a); break;
                case 0x82: v = static_cast<s16>(v - a); break;
                case 0x83: v = static_cast<s16>(v * a); break;
                case 0x84: if (a) v = static_cast<s16>(v / a); break;
                case 0x85: v = static_cast<s16>(a >= 0 ? v << a : v >> -a); break;
                case 0x86: {
                    const s32 r = static_cast<s32>((static_cast<u32>(Random()) * static_cast<u32>(std::abs(a) + 1)) >> 16);
                    v = static_cast<s16>(a < 0 ? -r : r);
                    break;
                }
                case 0x87: v = static_cast<s16>(v & a); break;
                case 0x88: v = static_cast<s16>(v | a); break;
                case 0x89: v = static_cast<s16>(v ^ a); break;
                case 0x8A: v = static_cast<s16>(~a); break;
                case 0x8B: if (a) v = static_cast<s16>(v % a); break;
                case 0x90: tr.cmp = v == a; break;
                case 0x91: tr.cmp = v >= a; break;
                case 0x92: tr.cmp = v > a; break;
                case 0x93: tr.cmp = v <= a; break;
                case 0x94: tr.cmp = v < a; break;
                case 0x95: tr.cmp = v != a; break;
                }
            } else if (sub >= 0xE0 && sub <= 0xE6) {
                last_arg(1);  // 사용자 호출, 모듈레이션 2~4 지연·주기: 다루지 않음
                skip_time();
            } else if (sub >= 0xA0 && sub <= 0xB1) {
                last_arg(0);  // 모듈레이션 2~4: 다루지 않음
                skip_time();
            } else {
                return false;  // 모르는 확장 명령
            }
            break;
        }
        case 0xFB: break;  // 엔벨로프 초기화
        case 0xFC:
            if (run && tr.call_depth > 0) {
                u8& count = tr.loop_count[tr.call_depth - 1];
                if (count == 0 || --count != 0)
                    tr.pos = tr.call_stack[tr.call_depth - 1];
                else
                    tr.call_depth--;
            }
            break;
        case 0xFD:
            if (run && tr.call_depth > 0) tr.pos = tr.call_stack[--tr.call_depth];
            break;
        case 0xFE: u8at(); u8at(); break;  // 트랙 할당 표시
        case 0xFF:
            if (run) return false;
            break;
        default:
            if (cmd >= 0xB0 && cmd <= 0xDF) {
                const s32 a = last_arg(cmd == 0xC3 || cmd == 0xC4 ? 3 : 0);
                skip_time();
                if (!run) break;
                switch (cmd) {
                case 0xB0: if (a > 0) sl.timebase = a; break;
                case 0xB6: tr.bank_select = a; break;
                case 0xC0: tr.pan = a - 64; break;
                case 0xC1: tr.volume = a; break;
                case 0xC2: sl.volume = a; break;
                case 0xC3: tr.transpose = a; break;
                case 0xC4: tr.pitch_bend = a; break;
                case 0xC5: tr.bend_range = a; break;
                case 0xC6: tr.prio = a; break;
                case 0xC7: tr.note_wait = a & 1; break;
                case 0xC8:
                    tr.tie = a & 1;
                    ReleaseTrackChannels(s, t, -1);
                    break;
                case 0xC9:
                    tr.porta_key = std::clamp(a + tr.transpose, 0, 127);
                    tr.porta = true;
                    break;
                case 0xCA: tr.mod_depth = a; break;
                case 0xCB: tr.mod_speed = a; break;
                case 0xCC: tr.mod_type = a; break;
                case 0xCD: tr.mod_range = a; break;
                case 0xCE: tr.porta = a & 1; break;
                case 0xCF: tr.porta_time = a; break;
                case 0xD0: tr.attack = a; break;
                case 0xD1: tr.decay = a; break;
                case 0xD2: tr.sustain = a; break;
                case 0xD3: tr.release = a; break;
                case 0xD4:
                    if (tr.call_depth < 3) {
                        tr.call_stack[tr.call_depth] = tr.pos;
                        tr.loop_count[tr.call_depth] = static_cast<u8>(a);
                        tr.call_depth++;
                    }
                    break;
                case 0xD5: tr.expression = a; break;
                case 0xDD: tr.mute = a != 0; if (tr.mute) ReleaseTrackChannels(s, t, 127); break;
                default: break;  // 필터·이펙트 보내기·서라운드 등: 소리 크기·높이와 무관하거나 다루지 않음
                }
                break;
            }
            return false;  // 모르는 명령: 트랙을 끝낸다
        }
    }
    return true;
}

void CtrSynth::SlotTick(int s) {
    Slot& sl = slot_states[s];
    bool any = false;
    for (int t = 0; t < 16; ++t) {
        if (!sl.tracks[t].active) continue;
        for (Channel& ch : channels) {
            if (!ch.active || ch.slot != s || ch.track != t) continue;
            if (ch.length > 0 && --ch.length == 0 && ch.env_status != 4) {
                ch.env_status = 4;
                ch.prio = 1;
            }
            if (!ch.auto_sweep && ch.sweep_counter < ch.sweep_length) ch.sweep_counter++;
        }
        if (!TrackStep(s, t))
            CloseTrack(s, t);
        else
            any = true;
    }
    if (!any) sl.active = false;
}

void CtrSynth::UpdateChannel(Channel& ch) {
    switch (ch.env_status) {
    case 0:
        ch.env_value = -((-ch.env_value * ch.attack_coef) >> 8);
        if (ch.env_value == 0) ch.env_status = ch.hold_left > 0 ? 1 : 2;
        break;
    case 1:
        if (--ch.hold_left <= 0) ch.env_status = 2;
        break;
    case 2: {
        const s32 sustain = DecibelSquare(ch.sustain) << 7;
        ch.env_value -= ch.decay_coef;
        if (ch.env_value <= sustain) {
            ch.env_value = sustain;
            ch.env_status = 3;
        }
        break;
    }
    case 4:
        ch.env_value -= ch.release_coef;
        if (ch.env_value <= kEnvMin) {
            ch.active = false;
            return;
        }
        break;
    default:
        break;
    }

    s32 sweep = 0;
    if (ch.sweep_pitch != 0 && ch.sweep_length > 0 && ch.sweep_counter < ch.sweep_length) {
        sweep = static_cast<s32>(static_cast<s64>(ch.sweep_pitch) * (ch.sweep_length - ch.sweep_counter) /
                                 ch.sweep_length);
        if (ch.auto_sweep) ch.sweep_counter++;
    }
    s32 lfo_pitch = 0, lfo_volume = 0, lfo_pan = 0;
    if (ch.lfo_depth != 0) {
        if (ch.lfo_delay_counter < ch.lfo_delay) {
            ch.lfo_delay_counter++;
        } else {
            const s64 value = static_cast<s64>(SinIdx(static_cast<int>(ch.lfo_counter >> 8))) * ch.lfo_depth *
                              ch.lfo_range;
            if (ch.lfo_type == 0) lfo_pitch = static_cast<s32>((value << 6) >> 14);
            else if (ch.lfo_type == 1) lfo_volume = static_cast<s32>((value * 60) >> 14);
            else lfo_pan = static_cast<s32>((value << 6) >> 14);
            ch.lfo_counter = (ch.lfo_counter + (ch.lfo_speed << 6)) & 0x7FFF;
        }
    }
    const Slot& sl = slot_states[ch.slot];
    const Track& tr = sl.tracks[ch.track];
    if (tr.active) {
        ch.user_pitch = (tr.pitch_bend * tr.bend_range * 64) >> 7;
        ch.user_pan = tr.pan;
        ch.user_decay = DecibelSquare(tr.volume) + DecibelSquare(tr.expression);
    }
    const s32 volume = DecibelSquare(ch.velocity) + DecibelSquare(ch.region_volume) + (ch.env_value >> 7) +
                       ch.user_decay + DecibelSquare(sl.volume) + lfo_volume;
    const float amp = (volume <= -723 ? 0.0f : std::pow(10.0f, std::min(volume, 0) / 200.0f)) * sl.sound_volume *
                      kOutputGain;
    const int pan = std::clamp(ch.region_pan + ch.user_pan + lfo_pan, 0, 127);
    ch.vol_left = amp * (127 - pan) / 127.0f;
    ch.vol_right = amp * pan / 127.0f;
    const s32 pitch = (ch.key - ch.orig_key) * 64 + sweep + ch.user_pitch + lfo_pitch;
    ch.rate = ch.base_rate * ch.region_pitch * std::pow(2.0, pitch / 768.0);
}

void CtrSynth::DriverFrame() {
    for (int s = 0; s < kSlots; ++s) {
        Slot& sl = slot_states[s];
        if (!sl.active) continue;
        // 틱 = 템포 x 타임베이스 / 60 (초당)
        sl.tick_accum += sl.tempo * sl.timebase / 60.0 * kDriverPeriod;
        for (int guard = 0; sl.tick_accum >= 1.0 && sl.active && guard < 64; ++guard) {
            sl.tick_accum -= 1.0;
            SlotTick(s);
        }
    }
    for (Channel& ch : channels)
        if (ch.active) UpdateChannel(ch);
}

void CtrSynth::Render(s16* out, int frames, u32 sample_rate) {
    if (!sample_rate) return;
    rendered += static_cast<u64>(std::max(frames, 0));
    const double per_frame = sample_rate * kDriverPeriod;
    for (int i = 0; i < frames; ++i) {
        if (frame_accum <= 0) {
            DriverFrame();
            frame_accum += per_frame;
        }
        frame_accum -= 1.0;
        float left = 0, right = 0;
        for (Channel& ch : channels) {
            if (!ch.active || !ch.wave) continue;
            const Wave& w = *ch.wave;
            const u32 idx = static_cast<u32>(ch.pos);
            if (idx >= w.frames) {
                ch.active = false;
                continue;
            }
            const float frac = static_cast<float>(ch.pos - idx);
            u32 nxt = idx + 1;
            if (nxt >= w.frames) nxt = w.loop ? w.loop_start : idx;
            const auto& c0 = w.pcm[0];
            const float s0 = c0[idx] + (c0[nxt] - c0[idx]) * frac;
            const auto& c1 = w.pcm.size() > 1 ? w.pcm[1] : w.pcm[0];
            const float s1 = c1[idx] + (c1[nxt] - c1[idx]) * frac;
            left += s0 * ch.vol_left;
            right += s1 * ch.vol_right;
            ch.pos += ch.rate / sample_rate;
            if (ch.pos >= w.frames) {
                if (w.loop && w.frames > w.loop_start)
                    ch.pos = w.loop_start + std::fmod(ch.pos - w.frames, static_cast<double>(w.frames - w.loop_start));
                else
                    ch.active = false;
            }
        }
        out[i * 2] = static_cast<s16>(std::clamp(out[i * 2] + left * master, -32768.0f, 32767.0f));
        out[i * 2 + 1] = static_cast<s16>(std::clamp(out[i * 2 + 1] + right * master, -32768.0f, 32767.0f));
    }
}

} // namespace AudioCore::HLE
