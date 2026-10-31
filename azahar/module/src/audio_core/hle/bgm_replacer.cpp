// ff-bgm1x: 배속과 관계없이 스트리밍 BGM(BCSTM DSP-ADPCM, AAC)을 1배속으로 재생한다.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>

#include <fmt/format.h>

#include <neaacdec.h>

#include "audio_core/audio_types.h"
#include "audio_core/hle/bgm_replacer.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/romfs_reader.h"
#include "core/loader/loader.h"

namespace AudioCore::HLE {

namespace {

u16 R16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
u32 R32(const u8* p) {
    return static_cast<u32>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24));
}
u64 R64(const u8* p) {
    return R32(p) | (static_cast<u64>(R32(p + 4)) << 32);
}
u64 Head8(const u8* p) {
    u64 v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

// BCSTM 참조 {u16 type, u16 pad, s32 offset}
s32 RefOffset(const u8* p) {
    return static_cast<s32>(R32(p + 4));
}

} // namespace

BgmReplacer& BgmReplacer::Instance() {
    static BgmReplacer instance;
    return instance;
}

u64 BgmReplacer::CoeffKey(const std::array<s16, 16>& c) {
    u64 h = 1469598103934665603ULL;
    for (s16 v : c) {
        h ^= static_cast<u16>(v);
        h *= 1099511628211ULL;
    }
    return h;
}

bool BgmReplacer::IsEvolutionSong(const StreamFile& f) {
    // 진화 곡(이름에 shinka, 진화 축하 팡파레 me_ 는 제외): 반복 여부와 관계없이 게임 속도로 둔다
    std::string lower = f.name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("shinka") != std::string::npos && lower.rfind("me_", 0) != 0;
}

bool BgmReplacer::IsFanfare(const StreamFile& f) {
    // 반복 없는 곡 중 팡파레: 짧은 곡(12초 이하) 또는 이름이 me_ 로 시작하는 곡.
    // 포켓몬 X·울트라썬에서 팡파레는 10.1초 이하, 연출·이벤트 음악은 12.8초 이상이었다
    if (f.loop || !f.sample_rate) return false;
    if (f.frames <= static_cast<u64>(f.sample_rate) * 12) return true;
    std::string lower = f.name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.rfind("me_", 0) == 0;
}

std::string BgmReplacer::NamePrefix(const std::string& name) {
    // 이름 앞쪽의 영문자 부분 (예: EVT_16_160030_0010 -> evt, me_ok -> me). 없으면 이름 전체
    std::string out;
    for (char c : name) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (out.empty()) {
        out = name;
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    }
    return out;
}

std::size_t BgmReplacer::MarkOverlayPrefix(const std::string& prefix) {
    std::size_t count = 0;
    for (StreamFile& f : files) {
        if (!f.loop && !f.overlay && NamePrefix(f.name) == prefix) {
            f.overlay = true;
            ++count;
        }
    }
    return count;
}

