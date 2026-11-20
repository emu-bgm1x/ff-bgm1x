// ff-bgm1x: 게임 소리 목록과 사용자 분류. 설명은 SoundCatalog.h
#include "SoundCatalog.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef FFBGM_DESMUME
#define CRC32 ffCrc32
#else
#include "CRC32.h"
#endif

// ff-bgm1x: 안드로이드 앱(영어 화면)에서는 영어 문구
#ifdef __ANDROID__
#define FF_TEXT(ko, en) en
#else
#define FF_TEXT(ko, en) ko
#endif

using namespace melonDS;

namespace
{

u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

constexpr u32 kSsarHead = 64;  // 효과음 묶음을 알아볼 때 쓰는 데이터 앞부분 길이

std::string readName(const u8* base, u32 left, u32 off)
{
    if (!off || off >= left) return {};
    const char* s = (const char*)base + off;
    return std::string(s, strnlen(s, std::min<u32>(64, left - off)));
}

std::filesystem::path u8path(const std::string& s)
{
    return std::filesystem::u8path(s);  // UTF-8 경로 (한글 폴더)
}

} // namespace

bool SoundCatalog::readRom(u32 off, u32 len, std::vector<u8>& out) const
{
    if (off > romlen || len > romlen - off) return false;
    out.resize(len);
    if (rom)
    {
        memcpy(out.data(), rom + off, len);
        return true;
    }
    return reader && reader(off, len, out.data());
}

