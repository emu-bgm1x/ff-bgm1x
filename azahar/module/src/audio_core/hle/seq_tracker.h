// ff-bgm1x: 게임 메모리에서 사운드 라이브러리의 시퀀스 트랙 칸을 지켜보고, 시퀀스가 새로 시작되는 순간을 알려 준다.
//
// 오메가루비에서 관찰한 구조: 선형 힙에 올라온 CSEQ 마다, 트랙 칸(0x138 바이트 간격 배열)의 +0 은 그 시퀀스 DATA 시작,
// +4 는 지금 읽는 위치를 가리킨다. 효과음이 시작되면 칸의 +0 이 새 시퀀스로 바뀌고 +4 가 처음부터 다시 간다.
// 처음 한 번(못 찾으면 다시) 선형 힙을 훑어 트랙 칸 배열을 찾고, 그 뒤로는 칸만 읽는다.
#pragma once

#include <cstddef>
#include <map>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"

namespace AudioCore::HLE {

class SeqTracker {
public:
    struct Start {
        u64 file_hash = 0;  // 시퀀스 파일(CSEQ) 전체의 FNV-1a 해시
        u32 cursor = 0;     // 감지했을 때의 읽기 위치 (DATA 기준)
    };

    void Reset();
    bool Ready() const { return !track_slots.empty(); }
    // DSP 프레임마다 (에뮬레이션 스레드). 새로 시작된 시퀀스를 out 에 더한다
    void OnDspFrame(u64 frame, std::vector<Start>& out);

private:
    static constexpr u32 kStride = 0x138;  // 트랙 칸 크기 (관찰값)
    struct Region {
        VAddr va = 0;
        u32 size = 0;
        const u8* ptr = nullptr;
    };
    struct Cseq {
        VAddr va = 0;     // 파일 시작
        u32 size = 0;
        VAddr data = 0;   // DATA 블록 본문
        u64 hash = 0;
    };
    struct Slot {
        u32 base = 0, cursor = 0;
    };

    void LoadRegions();
    const u8* Ptr(VAddr va, u32 len) const;
    // DATA 시작 주소로 그 CSEQ 파일을 찾는다 (처음 보면 앞쪽에서 파일 머리를 찾아 해시를 구한다)
    const Cseq* CseqForData(VAddr data);
    void FindSlots();

    std::vector<Region> regions;
    std::unordered_map<VAddr, Cseq> cseq_by_data;
    std::map<VAddr, Slot> track_slots;
    u64 last_scan = 0;
    bool scanned_once = false;
};

} // namespace AudioCore::HLE
