// ff-bgm1x: 게임 소리 목록과 사용자 분류 (melonDS·DeSmuME 판 SoundCatalog 와 같은 사양).
//
// 게임에서 소리가 나면 그 소리의 이름표(key)를 구해 "나온 소리" 목록에 넣고, 사용자가 정한 분류가 있으면 그것을 쓴다.
// 사용자 분류는 게임별 텍스트 파일(실행 파일 옆 ff-bgm1x-sounds\<타이틀 ID>.txt)에 저장하고,
// 그대로 다른 사람과 주고받을 수 있다 (같은 게임이면 이름표가 같다).
//
// 이름표:
//   "strm:<RomFS 파일 이름>"            스트림 곡 (.bcstm, .aac). 이름의 빈칸·#·% 는 %20·%23·%25 로 적는다
//   "wave:<아카이브>:<묶음>:<파형>"      사운드 아카이브(.bcsar) 의 파형 묶음(CWAR) 안 파형(CWAV).
//                                        아카이브 = RomFS 에서 나오는 .bcsar 순서, 묶음 = 아카이브 안 번호
//
// 창(화면 스레드)과 에뮬레이션 스레드에서 같이 부르므로 안에서 잠근다.
// 분류 창이 닫혀 있고 사용자 지정도 없으면 Active() 가 false 라 에뮬레이션 쪽은 아무것도 하지 않는다.
#pragma once

#include <cmath>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"

namespace AudioCore::HLE {

class SoundCatalog {
public:
    enum Kind {
        Kind_Unknown = -2,  // 아직 게임에서 나오지 않아 자동 판단을 모름 (저장된 지정만 있는 소리)
        Kind_Auto = -1,     // 사용자 지정 없음 (자동 판단)
        Kind_None = 0,      // 아무것도 아님 (원본 그대로)
        Kind_Bgm = 1,       // BGM (1배속)
        Kind_Jingle = 2,    // 팡파레·대사 (1배속으로 끝까지)
        Kind_Cutscene = 3,  // 연출 (원본 그대로, 나오는 동안 배속 멈춤)
        Kind_Sfx = 4,       // 효과음 (1배속, "대사·효과음 1배속" 옵션을 따름)
    };
    enum Type {
        Type_Stream = 0,
        Type_Wave = 1,
    };

    // 게임에서 나온 소리 하나 (화면의 목록 한 줄)
    struct Seen {
        std::string key;
        int type = Type_Stream;
        std::string name;
        int auto_kind = Kind_None;  // 에뮬레이터가 자동으로 정한 분류
        int user_kind = Kind_Auto;
        int volume = 100;           // 1배속으로 낼 때의 볼륨 (%)
        int count = 0;
        u32 order = 0;              // 처음 나온 순서
    };

    static constexpr int kMaxVolume = 200;
    // 볼륨(%)을 소리 크기 배율로: 들리는 크기가 퍼센트에 비례하게 한다
    // (50% = 절반으로 들림 = 약 -10dB, 200% = 두 배로 들림 = 약 +10dB, 0% = 무음)
    static float PerceivedGain(float percent) {
        return percent <= 0.0f ? 0.0f : std::pow(percent / 100.0f, 1.661f);
    }

    // 게임이 바뀌면 그 게임의 파일을 읽는다 (같은 게임이면 아무것도 안 함). 에뮬레이션 스레드
    void SetGame(u64 program_id, const std::string& title);
    std::string GameCode() const;
    std::string GameTitle() const;

    // 분류 창이 열려 있거나 사용자 지정이 하나라도 있으면 true (가볍게 읽는다)
    bool Active() const {
        return active.load(std::memory_order_relaxed);
    }
    // 사용자 지정이 바뀔 때마다 늘어난다 (에뮬레이션 쪽 캐시를 새로 읽으라는 표시)
    u32 UserVersion() const {
        return user_version.load(std::memory_order_relaxed);
    }
    bool HasWaveEntries() const;  // 파형(wave:) 지정이 있는지

    // 나온 소리를 목록에 넣고(수집 중일 때), 사용자 분류가 있으면 그것을, 없으면 auto_kind 를 돌려준다.
    // volume 에는 1배속 볼륨(%) 을 넣어 준다
    int NoteSeen(const std::string& key, int type, const std::string& name, int auto_kind, int* volume);
    // 목록에 넣지 않고 사용자 지정만 읽는다 (Kind_Auto / 100 이면 지정 없음)
    void Lookup(const std::string& key, int* kind, int* volume) const;
    void SetCollecting(bool on);
    bool Collecting() const;

    // --- 화면 스레드 ---
    std::vector<Seen> SeenList() const;
    u32 SeenVersion() const;  // 목록이 바뀔 때마다 늘어난다
    int UserKind(const std::string& key) const;
    int Volume(const std::string& key) const;
    void SetUserKind(const std::string& key, int kind);  // Kind_Auto = 자동 판단으로
    void SetVolume(const std::string& key, int percent);
    void ResetUser(const std::string& key);  // 분류·볼륨 모두 기본값으로
    void ClearSeen();

    std::string StoragePath() const;
    bool Save() const;
    bool ImportFrom(const std::string& path, std::string& error);
    bool ExportTo(const std::string& path) const;

    static const char* KindName(int kind);  // 화면 표시용
    static const char* KindTag(int kind);   // 파일에 쓰는 글자
    static int KindFromTag(const std::string& tag);
    static int TypeOfKey(const std::string& key);
    // 이름표에 쓰는 파일 이름 (빈칸·#·% 를 바꿔 적음)
    static std::string EscapeName(const std::string& name);

private:
    struct UserEntry {
        int kind = Kind_Auto;
        int volume = 100;
        std::string name;  // 파일에 함께 적는 이름 (읽기 쉽게)
    };
    bool ParseFile(const std::string& text, std::map<std::string, UserEntry>& out,
                   std::string& error) const;
    void UpdateUserLocked(const std::string& key, UserEntry e);
    void AddUserEntriesLocked();
    void UpdateActiveLocked();
    std::string FileText() const;
    bool LoadLocked();

    mutable std::mutex lock;
    std::string code, title;  // code = 타이틀 ID 16자리
    u64 program_id = 0;
    bool has_game = false;
    std::map<std::string, UserEntry> user;  // 이름표 -> 사용자 지정
    std::vector<Seen> seen;
    std::unordered_map<std::string, std::size_t> seen_index;
    u32 version = 0;
    bool collect = false;  // 분류 창이 열려 있을 때만 켠다
    std::atomic<bool> active{false};
    std::atomic<u32> user_version{0};
};

} // namespace AudioCore::HLE