void SoundCatalog::index(const u8* r, u32 len, bool keep)
{
    std::lock_guard g(lock);
    rom = r;
    romlen = len;
    sdats.clear();
    seqByCrc.clear();
    ssarByHead.clear();
    strmRanges.clear();
    user.clear();
    seen.clear();
    seenIndex.clear();
    version++;
    code.clear();
    title.clear();
    if (!rom || romlen < 0x200) return;
    code.assign((const char*)rom + 0x0C, strnlen((const char*)rom + 0x0C, 4));
    title.assign((const char*)rom, strnlen((const char*)rom, 12));

    static const u8 magic[6] = {'S', 'D', 'A', 'T', 0xFF, 0xFE};
    for (const u8* it = rom;;)
    {
        it = std::search(it, rom + romlen, magic, magic + 6);
        if (it == rom + romlen) break;
        const u8* sd = it;
        it += 4;
        const u32 left = (u32)(rom + romlen - sd);
        if (left < 0x40) break;
        const u32 symbOff = rd32(sd + 0x10), symbSize = rd32(sd + 0x14);
        const u32 infoOff = rd32(sd + 0x18), fatOff = rd32(sd + 0x20);
        // 압축 데이터 안에서 우연히 같은 글자가 나온 곳은 건너뛴다
        if (infoOff + 0x40 > left || fatOff + 0x10 > left || memcmp(sd + infoOff, "INFO", 4) != 0) continue;
        const u8* info = sd + infoOff;
        const u8* fat = sd + fatOff;
        const u8* symb = (symbSize && symbOff + 0x40 <= left && memcmp(sd + symbOff, "SYMB", 4) == 0) ? sd + symbOff : nullptr;
        const u32 fatCount = rd32(fat + 8);
        const u32 base = (u32)(sd - rom);
        const int sdatNo = (int)sdats.size();
        sdats.emplace_back();
        Sdat& S = sdats.back();

        // INFO 의 k 번째 기록: (개수, i 번째 항목 위치). 범위를 벗어나면 0
        auto infoCount = [&](int k) -> u32 {
            const u32 rec = rd32(info + 8 + 4 * k);
            if (!rec || infoOff + rec + 4 > left) return 0;
            return std::min<u32>(rd32(info + rec), 0x10000);
        };
        auto infoEntry = [&](int k, u32 i, u32 need) -> const u8* {
            const u32 rec = rd32(info + 8 + 4 * k);
            if (infoOff + rec + 8 + 4 * i > left) return nullptr;
            const u32 e = rd32(info + rec + 4 + 4 * i);
            if (!e || infoOff + e + need > left) return nullptr;
            return info + e;
        };
        auto fileOf = [&](const u8* e, u32& off, u32& size) -> bool {
            const u32 id = rd16(e);
            if (id >= fatCount || fatOff + 12 + 16 * id + 8 > left) return false;
            off = rd32(fat + 12 + 16 * id);
            size = rd32(fat + 12 + 16 * id + 4);
            if (off >= left || size > left - off) return false;
            off += base;
            return true;
        };
        // SYMB 의 k 번째 기록에서 i 번째 이름 (SEQARC 는 항목이 8바이트)
        auto symbName = [&](int k, u32 i, u32 stride) -> std::string {
            if (!symb) return {};
            const u32 rec = rd32(symb + 8 + 4 * k);
            if (!rec || symbOff + rec + 4 > left || i >= rd32(symb + rec)) return {};
            if (symbOff + rec + 4 + stride * (i + 1) > left) return {};
            return readName(symb, left - symbOff, rd32(symb + rec + 4 + stride * i));
        };

        // 시퀀스
        const u32 nSeq = infoCount(0);
        S.seqs.resize(nSeq);
        for (u32 i = 0; i < nSeq; i++)
        {
            SeqEntry& e = S.seqs[i];
            e.sdat = sdatNo;
            e.index = (int)i;
            const u8* p = infoEntry(0, i, 12);
            if (!p || !fileOf(p, e.fileOff, e.fileSize) || e.fileSize < 0x20) { e.fileSize = 0; continue; }
            if (memcmp(rom + e.fileOff, "SSEQ", 4) != 0) { e.fileSize = 0; continue; }
            e.bank = rd16(p + 4);
            e.volume = p[6];
            e.player = p[9];
            e.name = symbName(0, i, 4);
            seqByCrc.emplace(CRC32(rom + e.fileOff, (int)e.fileSize), std::make_pair(sdatNo, (int)i));
        }

        // 효과음 묶음
        const u32 nArc = infoCount(1);
        S.ssars.resize(nArc);
        u32 arcSymRec = symb ? rd32(symb + 8 + 4) : 0;
        for (u32 i = 0; i < nArc; i++)
        {
            SsarArc& a = S.ssars[i];
            a.sdat = sdatNo;
            a.index = (int)i;
            const u8* p = infoEntry(1, i, 4);
            if (!p || !fileOf(p, a.fileOff, a.fileSize) || a.fileSize < 0x20) { a.fileSize = 0; continue; }
            const u8* f = rom + a.fileOff;
            if (memcmp(f, "SSAR", 4) != 0) { a.fileSize = 0; continue; }
            a.dataOff = rd32(f + 0x18);
            const u32 n = std::min<u32>(rd32(f + 0x1C), 0x10000);
            if (a.dataOff > a.fileSize || 0x20 + 12 * n > a.fileSize) { a.fileSize = 0; continue; }
            // 이름: SYMB 의 SEQARC 기록은 {묶음 이름, 안쪽 이름 목록} 쌍
            const u8* sub = nullptr;
            if (symb && arcSymRec && symbOff + arcSymRec + 4 + 8 * (i + 1) <= left && i < rd32(symb + arcSymRec))
            {
                a.name = readName(symb, left - symbOff, rd32(symb + arcSymRec + 4 + 8 * i));
                const u32 subOff = rd32(symb + arcSymRec + 4 + 8 * i + 4);
                if (subOff && symbOff + subOff + 4 <= left) sub = symb + subOff;
            }
            a.seqs.resize(n);
            for (u32 j = 0; j < n; j++)
            {
                const u8* r = f + 0x20 + 12 * j;
                SsarSeq& s = a.seqs[j];
                s.offset = rd32(r);
                s.bank = rd16(r + 4);
                s.volume = r[6];
                if (sub && j < rd32(sub) && (u32)(sub - symb) + 4 + 4 * (j + 1) <= left - symbOff)
                    s.name = readName(symb, left - symbOff, rd32(sub + 4 + 4 * j));
            }
            const u32 headLen = std::min(kSsarHead, a.fileSize - a.dataOff);
            if (headLen >= 8) ssarByHead.emplace(CRC32(f + a.dataOff, (int)headLen), std::make_pair(sdatNo, (int)i));
        }

        // 뱅크, 웨이브 아카이브
        const u32 nBank = infoCount(2);
        S.banks.resize(nBank);
        for (u32 i = 0; i < nBank; i++)
        {
            BankEntry& b = S.banks[i];
            const u8* p = infoEntry(2, i, 12);
            if (!p || !fileOf(p, b.fileOff, b.fileSize)) { b.fileSize = 0; continue; }
            for (int w = 0; w < 4; w++)
            {
                const u16 v = rd16(p + 4 + 2 * w);
                b.warc[w] = v == 0xFFFF ? -1 : v;
            }
        }
        const u32 nWarc = infoCount(3);
        S.warcs.resize(nWarc);
        for (u32 i = 0; i < nWarc; i++)
        {
            const u8* p = infoEntry(3, i, 4);
            if (!p || !fileOf(p, S.warcs[i].fileOff, S.warcs[i].fileSize)) S.warcs[i].fileSize = 0;
        }

        // 스트림
        const u32 nStrm = infoCount(7);
        S.strms.resize(nStrm);
        for (u32 i = 0; i < nStrm; i++)
        {
            StrmEntry& e = S.strms[i];
            e.sdat = sdatNo;
            e.index = (int)i;
            const u8* p = infoEntry(7, i, 8);
            if (!p || !fileOf(p, e.fileOff, e.fileSize) || e.fileSize < 0x40) { e.fileSize = 0; continue; }
            const u8* f = rom + e.fileOff;
            if (memcmp(f, "STRM", 4) != 0) { e.fileSize = 0; continue; }
            e.dataOff = rd32(f + 0x28);
            if (e.dataOff >= e.fileSize) { e.fileSize = 0; continue; }
            e.loop = f[0x19] != 0;
            e.volume = p[4];
            e.name = symbName(7, i, 4);
            strmRanges.push_back({e.fileOff + e.dataOff, e.fileOff + e.fileSize, sdatNo, (int)i});
        }
    }
    std::sort(strmRanges.begin(), strmRanges.end(),
              [](const StrmRange& a, const StrmRange& b) { return a.data < b.data; });
    if (!keep) rom = nullptr;  // 이 메모리는 곧 사라진다 (DeSmuME: 색인할 때만 롬을 읽는다)
}

