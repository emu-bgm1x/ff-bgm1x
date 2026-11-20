// ff-bgm1x: 스트림 곡(STRM) 1배속 재생기. 설명은 NitroStrm.h
#include "NitroStrm.h"

#include <algorithm>
#include <cstring>

using namespace melonDS;

namespace
{

u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

const int kImaIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const u16 kImaStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};

} // namespace

bool NitroStrm::parse(const u8* rom, u32 romlen, u32 start, u32 size, File& f)
{
    if (size < 0x40 || start > romlen || size > romlen - start) return false;
    const u8* h = rom + start;
    if (memcmp(h, "STRM", 4) != 0 || memcmp(h + 0x10, "HEAD", 4) != 0) return false;
    f.base = rom;
    f.baseLen = romlen;
    f.start = start;
    f.end = start + size;
    f.type = h[0x18];
    f.loop = h[0x19] != 0;
    f.channels = h[0x1A];
    f.rate = rd16(h + 0x1C);
    f.loopStart = rd32(h + 0x20);
    f.samples = rd32(h + 0x24);
    f.data = start + rd32(h + 0x28);
    f.blocks = rd32(h + 0x2C);
    f.blockLen = rd32(h + 0x30);
    f.blockSamples = rd32(h + 0x34);
    f.lastLen = rd32(h + 0x38);
    f.lastSamples = rd32(h + 0x3C);
    if (f.type > 2 || f.channels < 1 || f.channels > 2 || f.rate < 1000 || !f.blocks || !f.blockSamples) return false;
    const u64 dataEnd = (u64)(f.data - start) + (u64)(f.blocks - 1) * f.blockLen * f.channels + (u64)f.lastLen * f.channels;
    if (dataEnd > size) return false;
    if (f.loopStart >= f.samples) f.loop = false;
    return true;
}

void NitroStrm::play(const File* f)
{
    if (f == cur && !stopping) return;
    if (cur && !stopping)
    {
        // 지금 소리를 짧게 줄인 뒤 바꾼다 (뚝 끊기는 소리 방지)
        stopping = true;
        fadeLen = fadeLeft = 1;  // 실제 길이는 render 에서 출력 표본율로 정한다
        fadeLeft = -1;
        pending = f;
        return;
    }
    if (cur && stopping)
    {
        pending = f;
        return;
    }
    cur = f;
    pending = nullptr;
    stopping = false;
    pos = 0;
    block = 0xFFFFFFFF;
    frac = 0;
    primed = false;
}

bool NitroStrm::decodeBlock(u32 b)
{
    const File& f = *cur;
    if (b >= f.blocks) return false;
    const bool last = b == f.blocks - 1;
    const u32 len = last ? f.lastLen : f.blockLen;
    const u32 n = last ? f.lastSamples : f.blockSamples;
    const u32 row = f.data + b * f.blockLen * f.channels;
    for (int c = 0; c < f.channels; c++)
    {
        const u32 off = row + c * len;
        if (off + len > f.baseLen) return false;
        const u8* p = f.base + off;
        std::vector<s16>& out = buf[c];
        out.assign(n, 0);
        if (f.type == 0)
        {
            for (u32 i = 0; i < n && i < len; i++) out[i] = (s16)((s8)p[i] * 256);
        }
        else if (f.type == 1)
        {
            for (u32 i = 0; i < n && i * 2 + 1 < len; i++) out[i] = (s16)rd16(p + i * 2);
        }
        else
        {
            if (len < 4) return false;
            s32 pred = (s16)rd16(p);
            int idx = std::clamp((int)rd16(p + 2), 0, 88);
            for (u32 i = 0; i < n; i++)
            {
                const u32 bi = 4 + i / 2;
                if (bi >= len) break;
                const int nib = (i & 1) ? (p[bi] >> 4) : (p[bi] & 15);
                const int step = kImaStep[idx];
                int diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                pred = (nib & 8) ? pred - diff : pred + diff;
                pred = std::clamp(pred, -0x7FFF, 0x7FFF);
                idx = std::clamp(idx + kImaIndex[nib & 7], 0, 88);
                out[i] = (s16)pred;
            }
        }
    }
    block = b;
    return true;
}

bool NitroStrm::fetch(s32* s)
{
    const File& f = *cur;
    if (pos >= f.samples)
    {
        if (!f.loop) return false;
        pos = f.loopStart;
    }
    const u32 b = pos / f.blockSamples;
    if (b != block && !decodeBlock(b)) return false;
    const u32 i = pos - b * f.blockSamples;
    for (int c = 0; c < 2; c++)
    {
        const std::vector<s16>& v = buf[f.channels == 2 ? c : 0];
        s[c] = i < v.size() ? v[i] : 0;
    }
    pos++;
    return true;
}

void NitroStrm::render(s16* out, int frames, int rate)
{
    if (rate <= 0) return;
    for (int i = 0; i < frames; i++)
    {
        if (!cur)
        {
            if (!pending) return;
            const File* f = pending;
            pending = nullptr;
            play(f);
        }
        if (!primed)
        {
            if (!fetch(s0) || !fetch(s1))
            {
                cur = nullptr;
                continue;
            }
            primed = true;
        }
        float gain = userGain;
        if (stopping)
        {
            if (fadeLeft < 0) fadeLen = fadeLeft = std::max(rate / 50, 1);  // 20ms
            gain = userGain * (float)fadeLeft / (float)fadeLen;
            if (--fadeLeft <= 0)
            {
                cur = nullptr;
                stopping = false;
                primed = false;
                continue;
            }
        }
        // 하드웨어와 같은 식: (표본 << shift) * 음량, 팬으로 나눔, 주 음량, >> 8
        s32 l = 0, r = 0;
        for (int c = 0; c < cur->channels; c++)
        {
            const float v = (float)s0[c] + (float)(s1[c] - s0[c]) * (float)frac;
            const Mix& m = mix[c];
            const double val = (double)v * (double)(1 << m.shift) * m.volume;
            l += (s32)(val * (128 - m.pan) / 1024.0);
            r += (s32)(val * m.pan / 1024.0);
        }
        l = (s32)(((s64)l * master) >> 7) >> 8;
        r = (s32)(((s64)r * master) >> 7) >> 8;
        out[i * 2] = (s16)std::clamp((s32)out[i * 2] + (s32)(l * gain), -32768, 32767);
        out[i * 2 + 1] = (s16)std::clamp((s32)out[i * 2 + 1] + (s32)(r * gain), -32768, 32767);

        frac += (double)cur->rate / rate;
        while (frac >= 1.0)
        {
            frac -= 1.0;
            s0[0] = s1[0];
            s0[1] = s1[1];
            if (!fetch(s1))
            {
                cur = nullptr;
                primed = false;
                break;
            }
        }
    }
}
