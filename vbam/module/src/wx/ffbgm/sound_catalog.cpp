// ff-bgm1x: 게임 소리 목록과 사용자 분류. 설명은 sound_catalog.h
#include "sound_catalog.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "winutil.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

bool readText(const std::string& path, std::string& out) {
    FILE* f = fopenUtf8(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    if (out.size() >= 3 && (unsigned char) out[0] == 0xEF && (unsigned char) out[1] == 0xBB && (unsigned char) out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

bool writeText(const std::string& path, const std::string& text) {
    FILE* f = fopenUtf8(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

void makeDir(const std::string& dir) {
#ifdef _WIN32
    _wmkdir(utf8ToWide(dir).c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
}

}  // namespace

std::string SoundCatalog::DefaultStorageDir() {
    return exeDir() + "/ff-bgm1x-sounds";
}

int SoundCatalog::TypeOfKey(const std::string& key) {
    if (key.rfind("song:", 0) == 0 || key.rfind("gbsong:", 0) == 0) return Type_Song;
    if (key.rfind("gbsfx:", 0) == 0) return Type_Sfx;
    if (key.rfind("cry:", 0) == 0) return Type_Cry;
    return Type_Other;
}

void SoundCatalog::SetStorageDir(const std::string& d) {
    std::lock_guard<std::mutex> g(lock);
    dir = d;
}

std::string SoundCatalog::PathLocked() const {
    if (!has_game || dir.empty() || code.empty()) return {};
    return dir + "/" + code + ".txt";
}

void SoundCatalog::SetGame(const std::string& c, const std::string& t) {
    std::lock_guard<std::mutex> g(lock);
    if (has_game && c == code) return;
    has_game = true;
    code = c;
    title = t;
    user.clear();
    seen.clear();
    seen_index.clear();
    std::string text, err;
    const std::string path = PathLocked();
    if (!path.empty() && readText(path, text)) {
        std::map<std::string, UserEntry> m;
        if (ParseFile(text, m, err)) user = std::move(m);
    }
    AddUserEntriesLocked();
    version++;
    UpdateActiveLocked();
}

std::string SoundCatalog::GameCode() const {
    std::lock_guard<std::mutex> g(lock);
    return has_game ? code : std::string();
}

std::string SoundCatalog::GameTitle() const {
    std::lock_guard<std::mutex> g(lock);
    return title;
}

void SoundCatalog::UpdateActiveLocked() {
    active.store(collect || !user.empty(), std::memory_order_relaxed);
    user_version.fetch_add(1, std::memory_order_relaxed);
}

void SoundCatalog::NoteSeen(const std::string& key, int type, const std::string& name, int auto_kind, int* kind,
                            int* volume) {
    std::lock_guard<std::mutex> g(lock);
    auto u = user.find(key);
    if (kind) *kind = u != user.end() ? u->second.kind : Kind_Auto;
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
        s.label = u != user.end() ? u->second.label : std::string();
        s.count = 1;
        s.order = (uint32_t) seen.size();
        seen_index.emplace(key, seen.size());
        seen.push_back(std::move(s));
        version++;
    }
}

void SoundCatalog::Lookup(const std::string& key, int* kind, int* volume) const {
    std::lock_guard<std::mutex> g(lock);
    auto u = user.find(key);
    if (kind) *kind = u != user.end() ? u->second.kind : Kind_Auto;
    if (volume) *volume = u != user.end() ? u->second.volume : 100;
}

void SoundCatalog::SetCollecting(bool on) {
    std::lock_guard<std::mutex> g(lock);
    collect = on;
    UpdateActiveLocked();
}

bool SoundCatalog::Collecting() const {
    std::lock_guard<std::mutex> g(lock);
    return collect;
}

std::vector<SoundCatalog::Seen> SoundCatalog::SeenList() const {
    std::lock_guard<std::mutex> g(lock);
    return seen;
}

uint32_t SoundCatalog::SeenVersion() const {
    std::lock_guard<std::mutex> g(lock);
    return version;
}

int SoundCatalog::UserKind(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock);
    auto it = user.find(key);
    return it == user.end() ? Kind_Auto : it->second.kind;
}

int SoundCatalog::Volume(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock);
    auto it = user.find(key);
    return it == user.end() ? 100 : it->second.volume;
}

void SoundCatalog::UpdateUserLocked(const std::string& key, UserEntry e) {
    auto it = seen_index.find(key);
    if (it != seen_index.end() && e.name.empty()) e.name = seen[it->second].name;
    if (e.empty()) {
        user.erase(key);
    } else {
        user[key] = e;
    }
    if (it != seen_index.end()) {
        seen[it->second].user_kind = e.kind;
        seen[it->second].volume = e.volume;
        seen[it->second].label = e.label;
    }
    version++;
    UpdateActiveLocked();
}

void SoundCatalog::SetUserKind(const std::string& key, int kind) {
    {
        std::lock_guard<std::mutex> g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.kind = kind;
        UpdateUserLocked(key, e);
    }
    Save();
}

void SoundCatalog::SetVolume(const std::string& key, int percent) {
    {
        std::lock_guard<std::mutex> g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.volume = std::clamp(percent, 0, kMaxVolume);
        UpdateUserLocked(key, e);
    }
    Save();
}

void SoundCatalog::ResetUser(const std::string& key) {
    {
        std::lock_guard<std::mutex> g(lock);
        auto it = user.find(key);
        UserEntry e;
        if (it != user.end()) e.label = it->second.label;
        UpdateUserLocked(key, e);
    }
    Save();
}

void SoundCatalog::SetLabel(const std::string& key, const std::string& label) {
    std::string l = label;
    // 파일 한 줄에 들어가야 하므로 줄바꿈은 빈칸으로, 앞뒤 빈칸은 뺀다
    for (char& c : l) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    const size_t b = l.find_first_not_of(" \t"), e = l.find_last_not_of(" \t");
    l = b == std::string::npos ? std::string() : l.substr(b, e - b + 1);
    {
        std::lock_guard<std::mutex> g(lock);
        auto it = user.find(key);
        UserEntry entry = it != user.end() ? it->second : UserEntry();
        entry.label = l;
        UpdateUserLocked(key, entry);
    }
    Save();
}

std::string SoundCatalog::Label(const std::string& key) const {
    std::lock_guard<std::mutex> g(lock);
    auto it = user.find(key);
    return it == user.end() ? std::string() : it->second.label;
}

void SoundCatalog::ClearSeen() {
    std::lock_guard<std::mutex> g(lock);
    seen.clear();
    seen_index.clear();
    AddUserEntriesLocked();
    version++;
}

void SoundCatalog::AddUserEntriesLocked() {
    for (const auto& kv : user) {
        if (seen_index.count(kv.first)) continue;
        Seen s;
        s.key = kv.first;
        s.type = TypeOfKey(kv.first);
        s.name = kv.second.name.empty() ? kv.first : kv.second.name;
        s.auto_kind = Kind_Unknown;
        s.user_kind = kv.second.kind;
        s.volume = kv.second.volume;
        s.label = kv.second.label;
        s.count = 0;
        s.order = (uint32_t) seen.size();
        seen_index.emplace(kv.first, seen.size());
        seen.push_back(std::move(s));
    }
}

std::string SoundCatalog::StoragePath() const {
    std::lock_guard<std::mutex> g(lock);
    return PathLocked();
}

const char* SoundCatalog::KindName(int kind) {
    switch (kind) {
    case Kind_Unknown: return "아직 안 나옴";
    case Kind_Auto: return "자동";
    case Kind_None: return "아무것도 아님";
    case Kind_Bgm: return "BGM";
    case Kind_Jingle: return "팡파레·대사";
    case Kind_Cutscene: return "연출";
    case Kind_Sfx: return "효과음";
    }
    return "?";
}

const char* SoundCatalog::KindTag(int kind) {
    switch (kind) {
    case Kind_None: return "none";
    case Kind_Bgm: return "bgm";
    case Kind_Jingle: return "jingle";
    case Kind_Cutscene: return "cutscene";
    case Kind_Sfx: return "sfx";
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

std::string SoundCatalog::FileText() const {
    std::ostringstream o;
    o << "# ff-bgm1x 소리 분류 (" << title << ")\n";
    o << "# 분류: bgm = BGM 1배속, jingle = 팡파레·대사 1배속으로 끝까지, cutscene = 연출(배속 멈춤),\n";
    o << "#       sfx = 효과음 1배속, none = 원본 그대로, auto = 에뮬레이터가 자동으로 판단.\n";
    o << "# vol=<%> : 이 소리의 볼륨 (100 = 그대로, 0~" << kMaxVolume
      << "). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용된다.\n";
    o << "# named : # 다음 글자가 사용자가 붙인 이름 (named 가 없으면 # 다음은 읽기 쉽게 적은 원래 이름).\n";
    o << "# 여기 없는 소리는 에뮬레이터가 자동으로 판단한다.\n";
    o << "game " << code << "\n";
    for (const auto& kv : user) {
        o << kv.first << " " << KindTag(kv.second.kind);
        if (kv.second.volume != 100) o << " vol=" << kv.second.volume;
        if (!kv.second.label.empty()) {
            o << " named # " << kv.second.label;
        } else if (!kv.second.name.empty()) {
            o << " # " << kv.second.name;
        }
        o << "\n";
    }
    return o.str();
}

bool SoundCatalog::ParseFile(const std::string& text, std::map<std::string, UserEntry>& out, std::string& error) const {
    std::istringstream in(text);
    std::string line, fileCode;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t hash = line.find('#');
        std::istringstream ls(hash == std::string::npos ? line : line.substr(0, hash));
        std::string a, b;
        if (!(ls >> a)) continue;
        ls >> b;
        if (a == "game") {
            fileCode = b;
            continue;
        }
        if (a.find(':') == std::string::npos) continue;
        UserEntry e;
        e.kind = KindFromTag(b);
        bool named = b == "named";
        std::string opt;
        while (ls >> opt) {
            if (opt.rfind("vol=", 0) == 0) e.volume = std::clamp(std::atoi(opt.c_str() + 4), 0, kMaxVolume);
            if (opt == "named") named = true;
        }
        if (hash != std::string::npos) {
            std::string n = line.substr(hash + 1);
            const size_t b0 = n.find_first_not_of(" \t");
            n = b0 == std::string::npos ? std::string() : n.substr(b0);
            if (named) {
                e.label = n;
            } else {
                e.name = n;
            }
        }
        if (e.empty()) continue;
        out[a] = e;
    }
    if (!fileCode.empty() && fileCode != code) {
        error = "다른 게임(" + fileCode + ")의 목록입니다. 이 게임은 " + code + " 입니다.";
        return false;
    }
    return true;
}

bool SoundCatalog::Save() const {
    std::string path, text, d;
    {
        std::lock_guard<std::mutex> g(lock);
        path = PathLocked();
        if (path.empty()) return false;
        d = dir;
        text = FileText();
    }
    makeDir(d);
    return writeText(path, text);
}

bool SoundCatalog::ImportFrom(const std::string& path, std::string& error) {
    std::string text;
    if (!readText(path, text)) {
        error = "파일을 열 수 없습니다.";
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock);
        std::map<std::string, UserEntry> m;
        if (!ParseFile(text, m, error)) return false;
        for (const auto& kv : m) UpdateUserLocked(kv.first, kv.second);
        AddUserEntriesLocked();
        version++;
    }
    Save();
    return true;
}

bool SoundCatalog::ExportTo(const std::string& path) const {
    std::string text;
    {
        std::lock_guard<std::mutex> g(lock);
        text = FileText();
    }
    return writeText(path, text);
}