const SoundCatalog::SeqEntry* SoundCatalog::findSeq(u32 crc) const
{
    auto it = seqByCrc.find(crc);
    if (it == seqByCrc.end()) return nullptr;
    return &sdats[it->second.first].seqs[it->second.second];
}

const SoundCatalog::SsarArc* SoundCatalog::findSsar(const u8* data, u32 avail, u32 offset, int& seqNo) const
{
    seqNo = -1;
    for (u32 len : {kSsarHead, 32u, 16u, 8u})
    {
        if (avail < len) continue;
        auto it = ssarByHead.find(CRC32(data, (int)len));
        if (it == ssarByHead.end()) continue;
        const SsarArc& a = sdats[it->second.first].ssars[it->second.second];
        // 데이터가 묶음 크기보다 짧은 묶음은 앞부분 길이가 다르다. 실제로 맞는 길이인지 확인
        if (std::min(kSsarHead, a.fileSize - a.dataOff) != len) continue;
        for (size_t j = 0; j < a.seqs.size(); j++)
        {
            if (a.seqs[j].offset == offset)
            {
                seqNo = (int)j;
                break;
            }
        }
        return &a;
    }
    return nullptr;
}

const SoundCatalog::StrmEntry* SoundCatalog::findStrmData(u32 addr) const
{
    auto it = std::upper_bound(strmRanges.begin(), strmRanges.end(), addr,
                               [](u32 a, const StrmRange& r) { return a < r.data; });
    if (it == strmRanges.begin()) return nullptr;
    --it;
    if (addr >= it->end) return nullptr;
    return &sdats[it->sdat].strms[it->index];
}

std::string SoundCatalog::seqKey(const SeqEntry& e)
{
    return "seq:" + std::to_string(e.sdat) + ":" + std::to_string(e.index);
}

std::string SoundCatalog::ssarKey(const SsarArc& a, int seqNo)
{
    return "ssar:" + std::to_string(a.sdat) + ":" + std::to_string(a.index) + ":" + std::to_string(seqNo);
}

std::string SoundCatalog::strmKey(const StrmEntry& e)
{
    return "strm:" + std::to_string(e.sdat) + ":" + std::to_string(e.index);
}

int SoundCatalog::noteSeen(const std::string& key, int type, const std::string& name, int autoKind)
{
    std::lock_guard g(lock);
    auto u = user.find(key);
    const int kind = u != user.end() && u->second.kind != Kind_Auto ? u->second.kind : autoKind;
    auto it = seenIndex.find(key);
    if (it != seenIndex.end())
    {
        Seen& s = seen[it->second];
        s.count++;
        s.autoKind = autoKind;
        if (!name.empty()) s.name = name;
        version++;
    }
    else if (collect)
    {
        Seen s;
        s.key = key;
        s.type = type;
        s.name = name;
        s.autoKind = autoKind;
        s.userKind = u != user.end() ? u->second.kind : Kind_Auto;
        s.volume = u != user.end() ? u->second.volume : 100;
        s.count = 1;
        s.order = (u32)seen.size();
        seenIndex.emplace(key, seen.size());
        seen.push_back(std::move(s));
        version++;
    }
    return kind;
}

