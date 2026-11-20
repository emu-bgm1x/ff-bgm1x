// ff-bgm1x: 게임 소리 목록과 사용자 분류 (melonDS·DeSmuME·Azahar 판과 같은 사양).
//
// 게임에서 소리가 나면 그 소리의 이름표(key)를 구해 "나온 소리" 목록에 넣고, 사용자가 정한 분류가 있으면 그것을 쓴다.
// 사용자 분류는 게임별 텍스트 파일(<저장 폴더>/<게임 코드>.txt)에 저장하고, 그대로 다른 사람과 주고받을 수 있다.
//
// 이름표:
//   "song:<곡 머리 주소 8자리 16진수>"   GBA MP2K 곡 (롬 안 곡 머리)
//   "cry:all"                           울음소리 전체 (GBA: 게임이 RAM 에 만들어 내는 곡, GB: 울음소리 표시가 켜진 효과음 채널)
//   "gbsong:<번호>", "gbsfx:<번호>"     GB/GBC 곡·효과음 번호
//
// 화면 스레드와 에뮬레이션 스레드에서 같이 부르므로 안에서 잠근다.
// 분류 창이 닫혀 있고 사용자 지정도 없으면 Active() 가 false 라 에뮬레이션 쪽은 아무것도 하지 않는다.
// 표준 C++ 만 쓴다 (안드로이드 레무로이드 코어에도 같이 빌드됨. 거기서는 쓰지 않는다).
#pragma once

#include <cmath>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class SoundCatalog {
public:
    enum Kind {
        Kind_Unknown = -2,  // 아직 게임에서 나오지 않아 자동 판단을 모름 (저장된 지정만 있는 소리)
        Kind_Auto = -1,     // 사용자 지정 없음 (자동 판단)
        Kind_None = 0,      // 아무것도 아님 (원본 그대로)
        Kind_Bgm = 1,       // BGM (1배속)
        Kind_Jingle = 2,    // 팡파레·대사 (1배속으로 끝까지)
        Kind_Cutscene = 3,  // 연출 (원본 그대로, 나오는 동안 배속 멈춤)
        Kind_Sfx = 4,       // 효과음 (1배속, "효과음 1배속" 옵션을 따름)
    };
    enum Type {
        Type_Song = 0,
        Type_Cry = 1,
        Type_Other = 2,
        Type_Sfx = 3,
    };

    struct Seen {
        std::string key;
        int type = Type_Song;
        std::string name;
        std::string label;          // 사용자가 붙인 이름 (비어 있으면 name 을 보여 준다)
        int auto_kind = Kind_None;  // 에뮬레이터가 자동으로 정한 분류
        int user_kind = Kind_Auto;
        int volume = 100;           // 볼륨 (%)
        int count = 0;
        uint32_t order = 0;         // 처음 나온 순서
    };

    static constexpr int kMaxVolume = 200;
    // 볼륨(%)을 소리 크기 배율로: 들리는 크기가 퍼센트에 비례하게 한다
    // (50% = 절반으로 들림 = 약 -10dB, 200% = 두 배로 들림 = 약 +10dB, 0% = 무음)
    static float PerceivedGain(float percent) {
        return percent <= 0.0f ? 0.0f : std::pow(percent / 100.0f, 1.661f);
    }

    // 실행 파일 옆 ff-bgm1x-sounds 폴더 (UTF-8)
    static std::string DefaultStorageDir();
    // 저장 폴더 (UTF-8). SetGame 전에 정한다
    void SetStorageDir(const std::string& dir);
    // 게임이 바뀌면 그 게임의 파일을 읽는다 (같은 게임이면 아무것도 안 함)
    void SetGame(const std::string& code, const std::string& title);
    std::string GameCode() const;
    std::string GameTitle() const;

    bool Active() const { return active.load(std::memory_order_relaxed); }
    // 사용자 지정이 바뀔 때마다 늘어난다 (에뮬레이션 쪽이 지정을 다시 읽으라는 표시)
    uint32_t UserVersion() const { return user_version.load(std::memory_order_relaxed); }

    // 나온 소리를 목록에 넣고(수집 중일 때) 사용자 지정(분류, 볼륨 %)을 돌려준다
    void NoteSeen(const std::string& key, int type, const std::string& name, int auto_kind, int* kind, int* volume);
    // 목록에 넣지 않고 사용자 지정만 읽는다 (Kind_Auto / 100 이면 지정 없음)
    void Lookup(const std::string& key, int* kind, int* volume) const;
    void SetCollecting(bool on);
    bool Collecting() const;

    // --- 화면 스레드 ---
    std::vector<Seen> SeenList() const;
    uint32_t SeenVersion() const;
    int UserKind(const std::string& key) const;
    int Volume(const std::string& key) const;
    void SetUserKind(const std::string& key, int kind);
    void SetVolume(const std::string& key, int percent);
    // 분류·볼륨을 기본값으로 (사용자가 붙인 이름은 그대로 둔다)
    void ResetUser(const std::string& key);
    // 사용자가 붙이는 이름 (빈 문자열 = 원래 이름으로)
    void SetLabel(const std::string& key, const std::string& label);
    std::string Label(const std::string& key) const;
    void ClearSeen();

    std::string StoragePath() const;
    bool Save() const;
    bool ImportFrom(const std::string& path, std::string& error);
    bool ExportTo(const std::string& path) const;

    static const char* KindName(int kind);
    static const char* KindTag(int kind);
    static int KindFromTag(const std::string& tag);
    static int TypeOfKey(const std::string& key);

private:
    struct UserEntry {
        int kind = Kind_Auto;
        int volume = 100;
        std::string name;
        std::string label;  // 사용자가 붙인 이름
        bool empty() const { return kind == Kind_Auto && volume == 100 && label.empty(); }
    };
    bool ParseFile(const std::string& text, std::map<std::string, UserEntry>& out, std::string& error) const;
    void UpdateUserLocked(const std::string& key, UserEntry e);
    void AddUserEntriesLocked();
    void UpdateActiveLocked();
    std::string FileText() const;
    std::string PathLocked() const;

    mutable std::mutex lock;
    std::string dir, code, title;
    bool has_game = false;
    std::map<std::string, UserEntry> user;
    std::vector<Seen> seen;
    std::unordered_map<std::string, size_t> seen_index;
    uint32_t version = 0;
    bool collect = false;
    std::atomic<bool> active{false};
    std::atomic<uint32_t> user_version{0};
};