u64 BgmReplacer::FrameHash(const u8* data, u32 size) {
    u64 h = 1469598103934665603ULL ^ size;
    for (u32 i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void BgmReplacer::SetEnabled(bool on) {
    enabled = on;
    LOG_INFO(Audio_DSP, "ff-bgm1x: BGM 1배속 유지 {}", on ? "켜짐" : "꺼짐");
    if (!on) {
        // 음성 연결은 다음 버퍼에서 풀리고, 대체 재생은 바로 멈춘다
        std::lock_guard lock(mutex);
        playbacks.clear();
        bindings = {};
    }
    if (!on) sfx_mirror.Clear();
}

bool BgmReplacer::IsEnabled() const {
    return enabled;
}

void BgmReplacer::SetEmulationRunning(bool running) {
    if (emulation_running.exchange(running) != running) {
        LOG_INFO(Audio_DSP, "ff-bgm1x: 에뮬레이션 {} -> 대체 재생 {}", running ? "재개" : "일시정지",
                 running ? "재개" : "일시정지");
    }
}

void BgmReplacer::Reset() {
    std::lock_guard lock(mutex);
    index_built = false;
    index_ok = false;
    files.clear();
    coeff_index.clear();
    bindings = {};
    playbacks.clear();
    dsp_frames = 0;
    video_until_frame = 0;
    video_file_seen = false;
    {
        std::lock_guard video_lock(video_mutex);
        video_ranges.clear();
        video_ranges_built = false;
    }
    cutscene_until = 0;
    Settings::cutscene_speed_hold = false;
    natural_end_file = -1;
    natural_end_time = 0;
    overlay_prefixes.clear();
    sfx_mirror.Clear();
    natural_end_set_frame = 0;
    aac_index.clear();
    aac_outputs.clear();
    aac_last_file = -1;
    aac_last_frame = 0;
    aac_last_restart = false;
    aac_last_cands.clear();
    aac_pending_loop = {};
}

bool BgmReplacer::BuildIndex() {
    index_built = true;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t aac_count = 0;
    std::vector<std::pair<u64, u64>> archives;  // .bcsar (위치, 크기)
    std::shared_ptr<FileSys::RomFSReader> romfs;
    auto& loader = Core::System::GetInstance().GetAppLoader();
    if (loader.ReadRomFS(romfs) != Loader::ResultStatus::Success || !romfs) {
        LOG_WARNING(Audio_DSP, "ff-bgm1x: RomFS 를 읽지 못해 BGM 대체를 끕니다");
        return false;
    }
    auto read = [&](u64 off, std::size_t len) {
        std::vector<u8> buf(len);
        const std::size_t got = romfs->ReadFile(off, len, buf.data());
        buf.resize(got);
        return buf;
    };

    // level3 헤더 (RomFSReader 의 0 은 level3 시작)
    const auto hdr = read(0, 0x28);
    if (hdr.size() < 0x28 || R32(hdr.data()) != 0x28) {
        LOG_WARNING(Audio_DSP, "ff-bgm1x: RomFS level3 헤더를 찾지 못했습니다");
        return false;
    }
    const u32 dir_meta_off = R32(hdr.data() + 0x0C), dir_meta_size = R32(hdr.data() + 0x10);
    const u32 file_meta_off = R32(hdr.data() + 0x1C), file_meta_size = R32(hdr.data() + 0x20);
    const u32 file_data_off = R32(hdr.data() + 0x24);
    const auto dmeta = read(dir_meta_off, dir_meta_size);
    const auto fmeta = read(file_meta_off, file_meta_size);
    (void)dmeta;

    // 파일 메타데이터를 순서대로 훑는다 (항목 크기: 0x20 + 이름, 4바이트 정렬)
    for (std::size_t o = 0; o + 0x20 <= fmeta.size();) {
        const u64 data_off = R64(fmeta.data() + o + 8);
        const u64 data_size = R64(fmeta.data() + o + 16);
        const u32 name_len = R32(fmeta.data() + o + 0x1C);
        std::string name;
        for (u32 i = 0; i + 1 < name_len && o + 0x20 + i + 1 < fmeta.size(); i += 2) {
            const u16 ch = R16(fmeta.data() + o + 0x20 + i);
            name.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
        }
        o += 0x20 + ((name_len + 3) & ~3u);

        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        const u64 abs = file_data_off + data_off;
        if (lower.size() >= 6 && lower.compare(lower.size() - 6, 6, ".bcsar") == 0) {
            archives.emplace_back(abs, data_size);
            continue;
        }
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".aac") == 0) {
            StreamFile f;
            f.name = name;
            f.offset = abs;
            f.size = data_size;
            f.aac = true;
            IndexAacFile(*romfs, f);
            if (f.frames) {
                files.push_back(std::move(f));
                ++aac_count;
            }
            continue;
        }
        if (lower.size() < 6 || lower.compare(lower.size() - 6, 6, ".bcstm") != 0) {
            continue;
        }
        const auto b = read(abs, 0x1000);
        if (b.size() < 0x100 || std::memcmp(b.data(), "CSTM", 4) != 0) {
            continue;
        }
        StreamFile f;
        f.name = name;
        f.offset = abs;
        f.size = data_size;
        const u16 nsec = R16(b.data() + 0x10);
        u32 info = 0;
        for (u16 i = 0; i < nsec; ++i) {
            const u16 type = R16(b.data() + 0x14 + 12 * i);
            const u32 off = R32(b.data() + 0x18 + 12 * i);
            if (type == 0x4000) info = off;
            if (type == 0x4002) f.data_offset = off;
        }
        if (!info || info + 0x100 > b.size()) {
            continue;
        }
        const u8* base = b.data() + info + 8;
        const u8* si = base + RefOffset(base);
        if (si[0] != 2) {  // DSP-ADPCM 만 처리
            continue;
        }
        f.loop = si[1] != 0;
        f.channels = si[2];
        f.sample_rate = R32(si + 4);
        f.loop_start = R32(si + 8);
        f.frames = R32(si + 12);
        f.loop_end = f.frames;
        f.blocks = R32(si + 16);
        f.block_size = R32(si + 20);
        f.block_samples = R32(si + 24);
        f.last_block_samples = R32(si + 32);
        f.last_block_padded = R32(si + 36);
        const u8* ct = base + RefOffset(base + 16);
        const u32 nch = R32(ct);
        if (nch != f.channels || nch == 0 || nch > 8) {
            continue;
        }
        for (u32 c = 0; c < nch; ++c) {
            const u8* cinfo = ct + RefOffset(ct + 4 + 8 * c);
            const u8* a = cinfo + RefOffset(cinfo);
            std::array<s16, 16> co;
            for (int k = 0; k < 16; ++k) co[k] = static_cast<s16>(R16(a + 2 * k));
            f.coeffs.push_back(co);
            f.history.push_back({static_cast<s16>(R16(a + 34)), static_cast<s16>(R16(a + 36))});
        }
        const int id = static_cast<int>(files.size());
        for (u32 c = 0; c < nch; ++c) {
            coeff_index.emplace(CoeffKey(f.coeffs[c]), std::make_pair(id, c));
        }
        files.push_back(std::move(f));
    }
    if (aac_count) {
        for (const auto& [off, size] : archives) ReadArchiveLoops(*romfs, off, size);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    const auto known = std::count_if(files.begin(), files.end(),
                                     [](const StreamFile& f) { return f.aac && f.loop_known; });
    LOG_INFO(Audio_DSP,
             "ff-bgm1x: BCSTM {} 개, AAC {} 개(프레임 {}개, 사운드 아카이브에서 반복 정보 {}개)를 "
             "색인했습니다 ({}ms)",
             files.size() - aac_count, aac_count, aac_index.size(), known, ms);
    LoadSongSettings();
    return !files.empty();
}

void BgmReplacer::LoadSongSettings() {
    u64 program_id = 0;
    Core::System::GetInstance().GetAppLoader().ReadProgramId(program_id);
    const std::string name = fmt::format("{:016X}.txt", program_id);
#ifdef _WIN32
    LoadSongSettingsFile(FileUtil::GetExeDirectory() + "/ff-bgm1x/" + name);
#endif
    settings_user_path = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "ff-bgm1x/" + name;
    LoadSongSettingsFile(settings_user_path);
}

void BgmReplacer::LoadSongSettingsFile(const std::string& path) {
    std::string text;
    if (!FileUtil::Exists(path) || !FileUtil::ReadFileToString(true, path, text)) return;
    auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        return v;
    };
    auto trim = [](std::string v) {
        const auto b = v.find_first_not_of(" \t\r\n");
        const auto e = v.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string() : v.substr(b, e - b + 1);
    };
    // 한 줄에 "chained <파일 이름>". # 으로 시작하는 줄은 주석
    std::size_t count = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        const auto space = line.find(' ');
        if (line.empty() || line[0] == '#' || space == std::string::npos) continue;
        const std::string kind = line.substr(0, space);
        if (kind == "overlay" || kind == "native") {
            const std::string prefix = lower(trim(line.substr(space + 1)));
            count += MarkOverlayPrefix(prefix);
            overlay_prefixes.insert(prefix);
            if (kind == "native") {
                for (StreamFile& f : files) {
                    if (f.overlay && NamePrefix(f.name) == prefix) f.native = true;
                }
            }
            continue;
        }
        if (kind != "chained") continue;
        const std::string song = lower(trim(line.substr(space + 1)));
        for (StreamFile& f : files) {
            if (!f.loop && !f.chained && lower(f.name) == song) {
                f.chained = true;
                ++count;
            }
        }
    }
    LOG_INFO(Audio_DSP, "ff-bgm1x: 설정 파일 {} 에서 곡 설정 {}개를 읽음", path, count);
}

void BgmReplacer::SaveChainedSong(const std::string& name) {
    SaveSongSetting("chained", name);
}

void BgmReplacer::SaveSongSetting(const std::string& kind, const std::string& name) {
    if (settings_user_path.empty()) return;
    const bool fresh = !FileUtil::Exists(settings_user_path);
    FileUtil::CreateFullPath(settings_user_path);
    FileUtil::IOFile file(settings_user_path, "ab");
    if (!file.IsOpen()) {
        LOG_WARNING(Audio_DSP, "ff-bgm1x: 설정 파일 {} 에 쓰지 못함", settings_user_path);
        return;
    }
    if (fresh) {
        file.WriteString("# ff-bgm1x: 이 게임에서 배운 곡 설정 (한 줄에 하나)\n"
                         "# chained <파일 이름> = 게임이 조각을 이어 붙여 재생하는 반복 없는 곡."
                         " 배속 중에도 1배속으로 재생한다\n"
                         "# overlay <이름 접두어> = 게임이 BGM 위에 겹쳐 트는 대사·효과음. 1배속으로 내되 BGM 을 멈추지 않는다\n"
                         "# native <이름 접두어> = 겹쳐 트는 소리 중 게임 속도로 둘 것 (직접 적는다)\n"
                         "# 줄을 지워도 게임이 같은 방식으로 재생하면 다시 기록된다\n");
    }
    file.WriteString(kind + " " + name + "\n");
}

