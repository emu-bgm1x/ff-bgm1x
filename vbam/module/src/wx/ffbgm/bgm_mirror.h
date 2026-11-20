// 게임의 BGM 플레이어(MusicPlayerInfo) 상태를 외부 재생기에 반영하고, 원본 BGM 을 무음 처리한다.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "mp2k_engine.h"
#include "rom.h"
#include "sound_catalog.h"

constexpr uint32_t kMp2kIdent = 0x68736D53;  // 'Smsh'
constexpr uint32_t kSoundInfoPtr = 0x03007FF0;

// 에뮬레이터 메모리 접근 (버스 주소)
struct GbaBus {
    virtual ~GbaBus() = default;
    virtual uint8_t r8(uint32_t a) = 0;
    virtual uint16_t r16(uint32_t a) { return (uint16_t) (r8(a) | (r8(a + 1) << 8)); }
    virtual uint32_t r32(uint32_t a) { return r16(a) | ((uint32_t) r16(a + 2) << 16); }
    virtual void w8(uint32_t a, uint8_t v) = 0;
    // 여러 바이트 읽기 (울음소리 등 RAM 곡을 복사할 때)
    virtual void read(uint32_t a, uint8_t* out, size_t n) {
        for (size_t i = 0; i < n; ++i) out[i] = r8(a + (uint32_t) i);
    }
};

// 게임이 RAM 에 만든 곡(울음소리 등)이 쓰는 데이터(곡 머리, 트랙, 악기표, 파형)만 따라가며 복사한다.
// 따라가다 상한을 넘으면 RAM 전체를 복사한다
std::shared_ptr<const RamImage> copyRamSong(GbaBus& bus, uint32_t header);

struct BgmSnapshot {
    uint32_t header = 0;
    uint32_t status = 0;
    uint32_t clock = 0;
    uint16_t fadeOI = 0;
    uint16_t fadeOC = 0;
    uint16_t fadeOV = 0;
};

struct SoundInfoSnapshot {
    bool valid = false;
    int reverb = 0, maxChans = 0, masterVolume = 0, dmaPeriod = 0, samplesPerVBlank = 0;
};

// gMPlayTable 후보 중 실행 중 RAM 과 일치하는 첫 항목(0번 = BGM)의 MusicPlayerInfo 주소. 없으면 0.
uint32_t findBgmPlayerInfo(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands);
bool isPlayerInfoValid(GbaBus& bus, uint32_t info);

BgmSnapshot readBgm(GbaBus& bus, uint32_t info);
SoundInfoSnapshot readSoundInfo(GbaBus& bus);

// 원본 BGM 트랙의 vol/volX 를 0 으로 만들고 볼륨 재계산 플래그를 세운다.
void muteBgmTracks(GbaBus& bus, uint32_t info);
// 게임이 재생 중인 곡을 처음부터 다시 시작하게 한다 (무음 처리를 풀 때: 트랙 볼륨을 곡이 다시 정한다)
void restartGameSong(GbaBus& bus, uint32_t info);

// 곡 데이터를 읽어, 앞으로 되돌아가는 GOTO(반복 구간)가 있으면 true. 배경음악은 반복하고 효과음은 반복하지 않는다.
bool isLoopingSong(const Rom& rom, uint32_t header);

// 음악용 플레이어 고르기. 게임마다 BGM 플레이어 번호가 다르므로(0번인 게임, 1번인 게임 등)
// 트랙 수가 큰 플레이어들을 음악 후보로 보고, 가장 최근에 곡을 시작해 재생 중인 플레이어를 따라간다.
class BgmPlayerSelector {
public:
    // 실행 중 RAM 과 맞는 테이블을 찾으면 true. 후보가 정해진다.
    bool locate(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands);
    bool located() const { return !m_players.empty(); }
    bool stillValid(GbaBus& bus) const;
    void reset();

