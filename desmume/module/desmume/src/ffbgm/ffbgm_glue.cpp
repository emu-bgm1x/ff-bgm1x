// ff-bgm1x: DeSmuME 쪽 연결 함수 (소리 분류·STRM 1배속용). 선언은 ffbgm_glue.h
#include "ffbgm_glue.h"

#include <cstdio>
#include <cstring>

#include <windows.h>

#include "SPU.h"

extern u16 g_ffbgmMuteMask;  // SPU.cpp
extern u16 g_ffbgmGainMask;
extern s32 g_ffbgmGainQ8;

std::string ffLocalPath(const char* name)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring dir(buf, n);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    const int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), &out[0], len, nullptr, nullptr);
    return out + "\\" + name;
}

bool ffReadFile(const std::string& path, u32 off, u32 len, u8* out)
{
    // 압축 롬(zip, gz 등)은 파일의 위치가 롬의 위치와 다르다: .nds 만 그대로 읽는다
    if (path.size() < 4 || _stricmp(path.c_str() + path.size() - 4, ".nds") != 0) return false;
    FILE* f = fopen(path.c_str(), "rb");  // DeSmuME 가 넘기는 경로는 시스템 코드 페이지
    if (!f) return false;
    bool ok = _fseeki64(f, (long long)off, SEEK_SET) == 0 && fread(out, 1, len, f) == len;
    fclose(f);
    return ok;
}

void ffSpuSetGain(u16 mask, s32 q8)
{
    g_ffbgmGainQ8 = q8;
    g_ffbgmGainMask = q8 == 256 ? 0 : mask;
}

void ffSpuSetMute(u16 mask)
{
    g_ffbgmMuteMask = mask;
}

void ffSpuChannel(int ch, int& volume, int& shift, int& pan)
{
    // DeSmuME: vol 0~127, volumeDiv 0~3 (1, 1/2, 1/4, 1/16), pan 0~127
    // melonDS 와 같은 값으로: 음량 127 -> 128, 나눗셈 칸 -> 왼쪽으로 미는 칸 수 (4,3,2,0)
    static const int kShift[4] = {4, 3, 2, 0};
    if (!SPU_core || ch < 0 || ch > 15) return;
    const channel_struct& c = SPU_core->channels[ch];
    volume = c.vol == 127 ? 128 : c.vol;
    shift = kShift[c.volumeDiv & 3];
    pan = c.pan;
}

int ffSpuMaster()
{
    if (!SPU_core) return 128;
    const int v = SPU_core->regs.mastervol;
    return v == 127 ? 128 : v;
}