void BgmReplacer::ReadArchiveLoops(FileSys::RomFSReader& romfs, u64 offset, u64 size) {
    std::vector<u8> head(0x40);
    if (size < head.size() || romfs.ReadFile(offset, head.size(), head.data()) != head.size() ||
        std::memcmp(head.data(), "CSAR", 4) != 0) {
        return;
    }
    // 섹션 표 {u16 id, u16, u32 offset, u32 size}: INFO 섹션만 읽는다
    u32 info_off = 0, info_size = 0;
    const u16 nsec = R16(head.data() + 0x10);
    for (u16 i = 0; i < nsec && 0x20 + 12 * i <= head.size(); ++i) {
        if (R16(head.data() + 0x14 + 12 * i) == 0x2001) {
            info_off = R32(head.data() + 0x18 + 12 * i);
            info_size = R32(head.data() + 0x1C + 12 * i);
        }
    }
    if (!info_off || info_size < 0x48 || info_off + static_cast<u64>(info_size) > size) return;
    std::vector<u8> info(info_size);
    if (romfs.ReadFile(offset + info_off, info.size(), info.data()) != info.size()) return;
    const u8* d = info.data();
    const std::size_t n = info.size();
    auto ok = [&](std::size_t o, std::size_t len) { return o + len <= n; };
    auto ref = [&](std::size_t o) { return static_cast<s32>(R32(d + o + 4)); };

    // INFO 본문: 참조 {u16 type, u16, s32 offset} 들 (본문 시작 기준)
    const std::size_t body = 8;
    std::size_t sound_table = 0, file_table = 0;
    for (std::size_t i = 0; i < 8 && ok(body + 8 * i, 8); ++i) {
        const u16 type = R16(d + body + 8 * i);
        if (type == 0x2100) sound_table = body + ref(body + 8 * i);
        if (type == 0x2106) file_table = body + ref(body + 8 * i);
    }
    if (!sound_table || !file_table || !ok(sound_table, 4) || !ok(file_table, 4)) return;

    // 파일 정보 표: 외부 파일(0x220D)이면 경로 문자열
    std::unordered_map<u32, std::string> external;
    const u32 nfiles = R32(d + file_table);
    for (u32 i = 0; i < nfiles && ok(file_table + 4 + 8 * i, 8); ++i) {
        const std::size_t fi = file_table + ref(file_table + 4 + 8 * i);
        if (!ok(fi, 8) || R16(d + fi) != 0x220D) continue;
        const std::size_t s = fi + ref(fi);
        std::string path;
        for (std::size_t k = s; k < n && d[k]; ++k) path.push_back(static_cast<char>(d[k]));
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        const auto slash = path.find_last_of("/\\");
        external[i] = slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // 사운드 정보 표: {u32 파일, u32 플레이어, u32 볼륨, 참조 상세}. 상세가 스트림(0x2201)이면
    // {u16 트랙, u16 채널, 참조(0x0101) 트랙 표, ...}. 트랙 표 참조가 0x1C 이면
    // 그 앞에 {u32, u32, u32 반복 시작, u32 반복 끝} (샘플) 이 있고, 0x14 면 반복이 없다
    std::unordered_map<std::string, std::pair<u32, u32>> loops;  // 반복 없으면 {0, 0}
    const u32 nsounds = R32(d + sound_table);
    for (u32 i = 0; i < nsounds && ok(sound_table + 4 + 8 * i, 8); ++i) {
        const std::size_t si = sound_table + ref(sound_table + 4 + 8 * i);
        if (!ok(si, 0x14) || R16(d + si + 0xC) != 0x2201) continue;
        const auto it = external.find(R32(d + si));
        if (it == external.end()) continue;
        const std::size_t x = si + static_cast<s32>(R32(d + si + 0x10));
        if (!ok(x, 0x1C) || R16(d + x + 4) != 0x0101) continue;
        const u32 tracks_at = R32(d + x + 8);
        if (tracks_at == 0x1C) {
            loops[it->second] = {R32(d + x + 0x14), R32(d + x + 0x18)};
        } else if (tracks_at == 0x14) {
            loops.emplace(it->second, std::make_pair(0u, 0u));
        }
    }

    std::lock_guard lock(mutex);
    for (StreamFile& f : files) {
        if (!f.aac) continue;
        std::string lower = f.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        const auto it = loops.find(lower);
        if (it == loops.end()) continue;
        const auto [start, end] = it->second;
        f.loop_known = true;
        f.loop = end > start && end <= f.frames;
        if (f.loop) {
            f.loop_start = start;
            f.loop_end = end;
        }
    }
}

void BgmReplacer::IndexAacFile(FileSys::RomFSReader& romfs, StreamFile& f) {
    static constexpr u32 kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                     22050, 16000, 12000, 11025, 8000,  7350};
    std::vector<u8> raw(static_cast<std::size_t>(f.size));
    raw.resize(romfs.ReadFile(f.offset, raw.size(), raw.data()));
    const int id = static_cast<int>(files.size());
    std::size_t p = 0;
    u32 n = 0;
    while (p + 7 <= raw.size() && raw[p] == 0xFF && (raw[p + 1] & 0xF0) == 0xF0) {
        const u32 len = ((raw[p + 3] & 3u) << 11) | (raw[p + 4] << 3) | (raw[p + 5] >> 5);
        if (len < 7 || p + len > raw.size()) break;
        if (n == 0) {
            const u32 sf = (raw[p + 2] >> 2) & 0xF;
            f.sample_rate = sf < 13 ? kRates[sf] : 0;
            f.channels = ((raw[p + 2] & 1u) << 2) | (raw[p + 3] >> 6);
        }
        f.aac_frame_offsets.push_back(static_cast<u32>(p));
        aac_index.emplace(FrameHash(raw.data() + p, len), std::make_pair(id, n));
        p += len;
        ++n;
    }
    if (n == 0 || !f.sample_rate || f.channels == 0 || f.channels > 2) {
        f.frames = 0;
        return;
    }
    f.aac_frame_offsets.push_back(static_cast<u32>(p));
    f.block_samples = 1024;
    f.frames = n * 1024;
    f.loop_end = f.frames;
}

void BgmReplacer::LoadBlockHeads(StreamFile& f) {
    if (f.heads_loaded) return;
    f.heads_loaded = true;
    std::shared_ptr<FileSys::RomFSReader> romfs;
    if (Core::System::GetInstance().GetAppLoader().ReadRomFS(romfs) != Loader::ResultStatus::Success) {
        return;
    }
    f.block_heads.assign(f.channels, {});
    const u64 data = f.offset + f.data_offset + 0x20;
    for (u32 k = 0; k < f.blocks; ++k) {
        for (u32 c = 0; c < f.channels; ++c) {
            const u64 off = data + static_cast<u64>(k) * f.block_size * f.channels +
                            static_cast<u64>(c) * (k + 1 == f.blocks ? f.last_block_padded : f.block_size);
            u8 head[8] = {};
            romfs->ReadFile(off, 8, head);
            f.block_heads[c].emplace(Head8(head), k);
        }
    }
}