void SoundCatalog::setCollecting(bool on)
{
    std::lock_guard g(lock);
    collect = on;
}

bool SoundCatalog::collecting() const
{
    std::lock_guard g(lock);
    return collect;
}

std::vector<SoundCatalog::Seen> SoundCatalog::seenList() const
{
    std::lock_guard g(lock);
    return seen;
}

u32 SoundCatalog::seenVersion() const
{
    std::lock_guard g(lock);
    return version;
}

int SoundCatalog::userKind(const std::string& key) const
{
    std::lock_guard g(lock);
    auto it = user.find(key);
    return it == user.end() ? Kind_Auto : it->second.kind;
}

int SoundCatalog::volume(const std::string& key) const
{
    std::lock_guard g(lock);
    auto it = user.find(key);
    return it == user.end() ? 100 : it->second.volume;
}

// 사용자 지정을 바꾼 뒤: 기본값(자동, 100%)만 남으면 지우고, 목록 줄에도 반영한다. 잠근 채로 부른다
void SoundCatalog::updateUserLocked(const std::string& key, UserEntry e)
{
    auto it = seenIndex.find(key);
    if (it != seenIndex.end() && e.name.empty()) e.name = seen[it->second].name;
    if (e.kind == Kind_Auto && e.volume == 100)
        user.erase(key);
    else
        user[key] = e;
    if (it != seenIndex.end())
    {
        seen[it->second].userKind = e.kind;
        seen[it->second].volume = e.volume;
    }
    version++;
}

void SoundCatalog::setUserKind(const std::string& key, int kind)
{
    {
        std::lock_guard g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.kind = kind;
        updateUserLocked(key, e);
    }
    save();
}

void SoundCatalog::setVolume(const std::string& key, int percent)
{
    {
        std::lock_guard g(lock);
        auto it = user.find(key);
        UserEntry e = it != user.end() ? it->second : UserEntry();
        e.volume = std::clamp(percent, 0, kMaxVolume);
        updateUserLocked(key, e);
    }
    save();
}

void SoundCatalog::resetUser(const std::string& key)
{
    {
        std::lock_guard g(lock);
        updateUserLocked(key, UserEntry());
    }
    save();
}

void SoundCatalog::clearSeen()
{
    std::lock_guard g(lock);
    seen.clear();
    seenIndex.clear();
    addUserEntriesLocked();
    version++;
}

void SoundCatalog::addUserEntriesLocked()
{
    for (const auto& [key, e] : user)
    {
        if (seenIndex.count(key)) continue;
        Seen s;
        s.key = key;
        s.type = key.rfind("strm:", 0) == 0 ? Type_Strm : key.rfind("ssar:", 0) == 0 ? Type_Ssar : Type_Seq;
        s.name = e.name.empty() ? key : e.name;
        s.autoKind = Kind_Unknown;
        s.userKind = e.kind;
        s.volume = e.volume;
        s.count = 0;
        s.order = (u32)seen.size();
        seenIndex.emplace(key, seen.size());
        seen.push_back(std::move(s));
    }
}

std::string SoundCatalog::storagePath() const
{
    if (storageDir.empty() || code.empty()) return {};
    return storageDir + "/" + code + ".txt";
}

