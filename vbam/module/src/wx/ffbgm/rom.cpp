#include "rom.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "winutil.h"

bool Rom::load(const std::string& path) {
    FILE* f = fopenUtf8(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 32 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    data.resize((size_t) n);
    size_t got = std::fread(data.data(), 1, (size_t) n, f);
    std::fclose(f);
    return got == (size_t) n;
}

bool Rom::loadFromMemory(const uint8_t* p, size_t n) {
    if (!p || n == 0 || n > 32 * 1024 * 1024) return false;
    data.assign(p, p + n);
    return true;
}

const uint8_t* Rom::at(uint32_t addr, uint32_t len) const {
    if (addr >= 0x02000000u && addr < 0x04000000u) {
        if (!ram) return nullptr;
        // EWRAM 은 256KB, IWRAM 은 32KB 마다 같은 곳이 되풀이된다
        const uint32_t c = addr < 0x03000000u ? 0x02000000u + ((addr - 0x02000000u) & 0x3FFFFu)
                                              : 0x03000000u + ((addr - 0x03000000u) & 0x7FFFu);
        auto it = std::upper_bound(ram->begin(), ram->end(), c,
                                   [](uint32_t v, const RamSegment& s) { return v < s.addr; });
        if (it == ram->begin()) return nullptr;
        --it;
        const uint64_t off = c - it->addr;
        return off + len <= it->bytes.size() ? &it->bytes[(size_t) off] : nullptr;
    }
    if (addr < 0x08000000u || addr >= 0x0A000000u) return nullptr;
    const std::vector<uint8_t>& d = base ? base->data : data;
    const uint64_t off = addr - 0x08000000u;
    return off + len <= d.size() ? &d[off] : nullptr;
}

bool Rom::valid(uint32_t addr, uint32_t len) const {
    return at(addr, len) != nullptr;
}

uint8_t Rom::u8(uint32_t addr) const {
    const uint8_t* p = at(addr, 1);
    return p ? *p : 0;
}

uint16_t Rom::u16(uint32_t addr) const {
    const uint8_t* p = at(addr, 2);
    if (!p) return 0;
    return (uint16_t) (p[0] | (p[1] << 8));
}

uint32_t Rom::u32(uint32_t addr) const {
    const uint8_t* p = at(addr, 4);
    if (!p) return 0;
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

const int8_t* Rom::ptr(uint32_t addr) const {
    return reinterpret_cast<const int8_t*>(at(addr, 1));
}

std::string Rom::gameCode() const {
    if (data.size() < 0xB0) return {};
    return std::string(reinterpret_cast<const char*>(&data[0xAC]), 4);
}

std::string Rom::gameTitle() const {
    if (data.size() < 0xB0) return {};
    std::string s(reinterpret_cast<const char*>(&data[0xA0]), 12);
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    return s;
}

static bool isRamPtr(uint32_t p) {
    if (p & 3) return false;
    if (p >= 0x02000000u && p < 0x02040000u) return true;  // EWRAM
    if (p >= 0x03000000u && p < 0x03008000u) return true;  // IWRAM
    return false;
}

static bool readEntry(const Rom& rom, uint32_t addr, MPlayEntry& e) {
    if (!rom.valid(addr, 12)) return false;
    e.info = rom.u32(addr);
    e.tracks = rom.u32(addr + 4);
    e.numTracks = rom.u8(addr + 8);
    if (!isRamPtr(e.info) || !isRamPtr(e.tracks)) return false;
    if (e.info == e.tracks) return false;
    if (e.numTracks < 1 || e.numTracks > 16) return false;  // MAX_MUSICPLAYER_TRACKS
    if (rom.u8(addr + 9) != 0) return false;                 // 구조체 패딩
    return true;
}

std::vector<MPlayTableCandidate> findMPlayTables(const Rom& rom, size_t maxResults) {
    std::vector<MPlayTableCandidate> out;
    if (rom.data.size() < 12) return out;
    uint32_t end = 0x08000000u + (uint32_t) rom.data.size() - 12;
    for (uint32_t a = 0x08000000u; a <= end; a += 4) {
        MPlayEntry e;
        if (!readEntry(rom, a, e)) continue;
        // 바로 앞이 같은 구조면 이미 이전 시작점에서 처리됨
        MPlayEntry prev;
        if (a >= 0x0800000Cu && readEntry(rom, a - 12, prev)) continue;

        MPlayTableCandidate c;
        c.romAddr = a;
        uint32_t p = a;
        while (readEntry(rom, p, e) && c.entries.size() < 32) {
            bool dup = false;
            for (auto& x : c.entries) {
                if (x.info == e.info || x.tracks == e.tracks) dup = true;
            }
            if (dup) break;
            c.entries.push_back(e);
            p += 12;
        }
        if (c.entries.size() >= 2) {
            out.push_back(std::move(c));
            if (out.size() >= maxResults) break;
        }
    }
    return out;
}

// 곡 목록의 한 항목처럼 보이는지: 곡 머리가 롬 안이고(트랙 0~16개, 트랙 위치도 롬 안), 플레이어 번호가 작다
static bool songEntry(const Rom& rom, uint32_t a) {
    if (!rom.valid(a, 8)) return false;
    const uint32_t h = rom.u32(a);
    if ((h & 3) || !rom.valid(h, 8)) return false;
    if (rom.u16(a + 4) >= 32 || rom.u16(a + 6) >= 32) return false;
    const uint8_t n = rom.u8(h);
    if (n > 16 || !rom.valid(h, 8 + 4u * n)) return false;
    for (uint8_t t = 0; t < n; ++t) {
        if (!rom.valid(rom.u32(h + 8 + 4u * t))) return false;
    }
    return n == 0 || rom.valid(rom.u32(h + 4));
}

std::unordered_map<uint32_t, int> findSongNumbers(const Rom& rom) {
    // 롬 전체에서 위 모양 항목이 가장 길게 이어진 곳을 곡 목록으로 본다 (32개 이상일 때만)
    std::unordered_map<uint32_t, int> out;
    if (rom.data.size() < 16) return out;
    const uint32_t end = 0x08000000u + (uint32_t) rom.data.size() - 8;
    uint32_t bestStart = 0, bestLen = 0;
    for (uint32_t a = 0x08000000u; a <= end; a += 4) {
        if (!songEntry(rom, a)) continue;
        uint32_t n = 0;
        while (a + 8 * n <= end && songEntry(rom, a + 8 * n)) ++n;
        if (n > bestLen) {
            bestLen = n;
            bestStart = a;
        }
        a += 8 * n - 4;  // 이어진 구간은 건너뛴다 (반복문이 4 를 더한다)
    }
    if (bestLen < 32) return out;
    for (uint32_t i = 0; i < bestLen; ++i) out.emplace(rom.u32(bestStart + 8 * i), (int) i);
    return out;
}