    // 프레임마다 호출. 따라갈 플레이어(없으면 0)와 그 상태를 돌려준다. 플레이어가 바뀌면 changed=true.
    // 다른 플레이어로 옮겨 가는 것은 그 플레이어가 반복하는 곡(배경음악)을 새로 시작했을 때뿐이다.
    uint32_t update(GbaBus& bus, const Rom& rom, BgmSnapshot& snap, bool& changed);
    const std::vector<uint32_t>& players() const { return m_players; }

private:
    struct State {
        uint32_t header = 0xFFFFFFFF;
        uint32_t clock = 0;
        uint64_t startedAt = 0;
    };
    std::vector<uint32_t> m_players;
    std::vector<State> m_state;
    uint32_t m_active = 0;
    uint64_t m_frame = 0;
    std::unordered_map<uint32_t, bool> m_loopCache;
};

// 프레임마다 받은 상태로 재생기를 시작/일시정지/재개한다.
class BgmMirror {
public:
    explicit BgmMirror(Mp2kEngine& eng) : eng_(eng) {}
    void update(const BgmSnapshot& s);
    // 게임이 같은 곡을 다시 틀었지만 재생기는 처음부터 다시 틀지 않을 때: 지금 상태만 기억한다
    void follow(const BgmSnapshot& s) {
        last_ = s;
        have_ = true;
    }
    void reset();

private:
    Mp2kEngine& eng_;
    bool have_ = false;
    BgmSnapshot last_;
};

// 곡의 대략적인 길이(초). 반복 곡은 첫 반복 지점까지. 트랙 중 가장 긴 것 기준
double songSeconds(const Rom& rom, uint32_t header);

// 곡이 음악인지: 반복 구간이 있거나(배경음악) 트랙이 4개 이상(팡파르 등)이면 음악으로 본다.
// 효과음은 대부분 트랙 1~3개에 반복이 없다.
bool isMusicSong(const Rom& rom, uint32_t header);

// 트랙이 많은 플레이어마다 대체 재생기를 하나씩 둔다. 각 플레이어가 음악을 재생하는 동안만
// 그 플레이어를 무음 처리하고 대체 재생하며, 효과음은 원본 그대로 둔다.
class BgmMultiMirror {
public:
    explicit BgmMultiMirror(const Rom* rom) : rom_(rom) {}

    bool locate(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands);  // 에뮬레이션 스레드
    bool located() const { return !players_.empty(); }
    bool stillValid(GbaBus& bus) const;
    void reset();                                                            // 재생기 모두 제거
    void stopAll();
    // 에뮬레이션 스레드: 대체 재생을 멈추고, 무음 처리하던 게임 곡을 처음부터 다시 시작시킨다
    void releaseMute(GbaBus& bus);

    void configure(const EngineConfig& cfg);  // 출력 레이트 등 (곡은 다시 시작)
    void setRuntimeParams(const SoundInfoSnapshot& si);

    // 에뮬레이션 스레드: 프레임마다. mute=false 면 무음 처리 없이 상태만 따라간다 (녹화용 두 번째 인스턴스)
    void onFrame(GbaBus& bus, bool mute);
    // 출력 스레드: 모든 재생기를 섞어 out 에 쓴다
    void render(float* out, int frames);

    // 연출 곡(반복 없는 긴 곡, 대체하지 않음)을 게임이 재생 중인지
    bool cutsceneActive() const { return cutscene_; }

    // 대체 중인 플레이어(1배속으로 대신 내는 음악·효과음)가 쓰는 PSG(CGB) 채널. 비트 i = 채널 i+1.
    // 트랙 볼륨을 0 으로 바꿔도 이미 시작된 PSG 소리는 몇 프레임 더 울리므로, 이 채널들은 에뮬레이터 출력에서 끈다.
    // onFrame(mute=true) 끝에 갱신된다 (에뮬레이션 스레드)
    uint8_t replacedPsgMask() const { return psgMask_; }

    // "효과음 1배속": 트랙이 적은 플레이어(효과음용)가 내는 반복 없는 소리도 1배속으로 끝까지 낸다
    // (다음 소리부터 적용). 곡 머리가 롬에 없는 소리(울음소리 등)는 대상이 아니다
    void setSfx1x(bool on) { sfx1x_ = on; }
    // 1배속으로 내는 효과음의 볼륨 (0~1)
    void setSfxVolume(float gain) { sfxGain_ = gain; }

    // 곡 종류
    enum Kind { Kind_Sound = 0, Kind_Music, Kind_Jingle, Kind_Cutscene };