bool BgmReplacer::StartPlayback(int file, u32 block) {
    StreamFile& f = files[file];
    std::shared_ptr<FileSys::RomFSReader> romfs;
    if (Core::System::GetInstance().GetAppLoader().ReadRomFS(romfs) != Loader::ResultStatus::Success) {
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<u8> raw(static_cast<std::size_t>(f.size));
    romfs->ReadFile(f.offset, raw.size(), raw.data());

    Playback p;
    p.file = file;
    p.pcm.assign(f.channels, std::vector<s16>(f.frames));
    const std::size_t data = f.data_offset + 0x20;
    if (f.aac && !DecodeAac(f, raw, p)) {
        LOG_WARNING(Audio_DSP, "ff-bgm1x: {} 을 해독하지 못해 원본 소리를 둡니다", f.name);
        return false;
    }
    for (u32 c = 0; c < f.channels && !f.aac; ++c) {
        s32 h1 = f.history[c][0], h2 = f.history[c][1];
        u32 out = 0;
        for (u32 k = 0; k < f.blocks && out < f.frames; ++k) {
            const bool last = k + 1 == f.blocks;
            const std::size_t off = data + static_cast<std::size_t>(k) * f.block_size * f.channels +
                                    static_cast<std::size_t>(c) * (last ? f.last_block_padded : f.block_size);
            const u32 nsamp = last ? f.last_block_samples : f.block_samples;
            for (u32 s = 0; s < nsamp && out < f.frames;) {
                const std::size_t fo = off + (s / 14) * 8;
                if (fo + 8 > raw.size()) break;
                const u8 ps = raw[fo];
                const s32 scale = 1 << (ps & 0xF);
                const u32 pred = (ps >> 4) & 7;
                const s32 c1 = f.coeffs[c][pred * 2], c2 = f.coeffs[c][pred * 2 + 1];
                for (u32 n = 0; n < 14 && s < nsamp && out < f.frames; ++n, ++s) {
                    const u8 byte = raw[fo + 1 + n / 2];
                    s32 nib = (n & 1) ? (byte & 0xF) : (byte >> 4);
                    if (nib >= 8) nib -= 16;
                    s32 v = (((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11;
                    v = std::clamp(v, -32768, 32767);
                    h2 = h1;
                    h1 = v;
                    p.pcm[c][out++] = static_cast<s16>(v);
                }
            }
        }
    }
    p.position = static_cast<double>(block) * f.block_samples;
    p.start_frame = dsp_frames;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();

    std::lock_guard lock(mutex);
    playbacks[file] = std::move(p);
    LOG_INFO(Audio_DSP, "ff-bgm1x: {} 을 {} {} 부터 1배속 재생 (해독 {}ms)", f.name,
             f.aac ? "프레임" : "블록", block, ms);
    return true;
}

bool BgmReplacer::DecodeAac(const StreamFile& f, const std::vector<u8>& raw, Playback& p) {
    NeAACDecHandle dec = NeAACDecOpen();
    if (!dec) return false;
    auto config = NeAACDecGetCurrentConfiguration(dec);
    config->defObjectType = LC;
    config->outputFormat = FAAD_FMT_16BIT;
    NeAACDecSetConfiguration(dec, config);
    const u32 nframes = static_cast<u32>(f.aac_frame_offsets.size() - 1);
    if (nframes == 0 || f.aac_frame_offsets.back() > raw.size()) {
        NeAACDecClose(dec);
        return false;
    }
    unsigned long rate = 0;
    u8 ch = 0;
    if (NeAACDecInit(dec, const_cast<u8*>(raw.data()), f.aac_frame_offsets[1], &rate, &ch) < 0) {
        NeAACDecClose(dec);
        return false;
    }
    // 프레임 k 의 출력은 k*1024 위치에 둔다 (게임 디코더와 같은 정렬)
    for (u32 k = 0; k < nframes; ++k) {
        const u32 off = f.aac_frame_offsets[k];
        const u32 len = f.aac_frame_offsets[k + 1] - off;
        NeAACDecFrameInfo info{};
        auto* out = static_cast<s16*>(
            NeAACDecDecode(dec, &info, const_cast<u8*>(raw.data()) + off, len));
        if (!out || info.error != 0 || info.channels == 0) continue;
        const u32 n = std::min<u32>(static_cast<u32>(info.samples / info.channels), 1024);
        for (u32 c = 0; c < f.channels; ++c) {
            const u32 src_ch = std::min<u32>(c, info.channels - 1);
            for (u32 s = 0; s < n; ++s) {
                p.pcm[c][k * 1024 + s] = out[s * info.channels + src_ch];
            }
        }
    }
    NeAACDecClose(dec);
    return true;
}

bool BgmReplacer::FastForwarding() {
    const double limit = Settings::GetFrameLimit();
    return limit == 0 || limit > 100;
}

void BgmReplacer::OnDspFrame() {
    ++dsp_frames;
    // 연출 음악이 나오는 동안에는 배속을 멈춘다 (메뉴 "연출 중에도 배속" 이 꺼져 있을 때)
    const double now = static_cast<double>(dsp_frames) * samples_per_frame / native_sample_rate;
    const bool video = dsp_frames < video_until_frame;
    if (!video) video_file_seen = false;
    const bool hold = enabled && !ff_cutscenes && (now < cutscene_until || video);
    if (Settings::cutscene_speed_hold.exchange(hold) != hold) {
        LOG_INFO(Audio_DSP, "ff-bgm1x: {}", hold ? "연출 시작 -> 배속을 잠시 멈춤" : "연출 끝 -> 배속 복귀");
    }
}

void BgmReplacer::MarkVideo(u64 hold_frames) {
    const u64 until = dsp_frames + hold_frames;
    u64 current = video_until_frame;
    while (current < until && !video_until_frame.compare_exchange_weak(current, until)) {
    }
    // DSP 가 다음 프레임을 만들기 전이라도 바로 배속을 멈춘다
    if (enabled && !ff_cutscenes) Settings::cutscene_speed_hold = true;
}

void BgmReplacer::OnVideoFrame() {
    // Y2R 은 카메라 등에서도 쓰이므로, 동영상 파일을 읽은 뒤에만 동영상으로 본다
    if (!enabled || !video_file_seen) return;
    MarkVideo(205);  // 게임 시간 약 1초
}

void BgmReplacer::OnRomFsRead(FileSys::RomFSReader& reader, u64 offset, u64 length) {
    if (!enabled || video_scanning) return;
    std::lock_guard lock(video_mutex);
    if (!video_ranges_built) {
        video_scanning = true;
        BuildVideoRanges(reader);
        video_scanning = false;
        video_ranges_built = true;
    }
    for (const VideoRange& v : video_ranges) {
        if (offset >= v.end || offset + length <= v.start) continue;
        if (dsp_frames >= video_until_frame) {
            LOG_INFO(Audio_DSP, "ff-bgm1x: 동영상 파일 {} 읽기 시작 -> 연출로 봄", v.name);
        }
        video_file_seen = true;
        MarkVideo(614);  // 게임 시간 약 3초 (파일을 미리 읽어 두는 분량을 감안)
        break;
    }
}

void BgmReplacer::BuildVideoRanges(FileSys::RomFSReader& reader) {
    auto read = [&](u64 off, std::size_t len) {
        std::vector<u8> buf(len);
        buf.resize(reader.ReadFile(off, len, buf.data()));
        return buf;
    };
    // level3 헤더와 파일 메타데이터 (BuildIndex 와 같은 형식)
    const auto hdr = read(0, 0x28);
    if (hdr.size() < 0x28 || R32(hdr.data()) != 0x28) return;
    const u32 file_meta_off = R32(hdr.data() + 0x1C), file_meta_size = R32(hdr.data() + 0x20);
    const u32 file_data_off = R32(hdr.data() + 0x24);
    const auto fmeta = read(file_meta_off, file_meta_size);
    for (std::size_t o = 0; o + 0x20 <= fmeta.size();) {
        const u64 data_off = R64(fmeta.data() + o + 8);
        const u64 data_size = R64(fmeta.data() + o + 16);
        const u32 name_len = R32(fmeta.data() + o + 0x1C);
        std::string name;
        for (u32 i = 0; i + 1 < name_len && o + 0x20 + i + 1 < fmeta.size(); i += 2) {
            const u16 ch = R16(fmeta.data() + o + 0x20 + i);
            name.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
        }
        o += 0x20 + ((name_len + 3) & ~3u);
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() >= 7 && lower.compare(lower.size() - 7, 7, ".moflex") == 0) {
            video_ranges.push_back(
                {file_data_off + data_off, file_data_off + data_off + data_size, name});
        }
    }
    LOG_INFO(Audio_DSP, "ff-bgm1x: 동영상 파일(.moflex) {}개를 찾음", video_ranges.size());
}

void BgmReplacer::SetVoices1x(bool on) {
    voices_1x = on;
    LOG_INFO(Audio_DSP, "ff-bgm1x: 대사·효과음 1배속 {}", on ? "켜짐" : "꺼짐 (게임 속도)");
    if (!on) sfx_mirror.Clear();
}

void BgmReplacer::SetSfxVolume(float volume) {
    sfx_volume = std::clamp(volume, 0.0f, 1.0f);
}

void BgmReplacer::SetFastForwardCutscenes(bool on) {
    ff_cutscenes = on;
    LOG_INFO(Audio_DSP, "ff-bgm1x: 연출 중에도 배속 {}", on ? "켜짐" : "꺼짐 (연출은 1배속)");
}

void BgmReplacer::StopUnboundPlaybacks() {
    std::lock_guard lock(mutex);
    for (auto it = playbacks.begin(); it != playbacks.end();) {
        const bool bound = std::any_of(bindings.begin(), bindings.end(),
                                       [&](const Binding& b) { return b.file == it->first; });
        const StreamFile& f = files[it->first];
        if (!bound && it->second.hold_to_end) {
            ++it;
            continue;
        }
        // 이어 붙이는 곡은 한 번에 하나만 끝까지 재생한다. 다른 곡을 끝까지 재생하는 동안
        // 게임이 지나가 버린 조각은 버린다 (안 그러면 여러 조각이 겹쳐 들린다)
        const bool other_holding =
            std::any_of(playbacks.begin(), playbacks.end(), [&](const auto& o) {
                return o.first != it->first && o.second.hold_to_end && !o.second.finished;
            });
        const bool to_end = IsFanfare(f) || f.overlay || (f.chained && !other_holding);
        if (!bound && to_end && f.game_reached_end && !it->second.finished) {
            // 게임은 곡을 끝냈지만(배속이면 1배속보다 먼저) 1배속으로 끝까지 마저 재생한다
            it->second.hold_to_end = true;
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 는 {}라 1배속으로 끝까지 재생 ({:.1f}초 / {:.1f}초)",
                     f.name, f.overlay ? "대사·효과음" : f.chained ? "이어 붙이는 곡" : "팡파레",
                     it->second.position / f.sample_rate,
                     static_cast<double>(f.frames) / f.sample_rate);
            ++it;
            continue;
        }
        if (!bound) {
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 대체 재생 정지 (게임이 곡을 멈춤/바꿈, 1배속 위치 {:.1f}초)",
                     files[it->first].name,
                     it->second.position / std::max<u32>(1, files[it->first].sample_rate));
            // 곧바로 다른 곡이 같은 박자 위치에서 이어질 수 있으므로 (예: ORAS 인트로의 GBA풍 -> 3DS풍)
            // 멈춘 곡의 1배속 위치와 게임 위치를 기억해 둔다
            const double rate = std::max<u32>(1, f.sample_rate);
            last_stop = {it->first, it->second.position / rate,
                         it->second.game_pos >= 0 ? it->second.game_pos / rate : -1.0, dsp_frames.load()};
        }
        it = bound ? std::next(it) : playbacks.erase(it);
    }
}

bool BgmReplacer::OnAdpcmBuffer(std::size_t source_id, const std::array<s16, 16>& coeffs,
                                const u8* data, u32 samples) {
    if (source_id >= bindings.size() || !data || !enabled) return false;
    if (!index_built) index_ok = BuildIndex();
    if (!index_ok) return false;

    const u64 key = CoeffKey(coeffs);
    const u64 head = Head8(data);
    int match_file = -1;
    u32 match_channel = 0, match_block = 0;
    auto range = coeff_index.equal_range(key);
    for (auto it = range.first; it != range.second && match_file < 0; ++it) {
        StreamFile& f = files[it->second.first];
        LoadBlockHeads(f);
        if (it->second.second >= f.block_heads.size()) continue;
        auto& heads = f.block_heads[it->second.second];
        auto h = heads.find(head);
        if (h != heads.end()) {
            match_file = it->second.first;
            match_channel = it->second.second;
            match_block = h->second;
        }
    }
    if (match_file >= 0) {
        // 이어 붙이는 곡 감지: 반복 없는 곡이 끝날 게임 시각 전후로 다른 스트림이 처음부터 시작되면
        // 게임이 조각을 이어 붙여 한 곡처럼 재생하는 것으로 본다 (예: 동물의 숲 상점가)
        static constexpr double kChainWindowSec = 0.75;
        StreamFile& mf = files[match_file];
        const double now = static_cast<double>(dsp_frames) * samples_per_frame / native_sample_rate;
        if (match_block == 0 && natural_end_file >= 0 && natural_end_set_frame != dsp_frames) {
            StreamFile& prev = files[natural_end_file];
            const double gap = now - natural_end_time;
            const bool chain = std::abs(gap) <= kChainWindowSec;
            if (std::abs(gap) < 10.0) {
                LOG_INFO(Audio_DSP, "ff-bgm1x: [이어붙임] {} 끝 -> {} 시작, 게임 시간 간격 {:.3f}초 ({})",
                         prev.name, mf.name, gap, chain ? "이어 붙임" : "별개");
            }
            if (chain) {
                std::vector<std::string> learned;
                {
                    std::lock_guard lock(mutex);
                    for (StreamFile* piece : {&prev, &mf}) {
                        if (piece->loop || piece->chained || IsFanfare(*piece) ||
                            IsEvolutionSong(*piece)) {
                            continue;
                        }
                        piece->chained = true;
                        learned.push_back(piece->name);
                    }
                }
                for (const std::string& song : learned) {
                    LOG_INFO(Audio_DSP, "ff-bgm1x: {} 를 이어 붙이는 곡으로 보고 이제부터 1배속 처리 (설정 파일에 저장)",
                             song);
                    SaveChainedSong(song);
                }
            }
            natural_end_file = -1;
        }
        if (!mf.loop && match_block + 1 == mf.blocks && mf.sample_rate) {
            natural_end_file = match_file;
            natural_end_time = now + static_cast<double>(mf.last_block_samples) / mf.sample_rate;
            natural_end_set_frame = dsp_frames;
        }
    }

    if (match_file >= 0 && files[match_file].loop) {
        // 반복 곡(BGM)의 새 버퍼가 왔는데, 그보다 나중에 시작한 반복 없는 짧은 곡을 게임이 아직
        // 재생하고 있으면(끝 블록을 보내기 전이고 음성에 연결돼 있음) 게임은 BGM 을 멈추지 않고
        // 그 소리를 겹쳐 트는 것이다: 팡파레가 아니라 대사·효과음으로 본다
        std::vector<std::string> learned;
        {
            std::lock_guard lock(mutex);
            auto bgm = playbacks.find(match_file);
            for (auto it = playbacks.begin(); bgm != playbacks.end() && it != playbacks.end();) {
                StreamFile& q = files[it->first];
                const bool bound = std::any_of(bindings.begin(), bindings.end(),
                                               [&](const Binding& x) { return x.file == it->first; });
                if (it->first == match_file || q.loop || q.chained || !IsFanfare(q) || q.overlay ||
                    !bound || it->second.hold_to_end || it->second.finished || q.game_reached_end ||
                    it->second.start_frame <= bgm->second.start_frame ||
                    dsp_frames < it->second.start_frame + 2) {
                    ++it;
                    continue;
                }
                learned.push_back(NamePrefix(q.name));
                ++it;
            }
            for (auto& [id, p] : playbacks) p.waiting = false;
        }
        for (const std::string& prefix : learned) {
            if (overlay_prefixes.count(prefix)) continue;
            overlay_prefixes.insert(prefix);
            std::size_t n;
            {
                std::lock_guard lock(mutex);
                n = MarkOverlayPrefix(prefix);
            }
            LOG_INFO(Audio_DSP, "ff-bgm1x: 게임이 BGM 위에 겹쳐 트는 소리 -> 이름이 {} 로 시작하는 곡 {}개를 "
                     "대사·효과음으로 봄: 1배속으로 내되 BGM 을 멈추지 않음 (설정 파일에 저장)", prefix, n);
            SaveSongSetting("overlay", prefix);
        }
    }

    Binding& b = bindings[source_id];
    if (match_file >= 0 && files[match_file].overlay && (!voices_1x || files[match_file].native)) {
        // 대사·효과음 1배속이 꺼져 있거나, 게임 속도로 두기로 한 종류: 게임 소리를 그대로 둔다
        match_file = -1;
    }
    if (match_file >= 0 && files[match_file].overlay && match_block == 0 && b.file != match_file) {
        // 대사·효과음이 새로 시작됨: 같은 종류(이름 접두어)의 앞 대사가 1배속으로 아직 나오고 있으면 끊는다
        // (배속 중 대사를 넘기면 겹쳐 쌓이지 않게)
        const std::string prefix = NamePrefix(files[match_file].name);
        std::lock_guard lock(mutex);
        for (auto it = playbacks.begin(); it != playbacks.end();) {
            if (it->first != match_file && files[it->first].overlay && NamePrefix(files[it->first].name) == prefix) {
                for (Binding& x : bindings) {
                    if (x.file == it->first) x.file = -1;
                }
                it = playbacks.erase(it);
            } else {
                ++it;
            }
        }
    }
    // 반복 없는 곡 중 팡파레도 이어 붙이는 곡도 아닌 곡(연출·이벤트 음악)과 진화 곡은
    // 대체하지 않고 원본처럼 게임 속도로 둔다
    if (match_file >= 0 &&
        ((!files[match_file].loop && !IsFanfare(files[match_file]) && !files[match_file].chained &&
          !files[match_file].overlay) ||
         IsEvolutionSong(files[match_file]))) {
        const StreamFile& sf = files[match_file];
        if (!IsEvolutionSong(sf) && sf.sample_rate) {
            // 연출 음악: 이 블록이 재생되는 동안은 연출 중으로 본다
            const u32 n = match_block + 1 == sf.blocks ? sf.last_block_samples : sf.block_samples;
            const double now = static_cast<double>(dsp_frames) * samples_per_frame / native_sample_rate;
            cutscene_until = std::max(cutscene_until, now + static_cast<double>(n) / sf.sample_rate + 0.15);
        }
        if (!files[match_file].skip_logged) {
            files[match_file].skip_logged = true;
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 는 연출·이벤트·진화 곡이라 대체하지 않음 (게임과 같은 속도)",
                     files[match_file].name);
        }
        match_file = -1;
    }
    if (match_file < 0) {
        if (b.file >= 0) OnSourceIdle(source_id);
        return false;
    }

    bool start = false;
    bool restart = false;
    StreamFile& f = files[match_file];
    {
        std::lock_guard lock(mutex);
        const int previous = b.file;
        b.file = match_file;
        b.channel = match_channel;
        auto pb = playbacks.find(match_file);
        if (previous != match_file) {
            start = pb == playbacks.end();
            // 끝까지 재생 중이거나 이미 끝난 팡파레를 게임이 다시 틀면 게임 위치로 맞춘다
            restart = !start && (pb->second.hold_to_end || pb->second.finished);
        } else if (match_block == 0) {
            // 같은 곡이 처음부터 다시 시작됨 (루프는 루프 지점부터 이어지므로 0 블록은 재시작)
            restart = pb != playbacks.end() && pb->second.position > f.block_samples * 2.0;
        }
        if (match_block == 0) f.game_reached_end = false;
        if (!f.loop && match_block + 1 == f.blocks && !f.game_reached_end) {
            f.game_reached_end = true;
            LOG_INFO(Audio_DSP, "ff-bgm1x: 게임이 {} 의 마지막 블록을 보냄", f.name);
        }
    }
    if (start && match_block == 0 && !f.overlay && !f.chained) {
        // 다른 곡이 처음부터 시작됨 = 장면이 넘어갔다: 1배속으로 끝까지 내던 팡파레를 끊고 새 곡을 바로 낸다
        // (예: ORAS 트럭 장면 곡이 짧아 팡파레로 분류되면, 배속 중 내린 뒤 마을 곡이 그만큼 늦게 나왔다).
        // 멈춰 두었던 곡을 이어 트는 경우(블록 0 이 아님)는 지금처럼 팡파레가 끝날 때까지 기다린다
        std::lock_guard lock(mutex);
        for (auto it = playbacks.begin(); it != playbacks.end();) {
            const StreamFile& q = files[it->first];
            if (it->first != match_file && it->second.hold_to_end && IsFanfare(q) && !q.overlay && !q.chained) {
                LOG_INFO(Audio_DSP, "ff-bgm1x: 새 곡 {} 이 처음부터 시작돼 팡파레 {} 를 끊음", f.name, q.name);
                for (Binding& x : bindings) {
                    if (x.file == it->first) x.file = -1;
                }
                it = playbacks.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (start && StartPlayback(match_file, match_block) && match_block > 0 && !f.overlay && !f.chained &&
        f.sample_rate) {
        // 방금 멈춘 곡과 같은 박자 위치에서 이어지는 곡: 게임 위치가 아니라, 멈춘 곡을 1배속으로 내던 위치에서
        // 이어 간다. 배속 중에는 1배속 위치가 게임보다 뒤처져 있어 게임 위치로 가면 곡이 앞으로 건너뛴다.
        // 1배속에서는 두 위치가 같아 달라지지 않는다
        const double begin = static_cast<double>(match_block) * f.block_samples / f.sample_rate;
        const bool recent = dsp_frames - last_stop.frame <= 205;  // 게임 시간 약 1초
        if (last_stop.file >= 0 && last_stop.file != match_file && recent && last_stop.game_sec >= 0 &&
            std::abs(begin - last_stop.game_sec) < 1.5) {
            const double sec = std::max(0.0, last_stop.our_sec + (begin - last_stop.game_sec));
            std::lock_guard lock(mutex);
            auto pb = playbacks.find(match_file);
            if (pb != playbacks.end() && sec * f.sample_rate < f.frames) {
                pb->second.position = sec * f.sample_rate;
                LOG_INFO(Audio_DSP, "ff-bgm1x: {} 는 방금 멈춘 {} 에 이어지는 곡이라 1배속 위치 {:.1f}초부터 재생 "
                         "(게임 위치 {:.1f}초)", f.name, files[last_stop.file].name, sec, begin);
            }
        }
        last_stop.file = -1;
    } else if (restart) {
        LOG_INFO(Audio_DSP, "ff-bgm1x: {} 블록 {} 부터 다시 시작 (게임이 같은 곡을 다시 틂)", f.name,
                 match_block);
        std::lock_guard lock(mutex);
        playbacks[match_file].position = static_cast<double>(match_block) * f.block_samples;
        playbacks[match_file].finished = false;
    }
    {
        std::lock_guard lock(mutex);
        auto pb = playbacks.find(match_file);
        if (pb != playbacks.end()) {
            pb->second.game_pos = static_cast<double>(match_block) * f.block_samples;
            pb->second.hold_to_end = false;  // 게임이 다시 이 곡을 재생함
        }
    }
    StopUnboundPlaybacks();
    return true;
}

void BgmReplacer::OnAacDecode(const u8* data, u32 size, u32 dst0, u32 dst1) {
    if (!data || !size || !enabled) return;
    if (!index_built) index_ok = BuildIndex();
    if (!index_ok || aac_index.empty()) return;

    // 같은 내용의 프레임(곡 첫머리의 무음 등)이 여러 파일에 있으면 직전 프레임 후보에 이어지는 것을 고른다
    auto was_candidate = [&](int cf, u32 cn) {
        return std::find(aac_last_cands.begin(), aac_last_cands.end(), std::make_pair(cf, cn)) !=
               aac_last_cands.end();
    };
    int file = -1;
    u32 frame = 0;
    int best = -1;
    std::vector<std::pair<int, u32>> cands;
    auto range = aac_index.equal_range(FrameHash(data, size));
    for (auto it = range.first; it != range.second; ++it) {
        const auto [cf, cn] = it->second;
        cands.push_back(it->second);
        int score = 0;
        if (cf == aac_last_file && (cn == aac_last_frame + 1 || cn == aac_last_frame)) {
            score = 2;
        } else if ((cn > 0 && was_candidate(cf, cn - 1)) || was_candidate(cf, cn)) {
            score = 1;
        }
        if (score > best) {
            best = score;
            file = cf;
            frame = cn;
        }
    }
    if (file < 0) {
        aac_outputs.erase(dst0);
        aac_outputs.erase(dst1);
        aac_last_file = -1;
        aac_last_cands.clear();
        aac_pending_loop = {};
        return;
    }
    if (best == 1 && file != aac_last_file && aac_last_file >= 0) {
        // 앞 프레임을 다른 파일로 잘못 골랐었다. 이미 기록한 출력도 바로잡는다
        for (auto& [addr, o] : aac_outputs) {
            if (o.file == aac_last_file) o.file = file;
        }
        LOG_INFO(Audio_DSP, "ff-bgm1x: 곡 첫머리가 여러 파일과 같아 {} 를 {} 로 바로잡음",
                 files[aac_last_file].name, files[file].name);
        aac_last_file = file;
    }
    aac_last_cands = std::move(cands);

    StreamFile& f = files[file];
    const u32 nframes = f.frames / 1024;
    bool restart = false;
    if (file != aac_last_file) {
        restart = true;  // 새 곡
        aac_pending_loop = {};
    } else if (frame == aac_last_frame) {
        // 같은 프레임을 한 번 더 풀면 곡을 새로 시작하는 것 (첫 프레임을 미리 한 번 푼다)
        restart = aac_last_restart || aac_pending_loop.file == file;
        aac_pending_loop = {};
    } else if (frame == aac_last_frame + 1) {
        if (aac_pending_loop.file == file) {
            // 되돌아간 뒤 그대로 이어서 풀면 반복이다. 여기서는 프레임 단위로만 알 수 있고,
            // 샘플 단위 위치는 그 버퍼가 실제로 재생될 때 OnPcmBuffer 에서 고친다
            std::lock_guard lock(mutex);
            const u32 start = aac_pending_loop.start * 1024;
            const u32 end = aac_pending_loop.end * 1024;
            if (!f.loop && !f.loop_known) {
                f.loop = true;
                f.loop_start = start;
                f.loop_end = end;
                LOG_INFO(Audio_DSP, "ff-bgm1x: {} 반복 지점을 배움: 프레임 {} -> {} ({:.2f}초 -> {:.2f}초)",
                         f.name, aac_pending_loop.end, aac_pending_loop.start,
                         end / static_cast<double>(f.sample_rate),
                         start / static_cast<double>(f.sample_rate));
            }
        }
        aac_pending_loop = {};
    } else if (frame < aac_last_frame) {
        // 끝에서 되돌아가거나 중간 프레임으로 되돌아가면 반복 후보, 중간에서 0 으로 가면 재시작
        if (aac_last_frame + 1 == nframes || frame > 0) {
            aac_pending_loop = {file, frame, aac_last_frame + 1};
        } else {
            restart = true;
            aac_pending_loop = {};
        }
    } else {
        restart = true;  // 앞으로 건너뜀
        aac_pending_loop = {};
    }
    aac_last_file = file;
    aac_last_frame = frame;
    aac_last_restart = restart;

    aac_outputs[dst0] = {file, frame, 0, restart};
    if (f.channels > 1) aac_outputs[dst1] = {file, frame, 1, restart};
    {
        std::lock_guard lock(mutex);
        if (restart) f.game_reached_end = false;
        if (frame + 1 == nframes && !f.loop && !f.game_reached_end) {
            f.game_reached_end = true;
            LOG_INFO(Audio_DSP, "ff-bgm1x: 게임이 {} 의 마지막 프레임을 풀었음", f.name);
        }
    }
}

bool BgmReplacer::OnPcmBuffer(std::size_t source_id, u32 address, u32 length, u32 channels) {
    if (source_id >= bindings.size() || channels != 1 || !enabled) return false;
    // 디코더 출력(프레임 하나 = 1024 샘플) 안의 어느 위치든 같은 프레임으로 본다
    auto it = aac_outputs.end();
    for (auto o = aac_outputs.begin(); o != aac_outputs.end(); ++o) {
        if (address >= o->first && address < o->first + 1024 * 2) {
            it = o;
            break;
        }
    }
    if (it == aac_outputs.end()) return false;
    const u32 offset = (address - it->first) / 2;
    const AacOutput out = it->second;
    it->second.restart = false;  // 재시작 표시는 한 번만 쓴다

    StreamFile& f = files[out.file];
    // 사운드 아카이브상 반복 없는 곡 중 팡파레가 아닌 곡(연출·이벤트 음악)과 진화 곡은 원본처럼 게임 속도로 둔다
    if (f.overlay && (!voices_1x || f.native)) {
        if (bindings[source_id].file >= 0) OnSourceIdle(source_id);
        return false;
    }
    if ((f.loop_known && !f.loop && !IsFanfare(f) && !f.overlay) || IsEvolutionSong(f)) {
        if (!IsEvolutionSong(f) && f.sample_rate) {
            // 연출 음악: 이 버퍼가 재생되는 동안은 연출 중으로 본다
            const double now = static_cast<double>(dsp_frames) * samples_per_frame / native_sample_rate;
            cutscene_until = std::max(cutscene_until, now + static_cast<double>(length) / f.sample_rate + 0.15);
        }
        if (!f.skip_logged) {
            f.skip_logged = true;
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 는 연출·이벤트·진화 곡이라 대체하지 않음 (게임과 같은 속도)", f.name);
        }
        if (bindings[source_id].file >= 0) OnSourceIdle(source_id);
        return false;
    }
    const s64 start = static_cast<s64>(out.frame) * 1024 + offset;
    const s64 end = start + std::min<u32>(length, 1024 - offset);

    enum class Action { None, Start, Seek } action = Action::None;
    {
        std::lock_guard lock(mutex);
        Binding& b = bindings[source_id];
        // 같은 곡에서 뒤로 되돌아간 버퍼면 반복: 직전 버퍼 끝 -> 이 버퍼 시작 (샘플 단위)
        if (!f.loop_known && b.file == out.file && !out.restart && b.aac_end > start + 1024 &&
            b.aac_end <= f.frames) {
            const u32 ls = static_cast<u32>(start), le = static_cast<u32>(b.aac_end);
            if (!f.loop || f.loop_start != ls || f.loop_end != le) {
                f.loop = true;
                f.loop_start = ls;
                f.loop_end = le;
                LOG_INFO(Audio_DSP, "ff-bgm1x: {} 반복 지점(샘플 단위): {} -> {} ({:.3f}초 -> {:.3f}초)",
                         f.name, le, ls, le / static_cast<double>(f.sample_rate),
                         ls / static_cast<double>(f.sample_rate));
            }
        }
        b.file = out.file;
        b.channel = out.channel;
        b.aac_end = end;
        auto pb = playbacks.find(out.file);
        if (pb == playbacks.end()) {
            action = Action::Start;
        } else if (out.restart || pb->second.finished) {
            action = Action::Seek;
        }
    }
    if (action == Action::Start) {
        if (StartPlayback(out.file, out.frame)) {
            std::lock_guard lock(mutex);
            playbacks[out.file].position = static_cast<double>(start);
        }
    } else if (action == Action::Seek) {
        std::lock_guard lock(mutex);
        Playback& p = playbacks[out.file];
        if (p.finished || std::abs(p.position - start) > 1024 * 4) {
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 프레임 {} 부터 다시 재생 ({})", f.name, out.frame,
                     p.finished ? "끝난 뒤 게임이 계속 재생" : "게임이 곡을 새로 시작");
        }
        p.position = static_cast<double>(start);
        p.finished = false;
    }
    {
        std::lock_guard lock(mutex);
        auto pb = playbacks.find(out.file);
        if (pb != playbacks.end()) {
            pb->second.game_pos = static_cast<double>(start);
            pb->second.hold_to_end = false;  // 게임이 다시 이 곡을 재생함
        }
    }
    StopUnboundPlaybacks();
    return true;
}

void BgmReplacer::OnSourceIdle(std::size_t source_id) {
    if (source_id >= bindings.size()) return;
    {
        std::lock_guard lock(mutex);
        if (bindings[source_id].file < 0) return;
        bindings[source_id].file = -1;
    }
    StopUnboundPlaybacks();
}

void BgmReplacer::UpdateSourceGain(std::size_t source_id,
                                   const std::array<std::array<float, 4>, 3>& gain) {
    if (source_id >= bindings.size()) return;
    std::lock_guard lock(mutex);
    bindings[source_id].gain = gain;
}

void BgmReplacer::UpdateMixerVolumes(const std::array<float, 3>& volumes) {
    mirror_mixer_volume = volumes;
    std::lock_guard lock(mutex);
    mixer_volume = volumes;
}

bool BgmReplacer::MirrorBegin(std::size_t source_id) {
    if (!enabled || !voices_1x || !FastForwarding()) return false;
    sfx_mirror.Begin(source_id);
    return true;
}

void BgmReplacer::MirrorEnd(std::size_t source_id) {
    sfx_mirror.End(source_id);
}

bool BgmReplacer::MirrorFeed(std::size_t source_id, const StereoFrame16& frame,
                             const std::array<std::array<float, 4>, 3>& gain) {
    return sfx_mirror.Feed(source_id, frame, gain, mirror_mixer_volume);
}

void BgmReplacer::Mix(s16* buffer, std::size_t frames, u32 sample_rate) {
    if (!enabled || !emulation_running) return;
    sfx_mirror.Mix(buffer, frames, sample_rate, sfx_volume);  // 효과음 (게임 출력을 1배속으로)
    std::lock_guard lock(mutex);
    if (sample_rate == 0) return;
    if (playbacks.empty()) return;

    // 끝까지 재생 중인 팡파레가 있으면 다른 곡은 기다렸다가, 끝나면 게임이 재생 중인 위치로 맞춘다
    bool holding = false;
    for (auto it = playbacks.begin(); it != playbacks.end();) {
        if (it->second.hold_to_end && it->second.finished) {
            LOG_INFO(Audio_DSP, "ff-bgm1x: {} 를 끝까지 재생함", files[it->first].name);
            it = playbacks.erase(it);
            continue;
        }
        holding |= it->second.hold_to_end && !files[it->first].overlay;
        ++it;
    }
    for (auto& [file, p] : playbacks) {
        if (holding && !p.hold_to_end) {
            p.waiting = true;
        } else if (p.waiting) {
            p.waiting = false;
            if (p.hold_to_end) continue;  // 기다리던 곡이 끝까지 재생할 곡이 됐으면 제자리에서 시작
            if (p.game_pos >= 0) p.position = p.game_pos;
            LOG_INFO(Audio_DSP, "ff-bgm1x: 끝까지 재생하던 곡이 끝나 {} 를 게임 위치 {:.1f}초부터 이어 재생",
                     files[file].name, p.position / std::max<u32>(1, files[file].sample_rate));
        }
    }

    for (auto& [file, p] : playbacks) {
        if (p.finished || p.waiting) continue;
        const StreamFile& f = files[file];
        // 채널별 좌우 게인: 원본 음성 gain x 믹서 볼륨 (스테레오 다운믹스: 앞+뒤)
        std::vector<float> gl(f.channels, 0.0f), gr(f.channels, 0.0f);
        for (const Binding& b : bindings) {
            if (b.file != file || b.channel >= f.channels) continue;
            float l = 0, r = 0;
            for (int m = 0; m < 3; ++m) {
                l += mixer_volume[m] * (b.gain[m][0] + b.gain[m][2]);
                r += mixer_volume[m] * (b.gain[m][1] + b.gain[m][3]);
            }
            gl[b.channel] = l;
            gr[b.channel] = r;
        }
        // 게임이 놓아 준 뒤 끝까지 재생하는 곡은 마지막 볼륨을 유지한다
        if (p.hold_to_end && !p.last_gl.empty()) {
            gl = p.last_gl;
            gr = p.last_gr;
        } else {
            p.last_gl = gl;
            p.last_gr = gr;
        }
        const double step = static_cast<double>(f.sample_rate) / sample_rate;
        const bool loops = f.loop && f.loop_end > f.loop_start && f.loop_end <= f.frames;
        const u32 end = loops ? f.loop_end : f.frames;
        for (std::size_t i = 0; i < frames; ++i) {
            if (p.position >= end) {
                if (loops) {
                    p.position = f.loop_start + std::fmod(p.position - end,
                                                          static_cast<double>(end - f.loop_start));
                } else {
                    p.finished = true;
                    LOG_INFO(Audio_DSP, "ff-bgm1x: {} 끝까지 재생함 ({})", f.name,
                             f.aac ? "게임이 아직 반복하지 않음" : "반복 없는 곡");
                    break;
                }
            }
            const std::size_t idx = static_cast<std::size_t>(p.position);
            const float frac = static_cast<float>(p.position - idx);
            std::size_t next = idx + 1;
            if (next >= end) next = loops ? f.loop_start : idx;
            float l = 0, r = 0;
            for (u32 c = 0; c < f.channels; ++c) {
                const float s = p.pcm[c][idx] + (p.pcm[c][next] - p.pcm[c][idx]) * frac;
                l += s * gl[c];
                r += s * gr[c];
            }
            const s32 ol = buffer[i * 2] + static_cast<s32>(l);
            const s32 or_ = buffer[i * 2 + 1] + static_cast<s32>(r);
            buffer[i * 2] = static_cast<s16>(std::clamp(ol, -32768, 32767));
            buffer[i * 2 + 1] = static_cast<s16>(std::clamp(or_, -32768, 32767));
            p.position += step;
        }
    }
}

} // namespace AudioCore::HLE
