// ROM 이미지 접근과 MP2K 플레이어 테이블(gMPlayTable) 후보 탐색.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ff-bgm1x: 게임 RAM 사본의 한 조각 (0x02xxxxxx·0x03xxxxxx 주소). RamImage 는 주소 순, 겹치지 않는다
struct RamSegment {
    uint32_t addr = 0;
    std::vector<uint8_t> bytes;
};
using RamImage = std::vector<RamSegment>;

struct Rom {
    std::vector<uint8_t> data;

    // ff-bgm1x: 겹쳐 보기. base 가 있으면 롬은 base 의 것을 쓰고, ram 에 RAM 사본이 있으면
    // 0x02xxxxxx·0x03xxxxxx 주소도 읽는다 (울음소리처럼 게임이 RAM 에 만든 곡을 1배속 재생기가 읽을 때)
    const Rom* base = nullptr;
    std::shared_ptr<const RamImage> ram;
    // addr 부터 len 바이트를 읽을 수 있으면 그 위치, 아니면 nullptr
    const uint8_t* at(uint32_t addr, uint32_t len = 1) const;

    bool load(const std::string& path);
    bool loadFromMemory(const uint8_t* p, size_t n);

    // GBA 버스 주소(0x08000000~) -> ROM 오프셋. 범위 밖이면 false.
    bool valid(uint32_t addr, uint32_t len = 1) const;
    uint8_t u8(uint32_t addr) const;
    uint16_t u16(uint32_t addr) const;
    uint32_t u32(uint32_t addr) const;
    const int8_t* ptr(uint32_t addr) const;

    std::string gameCode() const;   // 헤더 0xAC, 4바이트
    std::string gameTitle() const;  // 헤더 0xA0, 12바이트
};

// gMPlayTable 의 한 항목 { MusicPlayerInfo*, MusicPlayerTrack*, u8 numTracks, u8 pad, u16 unk }
struct MPlayEntry {
    uint32_t info;
    uint32_t tracks;
    uint8_t numTracks;
};

struct MPlayTableCandidate {
    uint32_t romAddr;               // 테이블 시작 주소 (0x08xxxxxx)
    std::vector<MPlayEntry> entries;
};

// ROM 전체를 훑어 gMPlayTable 구조와 일치하는 연속 항목들을 찾는다.
// 실제 사용 여부는 실행 중 RAM 의 MusicPlayerInfo.ident 로 확인해야 한다.
std::vector<MPlayTableCandidate> findMPlayTables(const Rom& rom, size_t maxResults = 64);

// ff-bgm1x: 곡 목록(gSongTable, 항목 {곡 머리, u16 플레이어, u16 플레이어})을 찾아 곡 머리 -> 곡 번호.
// 못 찾으면 비어 있다
std::unordered_map<uint32_t, int> findSongNumbers(const Rom& rom);