    // ---- ff-bgm1x 소리 분류 (sound_catalog.h). 카탈로그가 없으면(레무로이드 등) 자동 판단만 쓴다 ----
    // note = 이 인스턴스가 나온 소리를 목록에 넣는다 (녹화용 두 번째 인스턴스는 false: 지정만 읽는다)
    void setCatalog(SoundCatalog* catalog, bool note) {
        catalog_ = catalog;
        note_ = note;
    }
    // 곡 번호 (findSongNumbers 결과, 이름 표시용). 가리키는 표는 이 객체보다 오래 살아야 한다
    void setSongNumbers(const std::unordered_map<uint32_t, int>* numbers) { songNumbers_ = numbers; }
    // "RAM 곡 읽기": 게임이 RAM 에 만들어 내는 곡(울음소리 등)을 사용자가 1배속으로 정했을 때,
    // 소리를 시작하는 순간의 RAM 을 복사해 재생기로 낸다 (꺼져 있으면 언제나 게임 속도)
    void setRamSongs(bool on) { ramSongs_ = on; }
    // 기능이 꺼져 있고 분류 창만 열려 있을 때: 게임이 새로 낸 소리를 목록에만 넣는다 (에뮬레이션 스레드)
    void noteOnly(GbaBus& bus);
    // 미리 듣기용 (아무 스레드): 마지막으로 읽은 사운드 정보, 마지막 RAM 곡의 머리와 RAM 사본
    SoundInfoSnapshot lastSoundInfo() const;
    bool lastRamSong(uint32_t& header, std::shared_ptr<const RamImage>& ram) const;
    // 이름표·이름
    static bool isRamSong(uint32_t header) { return header >= 0x02000000u && header < 0x04000000u; }
    static std::string songKey(uint32_t header);
    std::string songName(uint32_t header) const;

private:
    struct Player {
        uint32_t info = 0;
        std::unique_ptr<Mp2kEngine> eng;
        std::unique_ptr<BgmMirror> mirror;
        uint32_t header = 0xFFFFFFFF;
        bool music = false;
        int kind = Kind_Sound;
        int engKind = Kind_Sound;  // 재생기가 지금 내고 있는 곡의 종류
        bool gameDone = false;     // 게임에서는 팡파레가 이미 끝남
        bool orphan = false;       // 게임은 이 플레이어로 다른 소리를 내지만, 재생기는 팡파레를 끝까지 냄
        bool se = false;           // 효과음용 플레이어 (트랙이 적음)
        uint8_t volX[16] = {};     // 게임이 트랙 볼륨 조절로 넣은 마지막 값 (0 = 아직 없음)
        uint32_t deferHeader = 0;  // 1배속 팡파레가 끝날 때까지 미뤄 둔 곡 (팡파레 전에 나오던 BGM 으로 돌아가는 경우)
        // 곡 목록에 없는 플레이어 (울음소리 전용 등. 사운드 정보의 플레이어 연결 목록으로 찾음).
        // 사용자가 분류 창에서 정하기 전에는 원본 그대로 둔다
        bool extra = false;
        bool seOn = false;         // 지금 게임이 내는 효과음을 대체 중
        uint32_t seHeader = 0;     // 효과음: 지난 프레임에 본 곡 머리와 clock (새로 냈는지 가린다)
        uint32_t seClock = 0;
        // 재생기가 효과음 방식으로 내고 있는 소리: 곡 머리, 시작한 게임 프레임·실제 시각, 멈출 실제 시각 (게임이 멈춘 뒤)
        uint32_t seLive = 0;
        uint32_t seStartFrame = 0;
        std::chrono::steady_clock::time_point seStartWall{};
        bool seStopping = false;
        std::chrono::steady_clock::time_point seStopAt{};
        uint32_t seStopFrame = 0;  // 스스로 반복하는 소리: 게임이 멈춘 게임 프레임
        // 음악 방식: 지난 프레임에 본 곡 머리와 clock (같은 곡을 다시 틀었는지 가린다)
        uint32_t musicHeader = 0;
        uint32_t musicClock = 0;
        // 소리 분류: 지금 곡의 사용자 지정(SoundCatalog::Kind_), 볼륨 배율
        int user = SoundCatalog::Kind_Auto;
        float gain = 1.0f;
        // 나온 횟수 세기: 마지막으로 본 곡·clock, 새로 시작했는데 아직 울리기 전
        uint32_t cntHeader = 0xFFFFFFFF;
        uint32_t cntClock = 0;
        bool cntPending = false;
        // 원본 그대로 나오는 소리의 볼륨: 트랙별 게임이 정한 volX 와 우리가 쓴 값 (-1 = 모름)
        bool scaled = false;
        int16_t baseVolX[16];
        int16_t writtenVolX[16];
    };
    int autoCatalogKind(uint32_t header, int kind);
    void lookupUser(Player& p, uint32_t header, int kind, bool count);
    static int effectiveKind(int kind, int user);
    bool wantSe(const Player& p, GbaBus& bus, uint32_t header);
    // 스스로 반복하는 짧은 소리 (낮은 체력 경고음 등): BGM·팡파레로 정해도 효과음 방식으로 낸다
    bool isShortLoop(uint32_t header);
    std::unordered_map<uint32_t, bool> shortLoopCache_;
    // 스스로 반복하는 소리인지와 반복 한 바퀴 길이(초, 게임 시간). 반복하지 않으면 0
    double selfLoopSeconds(uint32_t header);
    std::unordered_map<uint32_t, double> selfLoopCache_;
    // 같은 소리를 게임이 되풀이할 때의 간격 (곡 머리별): 마지막으로 게임이 튼 프레임, 마지막으로 1배속으로 낸 실제 시각.
    // 배속 중에도 되풀이 간격이 1배속이 되게, 게임 안 간격보다 실제 시간이 덜 지났으면 이번 것은 내지 않는다
    struct Repeat {
        uint32_t lastTrigFrame = 0;
        std::chrono::steady_clock::time_point lastPlay{};
        bool played = false;
    };
    std::unordered_map<uint32_t, Repeat> repeat_;
    bool allowRepeat(uint32_t header);
    // 게임이 멈춘 효과음은 게임 안에서 울린 시간만큼 1배속으로 낸 뒤 멈춘다
    void seStopCheck(Player& p, const BgmSnapshot& s);
    uint32_t frame_ = 0;  // 게임 프레임 수 (onFrame 마다)
    bool validRamSong(GbaBus& bus, uint32_t header) const;
    void startSe(GbaBus& bus, Player& p, uint32_t header);
    void applyNativeGain(GbaBus& bus, Player& p, float gain);
    void followVolumeControl(GbaBus& bus, Player& p);
    void refreshUser(GbaBus& bus);
    void countStart(Player& p, const BgmSnapshot& s);
    bool listable(uint32_t header) const;
    SoundCatalog* catalog_ = nullptr;
    bool note_ = false;
    uint32_t userVer_ = 0xFFFFFFFF;
    const std::unordered_map<uint32_t, int>* songNumbers_ = nullptr;
    std::atomic<bool> ramSongs_{false};
    mutable std::mutex lastMtx_;
    SoundInfoSnapshot lastSi_;
    uint32_t lastRamHeader_ = 0;
    std::shared_ptr<const RamImage> lastRam_;
    int classify(uint32_t header);
    bool isSfxSong(uint32_t header);
    void onSeFrame(GbaBus& bus, Player& p, bool mute);
    void seStep(GbaBus& bus, Player& p, const BgmSnapshot& s, bool mute);
    bool jingleLagging(const Player& p) const;
    bool anyJingleLagging(const Player* except) const;
    void cutLaggingJingles(const Player* except);
    uint8_t computePsgMask(GbaBus& bus, bool seStarted) const;
    const Rom* rom_;
    EngineConfig cfg_;
    std::mutex mtx_;
    std::vector<Player> players_;
    std::atomic<bool> sfx1x_{true};
    std::atomic<float> sfxGain_{1.0f};
    std::vector<float> tmp_;
    std::unordered_map<uint32_t, bool> musicCache_;
    std::unordered_map<uint32_t, int> kindCache_;
    std::atomic<bool> cutscene_{false};
    uint8_t psgMask_ = 0;
    uint32_t lastMusic_ = 0;     // 마지막으로 시작한 음악(반복 곡)
    uint32_t jingleBefore_ = 0;  // 마지막 팡파레가 시작되기 전에 나오던 음악
    bool seStarted_ = false;  // 이번 프레임에 대체할 효과음을 게임이 새로 냈다 (아직 채널이 정해지지 않음)
};
