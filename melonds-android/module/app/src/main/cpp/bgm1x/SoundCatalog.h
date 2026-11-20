// ff-bgm1x: 게임 소리 목록과 사용자 분류.
//
// 롬의 사운드 데이터(SDAT)마다 시퀀스(SSEQ), 효과음 묶음(SSAR) 안의 소리, 스트림(STRM)을 색인해 둔다.
// 게임에서 소리가 나면 그 소리의 이름표(key)를 구해 "나온 소리" 목록에 넣고, 사용자가 정한 분류가 있으면 그것을 쓴다.
// 사용자 분류는 게임별 텍스트 파일에 저장하고, 그대로 다른 사람과 주고받을 수 있다 (같은 롬이면 이름표가 같다).
//
// 이름표: "seq:<SDAT 번호>:<시퀀스 번호>", "ssar:<SDAT 번호>:<묶음 번호>:<묶음 안 순번>", "strm:<SDAT 번호>:<번호>"
// (SDAT 번호 = 롬 안에서 나오는 순서)
//
// 에뮬레이터와 무관한 코드라 melonDS 와 DeSmuME 가 같이 쓴다. 여러 스레드(에뮬레이션, 화면)에서 부르므로 안에서 잠근다.
#ifndef SOUNDCATALOG_H
#define SOUNDCATALOG_H

#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef FFBGM_DESMUME
#include "ffbgm_glue.h"
#else
#include "types.h"
#endif

class SoundCatalog
{
public:
    // 분류. 값은 BgmReplacer 의 Kind_ 와 같다
    enum Kind
    {
        Kind_Unknown = -2,  // 아직 게임에서 나오지 않아 자동 판단을 모름 (저장된 지정만 있는 소리)
        Kind_Auto = -1,     // 사용자 지정 없음 (자동 판단)
        Kind_None = 0,      // 아무것도 아님 (원본 그대로)
        Kind_Bgm = 1,       // BGM (1배속)
        Kind_Jingle = 2,    // 팡파레·대사 (1배속으로 끝까지)
        Kind_Cutscene = 3,  // 연출 (원본 그대로, 나오는 동안 배속 멈춤)
        Kind_Sfx = 4,       // 효과음 (1배속, "효과음 1배속" 옵션을 따름)
    };
    enum Type
    {
        Type_Seq = 0,
        Type_Ssar = 1,
        Type_Strm = 2,
    };

    struct SeqEntry
    {
        int sdat = 0, index = 0;
        melonDS::u32 fileOff = 0, fileSize = 0;  // 롬 위치
        int bank = -1, volume = 127, player = 0;
        std::string name;
    };
    struct SsarSeq
    {
        melonDS::u32 offset = 0;  // 묶음 데이터 안 시작 위치
        int bank = -1, volume = 127;
        std::string name;
    };
    struct SsarArc
    {
        int sdat = 0, index = 0;
        melonDS::u32 fileOff = 0, fileSize = 0, dataOff = 0;  // dataOff: 파일 안 데이터 시작 위치
        std::string name;
        std::vector<SsarSeq> seqs;
    };
    struct StrmEntry
    {
        int sdat = 0, index = 0;
        melonDS::u32 fileOff = 0, fileSize = 0, dataOff = 0;
        int volume = 127;
        bool loop = false;
        std::string name;
    };
    struct BankEntry
    {
        melonDS::u32 fileOff = 0, fileSize = 0;
        int warc[4] = {-1, -1, -1, -1};
    };
    struct FileRange
    {
        melonDS::u32 fileOff = 0, fileSize = 0;
    };
    struct Sdat
    {
        std::vector<SeqEntry> seqs;      // INFO 번호 순 (없는 번호는 fileSize 0)
        std::vector<SsarArc> ssars;
        std::vector<StrmEntry> strms;
        std::vector<BankEntry> banks;
        std::vector<FileRange> warcs;
    };

    // 게임에서 나온 소리 하나 (화면의 목록 한 줄)
    struct Seen
    {
        std::string key;
        int type = Type_Seq;
        std::string name;
        int autoKind = Kind_None;  // 에뮬레이터가 자동으로 정한 분류
        int userKind = Kind_Auto;
        int volume = 100;          // 1배속으로 낼 때의 볼륨 (%)
        int count = 0;
        melonDS::u32 order = 0;    // 처음 나온 순서
    };

    // 롬을 색인한다. gameCode 는 롬 머리 0x0C 의 4글자, title 은 0x00 의 12글자
    // rom 이 그대로 살아 있으면 keep = true (그 메모리에서 읽는다). 아니면 setReader 로 읽는 방법을 준다
    void index(const melonDS::u8* rom, melonDS::u32 romlen, bool keep = true);
    using Reader = std::function<bool(melonDS::u32 off, melonDS::u32 len, melonDS::u8* out)>;
    void setReader(Reader r) { reader = std::move(r); }
    // 롬의 off 부터 len 바이트 (재생·STRM 1배속용)
    bool readRom(melonDS::u32 off, melonDS::u32 len, std::vector<melonDS::u8>& out) const;
    const std::string& gameCode() const { return code; }
    const std::string& gameTitle() const { return title; }