const char* SoundCatalog::kindName(int kind)
{
    switch (kind)
    {
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

const char* SoundCatalog::kindTag(int kind)
{
    switch (kind)
    {
    case Kind_None: return "none";
    case Kind_Bgm: return "bgm";
    case Kind_Jingle: return "jingle";
    case Kind_Cutscene: return "cutscene";
    case Kind_Sfx: return "sfx";
    }
    return "auto";
}

int SoundCatalog::kindFromTag(const std::string& t)
{
    if (t == "none") return Kind_None;
    if (t == "bgm") return Kind_Bgm;
    if (t == "jingle") return Kind_Jingle;
    if (t == "cutscene") return Kind_Cutscene;
    if (t == "sfx") return Kind_Sfx;
    return Kind_Auto;
}

// 파일 형식 (UTF-8 텍스트, 줄마다 하나):
//   # 주석
//   game <게임 코드>
//   <이름표> <분류> [vol=<볼륨 %>] [# 이름]
//   분류가 auto 면 볼륨만 정한 것
std::string SoundCatalog::fileText() const
{
    std::ostringstream o;
    o << "# ff-bgm1x 소리 분류 (" << title << ")\n";
    o << "# 분류: bgm = BGM 1배속, jingle = 팡파레·대사 1배속으로 끝까지, cutscene = 연출(배속 멈춤),\n";
    o << "#       sfx = 효과음 1배속, none = 원본 그대로, auto = 에뮬레이터가 자동으로 판단.\n";
    o << "# vol=<%> : 이 소리의 볼륨 (100 = 그대로, 0~" << kMaxVolume << "). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용된다.\n";
    o << "# 여기 없는 소리는 에뮬레이터가 자동으로 판단한다.\n";
    o << "game " << code << "\n";
    for (const auto& [key, e] : user)
    {
        o << key << " " << kindTag(e.kind);
        if (e.volume != 100) o << " vol=" << e.volume;
        if (!e.name.empty()) o << " # " << e.name;
        o << "\n";
    }
    return o.str();
}

bool SoundCatalog::parseFile(const std::string& text, std::map<std::string, UserEntry>& out, std::string& error) const
{
    std::istringstream in(text);
    std::string line;
    std::string fileCode;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t hash = line.find('#');
        std::string body = hash == std::string::npos ? line : line.substr(0, hash);
        std::istringstream ls(body);
        std::string a, b;
        if (!(ls >> a)) continue;
        ls >> b;
        if (a == "game")
        {
            fileCode = b;
            continue;
        }
        if (a.rfind("seq:", 0) != 0 && a.rfind("ssar:", 0) != 0 && a.rfind("strm:", 0) != 0) continue;
        UserEntry e;
        e.kind = kindFromTag(b);
        std::string opt;
        while (ls >> opt)
        {
            if (opt.rfind("vol=", 0) == 0) e.volume = std::clamp(atoi(opt.c_str() + 4), 0, kMaxVolume);
        }
        if (e.kind == Kind_Auto && e.volume == 100) continue;
        if (hash != std::string::npos)
        {
            std::string n = line.substr(hash + 1);
            const size_t b0 = n.find_first_not_of(" \t");
            e.name = b0 == std::string::npos ? std::string() : n.substr(b0);
        }
        out[a] = e;
    }
    if (!fileCode.empty() && fileCode != code)
    {
        error = FF_TEXT(std::string("다른 게임(") + fileCode + ")의 목록입니다. 이 게임은 " + code + " 입니다.",
                    std::string("This list is for another game (") + fileCode + "). This game is " + code + ".");
        return false;
    }
    return true;
}

bool SoundCatalog::load()
{
    const std::string path = storagePath();
    if (path.empty()) return false;
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::map<std::string, UserEntry> m;
    std::string err;
    if (!parseFile(ss.str(), m, err)) return false;
    std::lock_guard g(lock);
    user = std::move(m);
    for (Seen& s : seen)
    {
        auto it = user.find(s.key);
        s.userKind = it == user.end() ? Kind_Auto : it->second.kind;
        s.volume = it == user.end() ? 100 : it->second.volume;
    }
    addUserEntriesLocked();
    version++;
    return true;
}

bool SoundCatalog::save() const
{
    const std::string path = storagePath();
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(u8path(storageDir), ec);
    std::string text;
    {
        std::lock_guard g(lock);
        text = fileText();
    }
    std::ofstream f(u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return (bool)f;
}

bool SoundCatalog::importFrom(const std::string& path, std::string& error)
{
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f)
    {
        error = FF_TEXT("파일을 열 수 없습니다.", "Could not open the file.");
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::map<std::string, UserEntry> m;
    if (!parseFile(ss.str(), m, error)) return false;
    {
        std::lock_guard g(lock);
        // 가져온 목록이 이 게임의 지정을 덮어쓴다 (가져온 파일에 없는 지정은 남긴다)
        for (const auto& [k, e] : m) updateUserLocked(k, e);
        addUserEntriesLocked();
        version++;
    }
    save();
    return true;
}

bool SoundCatalog::exportTo(const std::string& path) const
{
    std::string text;
    {
        std::lock_guard g(lock);
        text = fileText();
    }
    std::ofstream f(u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return (bool)f;
}
