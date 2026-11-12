// ff-bgm1x: melonDS 수정판의 BGM 1배속 모듈을 DeSmuME 에 붙이기 위한 연결부.
// melonDS 쪽 타입 이름(melonDS::u8 등)과 메모리 접근 함수를 DeSmuME 의 것으로 연결한다.
#ifndef FFBGM_GLUE_H
#define FFBGM_GLUE_H

#include <zlib.h>

#include "types.h"
#include "MMU.h"
#include "SPU.h"  // DESMUME_SAMPLE_RATE

namespace melonDS
{
using ::u8;
using ::s8;
using ::u16;
using ::s16;
using ::u32;
using ::s32;
using ::u64;
using ::s64;
}

// ARM7 쪽 주소 공간에서 읽고 쓴다 (디버그 접근: 캐시나 타이밍을 건드리지 않는다)
inline u8 ffArm7R8(u32 addr) { return _MMU_read08<ARMCPU_ARM7, MMU_AT_DEBUG>(addr); }
inline u16 ffArm7R16(u32 addr) { return _MMU_read16<ARMCPU_ARM7, MMU_AT_DEBUG>(addr); }
inline u32 ffArm7R32(u32 addr) { return _MMU_read32<ARMCPU_ARM7, MMU_AT_DEBUG>(addr); }
inline void ffArm7W16(u32 addr, u16 val) { _MMU_write16<ARMCPU_ARM7, MMU_AT_DEBUG>(addr, val); }

template <class T> inline T ffclamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline u32 ffCrc32(const u8* p, int n) { return (u32)crc32(0L, p, (uInt)n); }

// 메인 메모리 (16MB 배열, 주소는 mask 로 접는다)
#define FF_MAIN_RAM (MMU.MAIN_MEM)
#define FF_MAIN_RAM_MASK (_MMU_MAIN_MEM_MASK)

#endif // FFBGM_GLUE_H