    // --- 소리 알아내기 (에뮬레이션 스레드 전용. 롬 목록은 index 를 부르는 에뮬레이션 스레드만 바꾸므로 잠그지 않는다:
    //     카트를 읽을 때마다 불리므로 가볍게) ---
    // SSEQ 파일 내용(CRC32)으로 시퀀스를 찾는다. 없으면 nullptr
    const SeqEntry* findSeq(melonDS::u32 crc) const;
    // 효과음 묶음: data = 메모리의 묶음 데이터 시작, avail = 읽을 수 있는 길이, offset = 시작 위치
    const SsarArc* findSsar(const melonDS::u8* data, melonDS::u32 avail, melonDS::u32 offset, int& seqNo) const;
    // 카트에서 읽은 롬 위치가 STRM 데이터 안이면 그 STRM
    const StrmEntry* findStrmData(melonDS::u32 romAddr) const;
    const Sdat* sdat(int n) const { return n >= 0 && n < (int)sdats.size() ? &sdats[n] : nullptr; }

    static std::string seqKey(const SeqEntry& e);
    static std::string ssarKey(const SsarArc& a, int seqNo);
    static std::string strmKey(const StrmEntry& e);

    // 나온 소리를 목록에 넣고, 사용자 분류가 있으면 그것을, 없으면 autoKind 를 돌려준다
    int noteSeen(const std::string& key, int type, const std::string& name, int autoKind);
    // 수집 중인지 (꺼져 있으면 목록에 새로 넣지 않는다. 사용자 분류는 언제나 적용)
    void setCollecting(bool on);
    bool collecting() const;

    // --- 사용자 분류 (화면 스레드) ---
    std::vector<Seen> seenList() const;
    melonDS::u32 seenVersion() const;  // 목록이 바뀔 때마다 늘어난다
    int userKind(const std::string& key) const;
    void setUserKind(const std::string& key, int kind);  // Kind_Auto = 자동 판단으로
    // 1배속으로 내는 소리의 볼륨 (%, 100 = 그대로). 원본 그대로 나오는 소리에는 적용되지 않는다
    static constexpr int kMaxVolume = 200;
    // 볼륨(%)을 소리 크기 배율로: 들리는 크기가 퍼센트에 비례하게 한다
    // (50% = 절반으로 들림 = 약 -10dB, 200% = 두 배로 들림 = 약 +10dB, 0% = 무음)
    static float perceivedGain(float percent) {
        return percent <= 0.0f ? 0.0f : std::pow(percent / 100.0f, 1.661f);
    }
    int volume(const std::string& key) const;
    void setVolume(const std::string& key, int percent);
    void resetUser(const std::string& key);  // 분류·볼륨 모두 기본값으로
    void clearSeen();

    // 게임별 파일. dir 아래 <게임 코드>.txt (UTF-8 경로)
    void setStorageDir(const std::string& dir) { storageDir = dir; }
    std::string storagePath() const;
    bool load();
    bool save() const;
    // 다른 파일에서 가져오기 / 내보내기 (공유용, 같은 형식)
    bool importFrom(const std::string& path, std::string& error);
    bool exportTo(const std::string& path) const;

    static const char* kindName(int kind);   // 화면 표시용
    static const char* kindTag(int kind);    // 파일에 쓰는 글자
    static int kindFromTag(const std::string& tag);

private:
    struct UserEntry
    {
        int kind = Kind_Auto;
        int volume = 100;
        std::string name;  // 파일에 함께 적는 이름 (읽기 쉽게)
    };
    bool parseFile(const std::string& text, std::map<std::string, UserEntry>& out, std::string& error) const;
    void updateUserLocked(const std::string& key, UserEntry e);
    // 사용자 지정이 있는데 목록에 없는 소리를 목록에 넣는다 (게임을 다시 켜도 지정한 소리가 보이게). 잠근 채로 부른다
    void addUserEntriesLocked();
    std::string fileText() const;

    mutable std::mutex lock;
    const melonDS::u8* rom = nullptr;  // keep 일 때만
    melonDS::u32 romlen = 0;
    Reader reader;
    std::string code, title;
    std::vector<Sdat> sdats;
    std::unordered_map<melonDS::u32, std::pair<int, int>> seqByCrc;     // CRC32 -> (SDAT, 번호)
    std::unordered_map<melonDS::u32, std::pair<int, int>> ssarByHead;  // 묶음 데이터 앞부분 CRC32 -> (SDAT, 번호)
    struct StrmRange
    {
        melonDS::u32 data, end;
        int sdat, index;
    };
    std::vector<StrmRange> strmRanges;  // data 순

    std::map<std::string, UserEntry> user;  // 이름표 -> 사용자 지정
    std::vector<Seen> seen;
    std::unordered_map<std::string, size_t> seenIndex;
    melonDS::u32 version = 0;
    bool collect = false;  // 분류 창이 열려 있을 때만 켠다
    std::string storageDir;
};

#endif // SOUNDCATALOG_H
