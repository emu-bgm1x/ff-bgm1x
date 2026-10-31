// ff-bgm1x: 시퀀스 트랙 칸 감시. 설명은 seq_tracker.h
#include "audio_core/hle/seq_tracker.h"

#include <algorithm>
#include <cstring>
#include <set>

#include "audio_core/hle/ctr_synth.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace AudioCore::HLE {

namespace {
u32 R32(const u8* p) {
    u32 v;
    std::memcpy(&v, p, 4);
    return v;
}
u16 R16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
constexpr u64 kRescanFrames = 2048;  // 트랙 칸을 못 찾았을 때 다시 찾는 간격 (게임 시간 약 10초)
} // namespace

void SeqTracker::Reset() {
    regions.clear();
    cseq_by_data.clear();
    track_slots.clear();
    last_scan = 0;
    scanned_once = false;
}

void SeqTracker::LoadRegions() {
    // 상태를 불러오면 3DS 메모리의 호스트 주소가 바뀔 수 있으므로 프레임마다 다시 얻는다 (영역 수십 개라 가볍다)
    regions.clear();
    auto& sys = Core::System::GetInstance();
    auto process = sys.Kernel().GetCurrentProcess();
    if (!process) return;
    for (const auto& [va, vma] : process->vm_manager.vma_map) {
        if (vma.type != Kernel::VMAType::BackingMemory) continue;
        const bool linear = (va >= Memory::LINEAR_HEAP_VADDR && va < Memory::LINEAR_HEAP_VADDR_END) ||
                            (va >= Memory::NEW_LINEAR_HEAP_VADDR && va < Memory::NEW_LINEAR_HEAP_VADDR_END);
        const u8* p = vma.backing_memory.GetPtr();
        if (linear && p) regions.push_back({va, static_cast<u32>(vma.size), p});
    }
}

const u8* SeqTracker::Ptr(VAddr va, u32 len) const {
    for (const Region& r : regions) {
        if (va >= r.va && static_cast<u64>(va) + len <= static_cast<u64>(r.va) + r.size) return r.ptr + (va - r.va);
    }
    return nullptr;
}

const SeqTracker::Cseq* SeqTracker::CseqForData(VAddr data) {
    // DATA 본문 앞쪽에서 "CSEQ" 파일 머리를 찾는다 (같은 자리에 다른 시퀀스가 다시 올라올 수 있어 매번 확인한다)
    for (u32 back = 0x20; back <= 0x200; back += 4) {
        const VAddr va = data - back;
        const u8* p = Ptr(va, 0x40);
        if (!p || std::memcmp(p, "CSEQ", 4) != 0 || R16(p + 4) != 0xFEFF) continue;
        const u32 size = R32(p + 0x0C);
        if (size <= 0x20 || size > 0x100000) return nullptr;
        VAddr d = 0;
        for (u16 i = 0; i < R16(p + 0x10) && i < 8; ++i) {
            if (R16(p + 0x14 + 12 * i) == 0x5000) d = va + R32(p + 0x18 + 12 * i) + 8;
        }
        if (d != data) return nullptr;
        const u8* f = Ptr(va, size);
        if (!f) return nullptr;
        Cseq& c = cseq_by_data[data];
        c = {va, size, d, CtrSynth::FileHash(f, size)};
        return &c;
    }
    return nullptr;
}

void SeqTracker::FindSlots() {
    // 1) 선형 힙의 CSEQ 들 (DATA 범위)
    std::vector<std::pair<VAddr, VAddr>> ranges;  // [DATA 시작, 파일 끝)
    for (const Region& r : regions) {
        for (u32 o = 0; o + 0x40 <= r.size; o += 4) {
            const u8* p = r.ptr + o;
            if (p[0] != 'C' || std::memcmp(p, "CSEQ", 4) != 0 || R16(p + 4) != 0xFEFF) continue;
            const u32 size = R32(p + 0x0C);
            if (size <= 0x20 || size > 0x100000 || o + size > r.size) continue;
            for (u16 i = 0; i < R16(p + 0x10) && i < 8; ++i) {
                if (R16(p + 0x14 + 12 * i) == 0x5000)
                    ranges.push_back({r.va + o + R32(p + 0x18 + 12 * i) + 8, r.va + o + size});
            }
        }
    }
    if (ranges.empty()) return;
    std::sort(ranges.begin(), ranges.end());
    auto in_data = [&](u32 v) {
        auto it = std::upper_bound(ranges.begin(), ranges.end(), std::make_pair(static_cast<VAddr>(v), ~0u));
        if (it == ranges.begin()) return std::make_pair(VAddr{0}, false);
        --it;
        return std::make_pair(it->first, v < it->second);
    };
    // 2) +0 이 DATA 시작이고 +4 가 같은 시퀀스 안인 자리 = 트랙 칸 후보
    std::set<VAddr> hits;
    for (const Region& r : regions) {
        for (u32 o = 0; o + 8 <= r.size; o += 4) {
            const u32 v0 = R32(r.ptr + o);
            if (v0 < ranges.front().first || v0 >= ranges.back().second) continue;
            const auto [d0, ok0] = in_data(v0);
            if (!ok0 || d0 != v0) continue;
            const auto [d1, ok1] = in_data(R32(r.ptr + o + 4));
            if (ok1 && d1 == d0) hits.insert(r.va + o);
        }
    }
    // 3) 0x138 간격으로 이웃이 있는 후보는 배열로 보고, 묶음 사이와 앞뒤 4칸까지 지켜본다
    std::vector<VAddr> members;
    for (VAddr h : hits) {
        if (hits.count(h + kStride) || hits.count(h - kStride) || hits.count(h + 2 * kStride) ||
            hits.count(h - 2 * kStride))
            members.push_back(h);
    }
    std::size_t groups = 0;
    for (std::size_t i = 0; i < members.size();) {
        std::size_t j = i;
        while (j + 1 < members.size() && members[j + 1] - members[j] <= 4 * kStride &&
               (members[j + 1] - members[i]) % kStride == 0)
            ++j;
        for (VAddr v = members[i] - 4 * kStride; v <= members[j] + 4 * kStride; v += kStride) {
            if (Ptr(v, 8) && track_slots.size() < 256) track_slots.emplace(v, Slot{});
        }
        ++groups;
        i = j + 1;
    }
    LOG_INFO(Audio_DSP, "ff-bgm1x: 효과음 시퀀스 트랙 칸 {}개를 찾음 (배열 {}개, CSEQ {}개)", track_slots.size(), groups,
             ranges.size());
}

void SeqTracker::OnDspFrame(u64 frame, std::vector<Start>& out) {
    LoadRegions();
    if (regions.empty()) return;
    if (track_slots.empty()) {
        if (scanned_once && frame - last_scan < kRescanFrames) return;
        scanned_once = true;
        last_scan = frame;
        FindSlots();
        // 처음 읽는 값은 기준으로만 삼는다
        for (auto& [va, s] : track_slots) {
            if (const u8* p = Ptr(va, 8)) s = {R32(p), R32(p + 4)};
        }
        return;
    }
    std::set<u32> seen;
    for (auto& [va, s] : track_slots) {
        const u8* p = Ptr(va, 8);
        if (!p) continue;
        const u32 base = R32(p), cursor = R32(p + 4);
        const bool started = base && (base != s.base || cursor < s.cursor) && cursor >= base;
        s = {base, cursor};
        if (!started || !seen.insert(base).second) continue;
        if (const Cseq* c = CseqForData(base)) out.push_back({c->hash, cursor - base});
    }
}

} // namespace AudioCore::HLE
