// ff-bgm1x: 게임 소리 목록과 사용자 분류. 설명은 sound_catalog.h
#include <algorithm>
#include <cstdlib>
#include <sstream>

#include <fmt/format.h>

#include "audio_core/hle/sound_catalog.h"
#include "common/file_util.h"
#include "common/logging/log.h"

// ff-bgm1x: 안드로이드 앱(영어 화면)에서는 영어 문구
#ifdef __ANDROID__
#define FF_TEXT(ko, en) en
#else
#define FF_TEXT(ko, en) ko
#endif

namespace AudioCore::HLE {

namespace {

std::string StorageDir() {
#ifdef _WIN32
    return FileUtil::GetExeDirectory() + "/ff-bgm1x-sounds";
#else
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "ff-bgm1x-sounds";
#endif
}

} // namespace

std::string SoundCatalog::EscapeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == ' ') {
            out += "%20";
        } else if (c == '#') {
            out += "%23";
        } else if (c == '%') {
            out += "%25";
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += fmt::format("%{:02X}", static_cast<unsigned char>(c));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

int SoundCatalog::TypeOfKey(const std::string& key) {
    return key.rfind("wave:", 0) == 0 ? Type_Wave : Type_Stream;
}

void SoundCatalog::SetGame(u64 id, const std::string& game_title) {
    std::lock_guard g(lock);
    if (has_game && id == program_id) return;
    has_game = true;
    program_id = id;
    code = fmt::format("{:016X}", id);
    title = game_title;
    user.clear();
    seen.clear();
    seen_index.clear();
    LoadLocked();
    version++;
    UpdateActiveLocked();
}

std::string SoundCatalog::GameCode() const {
    std::lock_guard g(lock);
    return has_game ? code : std::string();
}

std::string SoundCatalog::GameTitle() const {
    std::lock_guard g(lock);
    return title;
}

bool SoundCatalog::HasWaveEntries() const {
    std::lock_guard g(lock);
    for (const auto& [key, e] : user) {
        if (TypeOfKey(key) == Type_Wave) return true;
    }
    return false;
}

void SoundCatalog::UpdateActiveLocked() {
    active.store(collect || !user.empty(), std::memory_order_relaxed);
    user_version.fetch_add(1, std::memory_order_relaxed);
}

int SoundCatalog::NoteSeen(const std::string& key, int type, const std::string& name, int auto_kind,
                           int* volume) {
    std::lock_guard g(lock);
    auto u = user.find(key);
    const int kind = u != user.end() && u->second.kind != Kind_Auto ? u->second.kind : auto_kind;
    if (volume) *volume = u != user.end() ? u->second.volume : 100;
    auto it = seen_index.find(key);
    if (it != seen_index.end()) {
        Seen& s = seen[it->second];
        s.count++;
        s.auto_kind = auto_kind;
        if (!name.empty()) s.name = name;
        version++;
    } else if (collect) {
        Seen s;
        s.key = key;
        s.type = type;
        s.name = name;
        s.auto_kind = auto_kind;
        s.user_kind = u != user.end() ? u->second.kind : Kind_Auto;
        s.volume = u != user.end() ? u->second.volume : 100;
        s.count = 1;
        s.order = static_cast<u32>(seen.size());
        seen_index.emplace(key, seen.size());
        seen.push_back(std::move(s));
        version++;
    }
    return kind;
}

void SoundCatalog::Lookup(const std::string& key, int* kind, int* volume) const {
    std::lock_guard g(lock);
    auto u = user.find(key);
    if (kind) *kind = u != user.end() ? u->second.kind : Kind_Auto;
    if (volume) *volume = u != user.end() ? u->second.volume : 100;
}

void SoundCatalog::SetCollecting(bool on) {
    std::lock_guard g(lock);
    collect = on;
    UpdateActiveLocked();
}

bool SoundCatalog::Collecting() const {
    std::lock_guard g(lock);
    return collect;
}

std::vector<SoundCatalog::Seen> SoundCatalog::SeenList() const {
    std::lock_guard g(lock);
    return seen;
}

u32 SoundCatalog::SeenVersion() const {
    std::lock_guard g(lock);
    return version;
}

int SoundCatalog::UserKind(const std::string& key) const {
    std::lock_guard g(lock);
    auto it = user.find(key);
    return it == user.end() ? Kind_Auto : it->second.kind;
}

int SoundCatalog::Volume(const std::string& key) const {
    std::lock_guard g(lock);
    auto it = user.find(key);
    return it == user.end() ? 100 : it->second.volume;
}

// 사용자 지정을 바꾼 뒤: 기본값(자동, 100%)만 남으면 지우고, 목록 줄에도 반영한다. 잠근 채로 부른다
void SoundCatalog::UpdateUserLocked(const std::string& key, UserEntry e) {
    auto it = seen_index.find(key);
    if (it != seen_index.end() && e.name.empty()) e.name = seen[it->second].name;
    if (e.kind == Kind_Auto && e.volume == 100) {
        user.erase(key);
    } else {
        user[key] = e;
    }
    if (it != seen_index.end()) {
        seen[it->second].user_kind = e.kind;
        seen[it->second].volume = e.volume;
    }
    version++;
    UpdateActiveLocked();
}

void SoundCatalog::SetUserKind(const std::string& key, int kind) {
    {
        std::lock_guard g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.kind = kind;
        UpdateUserLocked(key, e);
    }
    Save();
}

void SoundCatalog::SetVolume(const std::string& key, int percent) {
    {
        std::lock_guard g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.volume = std::clamp(percent, 0, kMaxVolume);
        UpdateUserLocked(key, e);
    }
    Save();
}

void SoundCatalog::ResetUser(const std::string& key) {
    {
        std::lock_guard g(lock);
        UpdateUserLocked(key, UserEntry());
    }
    Save();
}

void SoundCatalog::ClearSeen() {
    std::lock_guard g(lock);
    seen.clear();
    seen_index.clear();
    AddUserEntriesLocked();
    version++;
}

// 사용자 지정이 있는데 목록에 없는 소리를 목록에 넣는다 (게임을 다시 켜도 지정한 소리가 보이게)
void SoundCatalog::AddUserEntriesLocked() {
    for (const auto& [key, e] : user) {
        if (seen_index.count(key)) continue;
        Seen s;
        s.key = key;
        s.type = TypeOfKey(key);
        s.name = e.name.empty() ? key : e.name;
        s.auto_kind = Kind_Unknown;
        s.user_kind = e.kind;
        s.volume = e.volume;
        s.count = 0;
        s.order = static_cast<u32>(seen.size());
        seen_index.emplace(key, seen.size());
        seen.push_back(std::move(s));
    }
}

std::string SoundCatalog::StoragePath() const {
    std::lock_guard g(lock);
    if (!has_game) return {};
    return StorageDir() + "/" + code + ".txt";
}

const char* SoundCatalog::KindName(int kind) {
    switch (kind) {
    case Kind_Unknown:
        return "아직 안 나옴";
    case Kind_Auto:
        return "자동";
    case Kind_None:
        return "아무것도 아님";
    case Kind_Bgm:
        return "BGM";
    case Kind_Jingle:
        return "팡파레·대사";
    case Kind_Cutscene:
        return "연출";
    case Kind_Sfx:
        return "효과음";
    }
    return "?";
}

const char* SoundCatalog::KindTag(int kind) {
    switch (kind) {
    case Kind_None:
        return "none";
    case Kind_Bgm:
        return "bgm";
    case Kind_Jingle:
        return "jingle";
    case Kind_Cutscene:
        return "cutscene";
    case Kind_Sfx:
        return "sfx";
    }
    return "auto";
}

int SoundCatalog::KindFromTag(const std::string& t) {
    if (t == "none") return Kind_None;
    if (t == "bgm") return Kind_Bgm;
    if (t == "jingle") return Kind_Jingle;
    if (t == "cutscene") return Kind_Cutscene;
    if (t == "sfx") return Kind_Sfx;
    return Kind_Auto;
}

// 파일 형식 (UTF-8 텍스트, 줄마다 하나):
//   # 주석
//   game <타이틀 ID>
//   <이름표> <분류> [vol=<볼륨 %>] [# 이름]
//   분류가 auto 면 볼륨만 정한 것
std::string SoundCatalog::FileText() const {
    std::ostringstream o;
    o << "# ff-bgm1x 소리 분류 (" << title << ")\n";
    o << "# 분류: bgm = BGM 1배속, jingle = 팡파레·대사 1배속으로 끝까지, cutscene = 연출(배속 멈춤),\n";
    o << "#       sfx = 효과음 1배속, none = 원본 그대로, auto = 에뮬레이터가 자동으로 판단.\n";
    o << "# vol=<%> : 이 소리의 볼륨 (100 = 그대로, 0~" << kMaxVolume
      << "). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용된다.\n";
    o << "# 여기 없는 소리는 에뮬레이터가 자동으로 판단한다.\n";
    o << "game " << code << "\n";
    for (const auto& [key, e] : user) {
        o << key << " " << KindTag(e.kind);
        if (e.volume != 100) o << " vol=" << e.volume;
        if (!e.name.empty()) o << " # " << e.name;
        o << "\n";
    }
    return o.str();
}

bool SoundCatalog::ParseFile(const std::string& text, std::map<std::string, UserEntry>& out,
                             std::string& error) const {
    std::istringstream in(text);
    std::string line;
    std::string file_code;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t hash = line.find('#');
        std::string body = hash == std::string::npos ? line : line.substr(0, hash);
        std::istringstream ls(body);
        std::string a, b;
        if (!(ls >> a)) continue;
        ls >> b;
        if (a == "game") {
            file_code = b;
            continue;
        }
        if (a.rfind("strm:", 0) != 0 && a.rfind("wave:", 0) != 0) continue;
        UserEntry e;
        e.kind = KindFromTag(b);
        std::string opt;
        while (ls >> opt) {
            if (opt.rfind("vol=", 0) == 0) {
                e.volume = std::clamp(std::atoi(opt.c_str() + 4), 0, kMaxVolume);
            }
        }
        if (e.kind == Kind_Auto && e.volume == 100) continue;
        if (hash != std::string::npos) {
            std::string n = line.substr(hash + 1);
            const std::size_t b0 = n.find_first_not_of(" \t");
            e.name = b0 == std::string::npos ? std::string() : n.substr(b0);
        }
        out[a] = e;
    }
    std::string upper = file_code;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (!upper.empty() && upper != code) {
        error = FF_TEXT(std::string("다른 게임(") + file_code + ")의 목록입니다. 이 게임은 " + code + " 입니다.",
                    std::string("This list is for another game (") + file_code + "). This game is " + code + ".");
        return false;
    }
    return true;
}

bool SoundCatalog::LoadLocked() {
    const std::string path = StorageDir() + "/" + code + ".txt";
    std::string text;
    if (!FileUtil::Exists(path) || !FileUtil::ReadFileToString(true, path, text)) return false;
    std::map<std::string, UserEntry> m;
    std::string err;
    if (!ParseFile(text, m, err)) {
        LOG_WARNING(Audio_DSP, "ff-bgm1x: 소리 분류 파일 {} 을 읽지 못함: {}", path, err);
        return false;
    }
    user = std::move(m);
    AddUserEntriesLocked();
    LOG_INFO(Audio_DSP, "ff-bgm1x: 소리 분류 {}개를 {} 에서 읽음", user.size(), path);
    return true;
}

bool SoundCatalog::Save() const {
    std::string path, text;
    {
        std::lock_guard g(lock);
        if (!has_game) return false;
        path = StorageDir() + "/" + code + ".txt";
        text = FileText();
    }
    FileUtil::CreateFullPath(path);
    return FileUtil::WriteStringToFile(true, path, text) == text.size();
}

bool SoundCatalog::ImportFrom(const std::string& path, std::string& error) {
    std::string text;
    if (!FileUtil::Exists(path) || !FileUtil::ReadFileToString(true, path, text)) {
        error = FF_TEXT("파일을 열 수 없습니다.", "Could not open the file.");
        return false;
    }
    std::map<std::string, UserEntry> m;
    {
        std::lock_guard g(lock);
        if (!ParseFile(text, m, error)) return false;
        // 가져온 목록이 이 게임의 지정을 덮어쓴다 (가져온 파일에 없는 지정은 남긴다)
        for (const auto& [k, e] : m) UpdateUserLocked(k, e);
        AddUserEntriesLocked();
        version++;
    }
    Save();
    return true;
}

bool SoundCatalog::ExportTo(const std::string& path) const {
    std::string text;
    {
        std::lock_guard g(lock);
        text = FileText();
    }
    return FileUtil::WriteStringToFile(true, path, text) == text.size();
}

} // namespace AudioCore::HLE
