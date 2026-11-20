#include "bgm_mirror.h"

#include <memory>

#include <set>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>

namespace {
constexpr uint32_t STATUS_PAUSE = 0x80000000u;
constexpr uint32_t STATUS_TRACK = 0x0000FFFFu;
constexpr uint32_t TRACK_SIZE = 0x50;
constexpr double kGbaFps = 59.7275;
constexpr double kRepeatWindow = 3.0;  // 이보다 긴 간격(게임 시간, 초)은 되풀이로 보지 않는다
constexpr double kShortLoopSeconds = 3.0;
}  // namespace

bool isPlayerInfoValid(GbaBus& bus, uint32_t info) {
    uint32_t ident = bus.r32(info + 0x34);
    // MPlayMain 실행 중에는 ident 가 1 증가해 있다
    return ident == kMp2kIdent || ident == kMp2kIdent + 1;
}

uint32_t findBgmPlayerInfo(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands) {
    for (const auto& c : cands) {
        const MPlayEntry& e = c.entries[0];
        if (!isPlayerInfoValid(bus, e.info)) continue;
        if (bus.r8(e.info + 8) != e.numTracks) continue;
        if (bus.r32(e.info + 0x2C) != e.tracks) continue;
        return e.info;
    }
    return 0;
}

BgmSnapshot readBgm(GbaBus& bus, uint32_t info) {
    BgmSnapshot s;
    s.header = bus.r32(info);
    s.status = bus.r32(info + 4);
    s.clock = bus.r32(info + 0x0C);
    s.fadeOI = bus.r16(info + 0x24);
    s.fadeOC = bus.r16(info + 0x26);
    s.fadeOV = bus.r16(info + 0x28);
    return s;
}

SoundInfoSnapshot readSoundInfo(GbaBus& bus) {
    SoundInfoSnapshot s;
    uint32_t p = bus.r32(kSoundInfoPtr);
    if (p < 0x02000000u || p >= 0x03008000u) return s;
    uint32_t ident = bus.r32(p);
    if (ident != kMp2kIdent && ident != kMp2kIdent + 1) return s;
    s.valid = true;
    s.reverb = bus.r8(p + 5);
    s.maxChans = bus.r8(p + 6);
    s.masterVolume = bus.r8(p + 7);
    s.dmaPeriod = bus.r8(p + 0x0B);
    s.samplesPerVBlank = (int) bus.r32(p + 0x10);
    return s;
}

void muteBgmTracks(GbaBus& bus, uint32_t info) {
    uint8_t n = bus.r8(info + 8);
    uint32_t tracks = bus.r32(info + 0x2C);
    if (n > 16) n = 16;
    for (uint8_t i = 0; i < n; ++i) {
        uint32_t t = tracks + i * TRACK_SIZE;
        uint8_t flags = bus.r8(t);
        if (!(flags & 0x80)) continue;
        bus.w8(t + 0x12, 0);  // vol
        bus.w8(t + 0x13, 0);  // volX
        bus.w8(t, (uint8_t) (flags | 0x03));  // MPT_FLG_VOLCHG
    }
}

void restartGameSong(GbaBus& bus, uint32_t info) {
    // m4a MPlayStart 와 같은 순서: 트랙을 멈추고(TrackStop) 곡 머리의 트랙 위치부터 다시 시작하게 한다.
    // 트랙 초기화(볼륨 등)는 게임의 MPlayMain 이 MPT_FLG_START 를 보고 한다
    const uint32_t header = bus.r32(info);
    if (header < 0x08000000u || header >= 0x0A000000u) return;
    auto w16 = [&](uint32_t a, uint16_t v) {
        bus.w8(a, (uint8_t) v);
        bus.w8(a + 1, (uint8_t) (v >> 8));
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, (uint16_t) v);
        w16(a + 2, (uint16_t) (v >> 16));
    };
    const uint8_t songTracks = bus.r8(header);
    uint8_t n = bus.r8(info + 8);
    if (n > 16) n = 16;
    const uint32_t tracks = bus.r32(info + 0x2C);
    for (uint8_t i = 0; i < n; ++i) {
        const uint32_t t = tracks + i * TRACK_SIZE;
        if (bus.r8(t) & 0x80) {  // TrackStop
            uint32_t chan = bus.r32(t + 0x20);
            for (int guard = 0; chan && guard < 32; ++guard) {
                const uint8_t flags = bus.r8(chan);
                if (flags) {
                    // GB 음원 채널은 게임이 끄도록 정지 표시만, 다이렉트 사운드 채널은 바로 끈다
                    bus.w8(chan, (bus.r8(chan + 1) & 0x07) ? (uint8_t) (flags | 0x40) : 0);
                }
                w32(chan + 0x2C, 0);           // track
                chan = bus.r32(chan + 0x34);  // nextChannelPointer
            }
            w32(t + 0x20, 0);  // chan
        }
        if (i < songTracks) {
            bus.w8(t, 0xC0);                                 // MPT_FLG_EXIST | MPT_FLG_START
            w32(t + 0x40, bus.r32(header + 8 + 4u * i));  // cmdPtr
        } else {
            bus.w8(t, 0);
        }
    }
    w32(info + 4, 0);                       // status
    bus.w8(info + 9, bus.r8(header + 2));   // priority
    w32(info + 0x0C, 0);                    // clock
    w16(info + 0x1C, 150);                  // tempoD
    w16(info + 0x1E, 0x100);                // tempoU
    w16(info + 0x20, 150);                  // tempoI
    w16(info + 0x22, 0);                    // tempoC
    w16(info + 0x24, 0);                    // fadeOI
    w32(info + 0x30, bus.r32(header + 4));  // tone
}

bool isLoopingSong(const Rom& rom, uint32_t header) {
    const uint8_t n = rom.u8(header);
    if (n == 0 || n > 16 || !rom.valid(header, 8 + 4u * n)) return false;
    for (uint8_t t = 0; t < n; ++t) {
        uint32_t p = rom.u32(header + 8 + 4u * t);
        uint8_t running = 0;
        for (int steps = 0; steps < 20000 && rom.valid(p); ++steps) {
            const uint32_t at = p;
            uint8_t cmd = rom.u8(p);
            if (cmd < 0x80) {
                cmd = running;  // 러닝 스테이터스: 이 바이트는 인자
            } else {
                ++p;
                if (cmd >= 0xBD) running = cmd;
            }
            if (cmd >= 0xCF || cmd == 0xCE) {  // 음표 / EOT: 0x80 미만 인자 최대 3개
                for (int i = 0; i < 3 && rom.u8(p) < 0x80; ++i) ++p;
            } else if (cmd >= 0x80 && cmd <= 0xB0) {  // 대기
            } else if (cmd == 0xB1) {  // FINE
                break;
            } else if (cmd == 0xB2) {  // GOTO
                uint32_t target = rom.u32(p);
                if (target <= at) return true;
                break;
            } else if (cmd == 0xB3) {  // PATT
                p += 4;
            } else if (cmd == 0xB4) {  // PEND
            } else if (cmd == 0xB5) {  // REPT
                p += 5;
            } else if (cmd == 0xB9) {  // MEMACC
                uint8_t op = rom.u8(p);
                p += 3;
                if (op >= 6 && op <= 17) p += 4;
            } else if (cmd == 0xCC) {  // PORT
                p += 2;
            } else if (cmd == 0xCD) {  // XCMD
                uint8_t x = rom.u8(p++);
                if (x == 1 || x == 13) p += 4;
                else if (x == 12) p += 2;
                else if (x >= 2 && x <= 11) p += 1;
                else break;
            } else if (cmd >= 0xBA && cmd <= 0xC8) {  // 인자 1개
                p += 1;
            } else {
                break;  // 알 수 없는 명령 (원본도 FINE 처리)
            }
        }
    }
    return false;
}

bool BgmPlayerSelector::locate(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands) {
    reset();
    for (const auto& c : cands) {
        const MPlayEntry& e0 = c.entries[0];
        if (!isPlayerInfoValid(bus, e0.info)) continue;
        if (bus.r8(e0.info + 8) != e0.numTracks) continue;
        if (bus.r32(e0.info + 0x2C) != e0.tracks) continue;
        int maxTracks = 0;
        for (const auto& e : c.entries) maxTracks = std::max<int>(maxTracks, e.numTracks);
        const int threshold = std::max(4, (maxTracks + 1) / 2);
        for (const auto& e : c.entries) {
            if (e.numTracks >= threshold && isPlayerInfoValid(bus, e.info)) m_players.push_back(e.info);
        }
        m_state.assign(m_players.size(), State());
        return !m_players.empty();
    }
    return false;
}

bool BgmPlayerSelector::stillValid(GbaBus& bus) const {
    for (uint32_t p : m_players) {
        if (!isPlayerInfoValid(bus, p)) return false;
    }
    return !m_players.empty();
}

void BgmPlayerSelector::reset() {
    m_players.clear();
    m_state.clear();
    m_active = 0;
    m_frame = 0;
    m_loopCache.clear();
}

uint32_t BgmPlayerSelector::update(GbaBus& bus, const Rom& rom, BgmSnapshot& snap, bool& changed) {
    ++m_frame;
    changed = false;
    uint32_t best = 0;
    uint64_t bestStart = 0;
    bool activePlaying = false;
    BgmSnapshot activeSnap, bestSnap;
    for (size_t i = 0; i < m_players.size(); ++i) {
        BgmSnapshot s = readBgm(bus, m_players[i]);
        State& st = m_state[i];
        if (s.header != st.header || s.clock < st.clock) st.startedAt = m_frame;  // 곡 시작/재시작
        st.header = s.header;
        st.clock = s.clock;
        const bool playing = s.header && !(s.status & 0x80000000u) && (s.status & 0xFFFFu);
        if (m_players[i] == m_active) {
            activePlaying = playing;
            activeSnap = s;
        }
        bool looping = false;
        if (playing) {
            auto it = m_loopCache.find(s.header);
            if (it == m_loopCache.end()) it = m_loopCache.emplace(s.header, isLoopingSong(rom, s.header)).first;
            looping = it->second;
        }
        if (playing && looping && st.startedAt >= bestStart) {
            best = m_players[i];
            bestStart = st.startedAt;
            bestSnap = s;
        }
    }
    // 재생 중인 플레이어 중 가장 최근에 시작한 쪽을 따른다. 아무것도 재생 중이 아니면 현재 플레이어 유지
    if (best && best != m_active) {
        uint64_t activeStart = 0;
        for (size_t i = 0; i < m_players.size(); ++i) {
            if (m_players[i] == m_active) activeStart = m_state[i].startedAt;
        }
        if (!activePlaying || bestStart > activeStart) {
            m_active = best;
            changed = true;
            snap = bestSnap;
            return m_active;
        }
    }
    if (!m_active && !m_players.empty()) {
        m_active = m_players[0];
        changed = true;
        activeSnap = readBgm(bus, m_active);
    }
    snap = activeSnap;
    return m_active;
}

void BgmMirror::reset() {
    have_ = false;
    last_ = BgmSnapshot();
}

void BgmMirror::update(const BgmSnapshot& s) {
    const bool paused = (s.status & STATUS_PAUSE) != 0;

    if (s.header == 0) {
        if (eng_.currentSong()) eng_.stop();
        last_ = s;
        have_ = true;
        return;
    }

    // 곡 변경 또는 같은 곡 재시작(MPlayStart 가 clock 을 0 으로 되돌림)
    bool restart = !have_ || s.header != last_.header || s.clock < last_.clock;
    if (restart) {
        if (!have_ && paused) {
            // 연결 시점에 이미 멈춰 있던 곡은 시작하지 않는다
        } else {
            eng_.start(s.header);
        }
    }

    // 페이드 시작 (m4aMPlayFadeOut / FadeOutTemporarily / FadeIn): 1배속 재생기가 같은 규칙으로 실제 시간 기준으로 진행한다.
    // 게임 함수는 fadeOI = fadeOC = 속도, fadeOV = 시작 값(나가기 256, 임시 257, 들어오기 2)을 넣고,
    // 프레임마다 fadeOV 를 16 씩 바꾼다. 우리가 읽기 전에 한 단계 진행되었을 수 있으므로 시작 값은 종류로 정한다
    if (s.fadeOI) {
        const uint16_t type = s.fadeOV & 3;
        bool started = !have_ || last_.fadeOI == 0 || (last_.fadeOV & 3) != type || last_.fadeOI != s.fadeOI;
        if (!started && have_) {
            // 같은 종류의 페이드를 처음부터 다시 걸었다
            started = (type & 2) ? s.fadeOV < last_.fadeOV : s.fadeOV > last_.fadeOV;
        }
        if (started) {
            const uint16_t startOV = (type & 2) ? (uint16_t) 2 : (uint16_t) ((64 << 2) | (type & 1));
            eng_.startFade(s.fadeOI, startOV);
        }
    }

    if (paused) {
        bool explicitStop = (s.status & STATUS_TRACK) != 0;  // m4aMPlayStop / 임시 페이드아웃
        bool fadeEnded = have_ && last_.fadeOI != 0;          // 게임에서 페이드아웃 완료
        // 페이드로 멈춘 것은 1배속 재생기가 자기 페이드를 끝까지 진행한 뒤 스스로 멈춘다.
        // 트랙이 모두 끝난 자연 종료는 외부 재생기가 스스로 끝날 때까지 둔다
        if (fadeEnded || eng_.fading()) {
        } else if (explicitStop) {
            eng_.setPaused(true);
        }
    } else if (eng_.paused()) {
        eng_.setPaused(false);  // MPlayContinue
    }

    last_ = s;
    have_ = true;
}

double songSeconds(const Rom& rom, uint32_t header) {
    // MP2K 대기 명령(0x80~0xB0)의 틱 수
    static const uint8_t kWait[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                    17, 18, 19, 20, 21, 22, 23, 24, 28, 30, 32, 36, 40, 42, 44, 48, 52,
                                    54, 56, 60, 64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96};
    const uint8_t n = rom.u8(header);
    if (n == 0 || n > 16 || !rom.valid(header, 8 + 4u * n)) return 0;
    uint32_t longest = 0;
    int tempo = 0;
    for (uint8_t t = 0; t < n; ++t) {
        uint32_t p = rom.u32(header + 8 + 4u * t);
        uint8_t running = 0;
        uint32_t ticks = 0;
        for (int steps = 0; steps < 20000 && rom.valid(p); ++steps) {
            const uint32_t at = p;
            uint8_t cmd = rom.u8(p);
            if (cmd < 0x80) {
                cmd = running;
            } else {
                ++p;
                if (cmd >= 0xBD) running = cmd;
            }
            if (cmd >= 0xCF || cmd == 0xCE) {
                for (int i = 0; i < 3 && rom.u8(p) < 0x80; ++i) ++p;
            } else if (cmd >= 0x80 && cmd <= 0xB0) {
                ticks += kWait[cmd - 0x80];
            } else if (cmd == 0xB1) {
                break;
            } else if (cmd == 0xB2) {
                (void) at;
                break;  // GOTO: 반복 곡은 첫 반복 지점까지, 앞으로 건너뛰는 곡은 거기서 끝으로 본다
            } else if (cmd == 0xB3) {
                p += 4;
            } else if (cmd == 0xB4) {
            } else if (cmd == 0xB5) {
                p += 5;
            } else if (cmd == 0xB9) {
                uint8_t op = rom.u8(p);
                p += 3;
                if (op >= 6 && op <= 17) p += 4;
            } else if (cmd == 0xBB) {
                if (!tempo) tempo = rom.u8(p) * 2;  // 템포 = 인자 x 2 (분당 4분음표)
                p += 1;
            } else if (cmd == 0xCC) {
                p += 2;
            } else if (cmd == 0xCD) {
                uint8_t x = rom.u8(p++);
                if (x == 1 || x == 13) p += 4;
                else if (x == 12) p += 2;
                else if (x >= 2 && x <= 11) p += 1;
                else break;
            } else if (cmd >= 0xBA && cmd <= 0xC8) {
                p += 1;
            } else {
                break;
            }
        }
        longest = std::max(longest, ticks);
    }
    // 4분음표 = 24틱
    return longest * 60.0 / (24.0 * (tempo > 0 ? tempo : 150));
}

bool isMusicSong(const Rom& rom, uint32_t header) {
    const uint8_t n = rom.u8(header);
    if (n == 0 || n > 16) return false;
    return n >= 4 || isLoopingSong(rom, header);
}

bool BgmMultiMirror::locate(GbaBus& bus, const std::vector<MPlayTableCandidate>& cands) {
    std::lock_guard<std::mutex> lk(mtx_);
    players_.clear();
    for (const auto& c : cands) {
        const MPlayEntry& e0 = c.entries[0];
        if (!isPlayerInfoValid(bus, e0.info)) continue;
        if (bus.r8(e0.info + 8) != e0.numTracks) continue;
        if (bus.r32(e0.info + 0x2C) != e0.tracks) continue;
        int maxTracks = 0;
        for (const auto& e : c.entries) maxTracks = std::max<int>(maxTracks, e.numTracks);
        const int threshold = std::max(4, (maxTracks + 1) / 2);
        for (const auto& e : c.entries) {
            if (!isPlayerInfoValid(bus, e.info)) continue;
            Player p;
            p.info = e.info;
            p.se = e.numTracks < threshold;
            p.eng.reset(new Mp2kEngine(rom_));
            p.eng->configure(cfg_);
            p.mirror.reset(new BgmMirror(*p.eng));
            players_.push_back(std::move(p));
        }
        // 곡 목록에 없는 플레이어: MPlayOpen 이 SoundInfo.musicPlayerHead(+0x24) 부터
        // MusicPlayerInfo.musicPlayerNext(+0x3C) 로 이어 둔다 (효과음 전용으로 따로 연 플레이어가 이렇게 이어진다)
        const uint32_t si = bus.r32(kSoundInfoPtr);
        if (si >= 0x02000000u && si < 0x03008000u) {
            uint32_t mp = bus.r32(si + 0x24);
            for (int guard = 0; guard < 32 && mp >= 0x02000000u && mp < 0x03008000u; ++guard) {
                bool known = false;
                for (const auto& q : players_) known |= q.info == mp;
                if (!known && isPlayerInfoValid(bus, mp)) {
                    Player p;
                    p.info = mp;
                    p.se = true;
                    p.extra = true;
                    p.eng.reset(new Mp2kEngine(rom_));
                    p.eng->configure(cfg_);
                    p.mirror.reset(new BgmMirror(*p.eng));
                    players_.push_back(std::move(p));
                }
                mp = bus.r32(mp + 0x3C);
            }
        }
        return !players_.empty();
    }
    return false;
}

bool BgmMultiMirror::stillValid(GbaBus& bus) const {
    for (const auto& p : players_) {
        if (!isPlayerInfoValid(bus, p.info)) return false;
    }
    return !players_.empty();
}

void BgmMultiMirror::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    players_.clear();
    repeat_.clear();
    cutscene_ = false;
    psgMask_ = 0;
}

void BgmMultiMirror::stopAll() {
    for (auto& p : players_) {
        p.eng->stop();
        p.mirror->reset();
        p.header = 0xFFFFFFFF;
        p.music = false;
        p.kind = p.engKind = Kind_Sound;
        p.orphan = p.gameDone = false;
        p.seOn = false;
        p.seHeader = p.seClock = 0;
        p.seLive = 0;
        p.seStopping = false;
        p.musicHeader = p.musicClock = 0;
    }
    repeat_.clear();
    cutscene_ = false;
    psgMask_ = 0;
}

void BgmMultiMirror::releaseMute(GbaBus& bus) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& p : players_) {
        if (!p.music) continue;
        const BgmSnapshot s = readBgm(bus, p.info);
        if (s.header && !(s.status & STATUS_PAUSE) && (s.status & STATUS_TRACK)) restartGameSong(bus, p.info);
    }
    stopAll();
}

void BgmMultiMirror::configure(const EngineConfig& cfg) {
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_ = cfg;
    for (auto& p : players_) {
        uint32_t song = p.eng->currentSong();
        p.eng->configure(cfg);
        if (song && p.music) p.eng->start(song);
    }
}

void BgmMultiMirror::setRuntimeParams(const SoundInfoSnapshot& si) {
    {
        std::lock_guard<std::mutex> lk(lastMtx_);
        lastSi_ = si;
    }
    for (auto& p : players_) {
        p.eng->setRuntimeParams(si.maxChans, si.masterVolume, si.reverb, si.samplesPerVBlank, si.dmaPeriod);
    }
}

int BgmMultiMirror::classify(uint32_t header) {
    if (!header) return Kind_Sound;
    auto it = kindCache_.find(header);
    if (it != kindCache_.end()) return it->second;
    int kind = Kind_Sound;
    if (isMusicSong(*rom_, header)) {
        if (isLoopingSong(*rom_, header)) kind = Kind_Music;
        // 반복 없는 음악: 12초 이하는 팡파레, 그보다 길면 연출 곡 (NDS·3DS 판과 같은 기준)
        else kind = songSeconds(*rom_, header) <= 12.0 ? Kind_Jingle : Kind_Cutscene;
    }
    kindCache_.emplace(header, kind);
    return kind;
}

bool BgmMultiMirror::isSfxSong(uint32_t header) {
    // 곡 머리가 롬에 있고, 음악이 아니고, 반복하지 않는 소리
    if (header < 0x08000000u || header >= 0x0A000000u || !rom_->valid(header, 8)) return false;
    const uint8_t n = rom_->u8(header);
    if (n == 0 || n > 16) return false;
    return classify(header) == Kind_Sound && !isLoopingSong(*rom_, header);
}

void BgmMultiMirror::onSeFrame(GbaBus& bus, Player& p, bool mute) {
    const BgmSnapshot s = readBgm(bus, p.info);
    if (s.header != p.header) {
        const bool recheck = p.header == 0xFFFFFFFEu;  // 사용자 지정이 바뀌어 다시 보는 것 (새로 나온 소리가 아님)
        p.header = s.header;
        lookupUser(p, s.header, classify(s.header), !recheck);
        p.seOn = wantSe(p, bus, s.header);
    }
    // 대체하지 않는 소리로 바뀌어도, 재생기에서 나오던 효과음은 끝까지 둔다
    if (!p.seOn) {
        p.seHeader = 0;
        if (mute) applyNativeGain(bus, p, p.gain);
        return;
    }
    p.scaled = false;
    seStep(bus, p, s, mute);
}

// 효과음 하나를 대체 중인 플레이어의 한 프레임 (효과음용·음악용 플레이어 공통)
void BgmMultiMirror::seStep(GbaBus& bus, Player& p, const BgmSnapshot& s, bool mute) {
    if (mute) muteBgmTracks(bus, p.info);
    // 게임이 소리를 새로 냈는지: 다른 소리이거나, 같은 소리를 다시 냄(MPlayStart 가 clock 을 0 으로 되돌림)
    const bool started = s.header != p.seHeader || s.clock < p.seClock;
    p.seHeader = s.header;
    p.seClock = s.clock;
    if (started) seStarted_ = true;
    if (!started) return;
    const bool sounding = p.eng->currentSong() && !p.eng->finished();
    // 재생기에서 다른 효과음이 아직 울리는 동안 새로 낸 소리는 건너뛴다 (배속 중에 끊겨 들리지 않게)
    if (sounding && p.seLive != s.header) return;
    // 스스로 반복하는 소리(경고음 등)를 게임이 다시 틀었는데 재생기에서 아직 반복 중이면 그대로 이어 간다 (간격이 1배속)
    if (sounding && p.seLive == s.header && selfLoopSeconds(s.header) > 0) {
        p.seStopping = false;
        return;
    }
    // 같은 소리를 되풀이: 게임 안 간격만큼 실제 시간이 지났을 때만 처음부터 다시 낸다 (되풀이 간격이 1배속)
    if (!allowRepeat(s.header)) return;
    startSe(bus, p, s.header);
    p.seLive = s.header;
    p.seStartFrame = frame_;
    p.seStartWall = std::chrono::steady_clock::now();
    p.seStopping = false;
}

bool BgmMultiMirror::allowRepeat(uint32_t header) {
    Repeat& r = repeat_[header];
    const auto now = std::chrono::steady_clock::now();
    const double gameGap = (double) (frame_ - r.lastTrigFrame) / kGbaFps;
    const bool repeat = r.played && frame_ >= r.lastTrigFrame && gameGap <= kRepeatWindow;
    r.lastTrigFrame = frame_;
    if (repeat && std::chrono::duration<double>(now - r.lastPlay).count() < gameGap * 0.9) return false;
    r.played = true;
    r.lastPlay = now;
    return true;
}

// 효과음 방식으로 내는 소리를 게임이 멈췄으면(다른 소리로 바뀜, 멈춤, 트랙이 모두 끝남), 시작한 뒤 게임 안에서 흐른
// 시간만큼 1배속으로 낸 뒤 멈춘다 (배속 중에 바로 끊으면 막 시작한 소리가 아주 짧게 잘린다).
// 스스로 반복하는 소리(경고음 등)는 게임이 멈추는 순간 바로 멈춘다: 게임 시간만큼 기다리면 배속 중에 오래 울린 만큼
// 게임에서 끝난 뒤에도 한참 남는다
void BgmMultiMirror::seStopCheck(Player& p, const BgmSnapshot& s) {
    if (!p.seLive) return;
    if (!p.eng->currentSong() || p.eng->finished()) {
        p.seLive = 0;
        p.seStopping = false;
        return;
    }
    const bool gamePlaying = s.header == p.seLive && !(s.status & STATUS_PAUSE) && (s.status & STATUS_TRACK);
    const auto now = std::chrono::steady_clock::now();
    if (gamePlaying) {
        p.seStopping = false;
        return;
    }
    const double loopSec = selfLoopSeconds(p.seLive);
    if (loopSec > 0) {
        // 게임이 멈춘 뒤 반복 한 바퀴의 1.5배(게임 시간, 최대 3초) 안에 다시 틀지 않으면 멈춘다.
        // 게임이 짧은 간격으로 멈췄다 다시 트는 동안에는 1배속 반복이 끊기지 않는다
        if (!p.seStopping) {
            p.seStopping = true;
            p.seStopFrame = frame_;
        }
        const double waited = (double) (frame_ - p.seStopFrame) / kGbaFps;
        if (waited >= std::min(kRepeatWindow, std::max(0.5, loopSec * 1.5))) {
            p.eng->stop();
            p.seLive = 0;
            p.seStopping = false;
        }
        return;
    }
    if (!p.seStopping) {
        const double gameSec = (double) (frame_ - p.seStartFrame) / kGbaFps;
        p.seStopAt = p.seStartWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                         std::chrono::duration<double>(gameSec));
        p.seStopping = true;
    }
    if (now >= p.seStopAt) {
        p.eng->stop();
        p.seLive = 0;
        p.seStopping = false;
    }
}

double BgmMultiMirror::selfLoopSeconds(uint32_t header) {
    if (header < 0x08000000u || header >= 0x0A000000u) return 0;
    auto it = selfLoopCache_.find(header);
    if (it != selfLoopCache_.end()) return it->second;
    double v = 0;
    if (rom_->valid(header, 8) && isLoopingSong(*rom_, header)) v = std::max(0.1, songSeconds(*rom_, header));
    selfLoopCache_.emplace(header, v);
    return v;
}

bool BgmMultiMirror::isShortLoop(uint32_t header) {
    if (header < 0x08000000u || header >= 0x0A000000u) return false;
    auto it = shortLoopCache_.find(header);
    if (it != shortLoopCache_.end()) return it->second;
    bool v = false;
    if (rom_->valid(header, 8)) {
        const uint8_t n = rom_->u8(header);
        v = n >= 1 && n <= 16 && isLoopingSong(*rom_, header) && songSeconds(*rom_, header) <= kShortLoopSeconds;
    }
    shortLoopCache_.emplace(header, v);
    return v;
}

// 효과음 하나를 재생기로 시작한다. 게임이 RAM 에 만든 곡(울음소리 등)은 지금 RAM 을 복사해 쓴다
void BgmMultiMirror::startSe(GbaBus& bus, Player& p, uint32_t header) {
    if (!isRamSong(header)) {
        p.eng->start(header);
        return;
    }
    std::shared_ptr<const RamImage> ram = copyRamSong(bus, header);
    {
        std::lock_guard<std::mutex> lk(lastMtx_);
        lastRamHeader_ = header;
        lastRam_ = ram;  // 미리 듣기용: 같은 사본을 함께 쓴다 (읽기만 한다)
    }
    p.eng->startWithRam(header, std::move(ram));
}

// 게임에서는 이미 끝났거나 지나갔는데, 재생기는 아직 1배속으로 내고 있는 팡파레
bool BgmMultiMirror::jingleLagging(const Player& p) const {
    return p.engKind == Kind_Jingle && p.eng->currentSong() && !p.eng->finished() && (p.orphan || p.gameDone);
}

bool BgmMultiMirror::anyJingleLagging(const Player* except) const {
    for (const auto& p : players_) {
        if (&p != except && jingleLagging(p)) return true;
    }
    return false;
}

void BgmMultiMirror::cutLaggingJingles(const Player* except) {
    for (auto& p : players_) {
        if (&p == except || !jingleLagging(p)) continue;
        p.eng->stop();
        p.mirror->reset();
        p.orphan = false;
        p.engKind = Kind_Sound;
    }
}

void BgmMultiMirror::onFrame(GbaBus& bus, bool mute) {
    bool cutscene = false;
    seStarted_ = false;
    ++frame_;
    refreshUser(bus);
    for (auto& p : players_) {
        if (note_ && catalog_ && catalog_->Active()) countStart(p, readBgm(bus, p.info));
        seStopCheck(p, readBgm(bus, p.info));
        if (p.se) {
            onSeFrame(bus, p, mute);
            // 효과음용 플레이어로 내는 소리를 사용자가 연출로 정했으면 나오는 동안 배속을 멈춘다
            if (p.user == SoundCatalog::Kind_Cutscene) {
                const BgmSnapshot s = readBgm(bus, p.info);
                if (s.header && !(s.status & STATUS_PAUSE) && (s.status & STATUS_TRACK)) cutscene = true;
            }
            continue;
        }
        BgmSnapshot s = readBgm(bus, p.info);
        const bool playing = s.header && !(s.status & STATUS_PAUSE) && (s.status & STATUS_TRACK);
        if (s.header != p.header) {
            const bool recheck = p.header == 0xFFFFFFFEu;  // 사용자 지정이 바뀌어 다시 보는 것
            p.header = s.header;
            const int autoKind = classify(s.header);
            lookupUser(p, s.header, autoKind, !recheck);
            const int kind = effectiveKind(autoKind, p.user);
            // 게임이 RAM 에 만든 곡은 음악으로 따라가지 않는다 (지정에 따라 효과음처럼 처음부터 끝까지 낸다)
            // 스스로 반복하는 짧은 소리(경고음 등)는 BGM 으로 정해도 효과음 방식으로 낸다
            const bool music = (kind == Kind_Music || kind == Kind_Jingle) && !isRamSong(s.header) && !isShortLoop(s.header);
            if (!music && jingleLagging(p)) {
                // 게임은 이 플레이어로 효과음 등을 내기 시작했지만, 팡파레는 1배속으로 끝까지 낸다
                p.orphan = true;
            } else if (!music && p.music) {
                p.eng->stop();  // 음악 -> 효과음/빈 곡/연출 곡: 대체 재생을 멈춘다
                p.mirror->reset();
                p.engKind = Kind_Sound;
            }
            // 팡파레 전에 나오던 BGM 으로 돌아가는 곡인데 1배속 팡파레가 아직 나오고 있으면,
            // 팡파레를 끝까지 낸 뒤 그 곡을 처음부터 낸다 (1배속으로 할 때 들리는 순서)
            const bool defer = music && kind == Kind_Music && s.header == jingleBefore_ &&
                               (jingleLagging(p) || anyJingleLagging(&p));
            if (music && kind == Kind_Music && !defer) lastMusic_ = s.header;
            if (music && kind == Kind_Jingle) jingleBefore_ = lastMusic_;
            if (defer) {
                p.deferHeader = s.header;
                p.seLive = 0;
                p.seStopping = false;
            } else if (music) {
                p.deferHeader = 0;
                p.seLive = 0;  // 이 플레이어의 재생기는 이제 음악을 따라간다
                p.seStopping = false;
                // 새 곡이 시작된다 = 장면이 넘어갔다: 게임에서는 지나간 팡파레가 아직 나오고 있으면 끊는다
                cutLaggingJingles(&p);
                if (p.orphan) {
                    p.eng->stop();
                    p.mirror->reset();
                    p.orphan = false;
                }
                p.engKind = kind;
                p.gameDone = false;
            }
            p.kind = kind;
            p.music = music;
            // 음악용 플레이어로 내는 효과음 (일부 게임의 선택음)
            p.seOn = !music && wantSe(p, bus, s.header);
        }
        if (p.seOn && !p.orphan) {
            p.scaled = false;
            seStep(bus, p, s, mute);
            continue;
        }
        p.seHeader = 0;
        if (p.kind == Kind_Cutscene && playing) cutscene = true;
        // 원본 그대로 나오는 소리: 사용자가 정한 볼륨을 트랙 볼륨에 곱한다 (대체 중이면 무음 처리가 맡는다)
        if (mute && !p.music) applyNativeGain(bus, p, p.gain);
        else p.scaled = false;
        if (p.orphan) {
            if (p.eng->finished() || !p.eng->currentSong()) {
                p.eng->stop();
                p.mirror->reset();
                p.orphan = false;
                p.engKind = Kind_Sound;
            }
            continue;
        }
        if (!p.music) continue;
        if (p.kind == Kind_Jingle && !(s.status & STATUS_TRACK)) p.gameDone = true;  // 게임에서는 끝남
        // 게임의 트랙 볼륨 조절 (m4aMPlayVolumeControl: 울음소리 중 BGM 줄이기 등).
        // 원본 트랙의 volX 는 무음 처리로 0 이므로, 0 이 아니면 게임이 이번 프레임에 새로 넣은 값이다
        // (페이드 중에는 페이드가 volX 를 쓰므로 보지 않는다)
        if (s.header != p.musicHeader) {
            std::fill(std::begin(p.volX), std::end(p.volX), 0);
        } else if (!s.fadeOI) {
            followVolumeControl(bus, p);
        }
        if (mute) muteBgmTracks(bus, p.info);
        if (p.deferHeader) {
            if (s.header != p.deferHeader) {
                p.deferHeader = 0;  // 기다리는 동안 게임이 다른 곡으로 넘어갔다 (위에서 따로 처리됨)
            } else if (jingleLagging(p) || anyJingleLagging(&p)) {
                continue;  // 팡파레가 1배속으로 끝날 때까지 기다린다
            } else {
                // 팡파레가 끝났다: 미뤄 둔 곡을 처음부터
                p.deferHeader = 0;
                lastMusic_ = s.header;
                p.engKind = Kind_Music;
                p.gameDone = false;
                p.orphan = false;
                p.eng->stop();
                p.mirror->reset();
                p.musicHeader = s.header;
                p.musicClock = s.clock;
                p.mirror->update(s);
                continue;
            }
        }
        // 멈춰 있던 곡을 게임이 다시 이어 갈 때, 게임에서는 지나간 팡파레가 아직 나오고 있으면
        // 팡파레가 1배속으로 끝날 때까지 기다린다
        if (p.kind == Kind_Music && p.eng->paused() && !(s.status & STATUS_PAUSE) && anyJingleLagging(&p)) {
            continue;
        }
        // 같은 곡을 게임이 다시 틀었을 때도 되풀이 간격을 1배속으로 (게임 안 간격만큼 실제 시간이 지났을 때만 처음부터)
        const bool restart = s.header && s.header == p.musicHeader && s.clock < p.musicClock;
        if (restart && s.header == jingleBefore_ && anyJingleLagging(&p)) {
            // 팡파레 전 BGM 을 게임이 처음부터 다시 틀었다: 1배속 팡파레가 끝난 뒤 처음부터
            p.deferHeader = s.header;
            continue;
        }
        p.musicHeader = s.header;
        p.musicClock = s.clock;
        if (restart && !allowRepeat(s.header)) {
            p.mirror->follow(s);
            continue;
        }
        p.mirror->update(s);
    }
    cutscene_ = cutscene;
    if (mute) psgMask_ = computePsgMask(bus, seStarted_);
}

void BgmMultiMirror::followVolumeControl(GbaBus& bus, Player& p) {
    uint8_t n = bus.r8(p.info + 8);
    const uint32_t tracks = bus.r32(p.info + 0x2C);
    if (n > 16) n = 16;
    for (uint8_t i = 0; i < n; ++i) {
        const uint32_t t = tracks + i * TRACK_SIZE;
        if (!(bus.r8(t) & 0x80)) continue;
        const uint8_t volX = bus.r8(t + 0x13);
        if (volX && volX != p.volX[i]) {
            p.eng->volumeControl((uint16_t) (1u << i), (uint16_t) (volX * 4));
        }
        if (volX) p.volX[i] = volX;
    }
}

uint8_t BgmMultiMirror::computePsgMask(GbaBus& bus, bool seStarted) const {
    // m4a SoundInfo(+0x1C) 의 CgbChannel 4개 (0x40 바이트씩): statusFlags(+0), 이 채널을 쓰는 트랙(+0x2C)
    const uint32_t si = bus.r32(kSoundInfoPtr);
    if (si < 0x02000000u || si >= 0x03008000u) return 0;
    const uint32_t cgb = bus.r32(si + 0x1C);
    if (cgb < 0x02000000u || cgb >= 0x03008000u) return 0;
    uint8_t mask = 0;
    for (int k = 0; k < 4; ++k) {
        const uint32_t ch = cgb + k * 0x40;
        if (!bus.r8(ch)) {
            // 빈 채널: 대체할 효과음이 방금 시작됐으면 다음 프레임에 그 소리가 이 채널을 잡을 수 있으므로 미리 끈다
            if (seStarted) mask |= 1 << k;
            continue;
        }
        const uint32_t track = bus.r32(ch + 0x2C);
        for (const auto& p : players_) {
            if (!p.seOn && !p.music) continue;
            const uint32_t base = bus.r32(p.info + 0x2C);
            const uint32_t n = std::min<uint32_t>(bus.r8(p.info + 8), 16);
            if (track >= base && track < base + n * TRACK_SIZE) {
                mask |= 1 << k;
                break;
            }
        }
    }
    return mask;
}

void BgmMultiMirror::render(float* out, int frames) {
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t n = static_cast<size_t>(frames) * 2;
    std::fill(out, out + n, 0.0f);
    if (tmp_.size() < n) tmp_.resize(n);
    for (auto& p : players_) {
        if (!p.eng->currentSong()) continue;
        p.eng->render(tmp_.data(), frames);
        // 음악·팡파레가 아닌 소리 = 효과음
        const float gain = (p.engKind == Kind_Sound ? sfxGain_.load() : 1.0f) * p.gain;
        for (size_t i = 0; i < n; ++i) out[i] += tmp_[i] * gain;
    }
}

// ======================= ff-bgm1x: 소리 분류 =======================

std::string BgmMultiMirror::songKey(uint32_t header) {
    if (isRamSong(header)) return "cry:all";
    char buf[24];
    std::snprintf(buf, sizeof(buf), "song:%08X", (unsigned) header);
    return buf;
}

std::string BgmMultiMirror::songName(uint32_t header) const {
    if (isRamSong(header)) return "울음소리 전체 (게임이 RAM 에 만드는 곡)";
    char buf[48];
    if (songNumbers_) {
        auto it = songNumbers_->find(header);
        if (it != songNumbers_->end()) {
            std::snprintf(buf, sizeof(buf), "곡 %d (0x%08X)", it->second, (unsigned) header);
            return buf;
        }
    }
    std::snprintf(buf, sizeof(buf), "곡 (0x%08X)", (unsigned) header);
    return buf;
}

// 분류 창에 보여줄 자동 판단
int BgmMultiMirror::autoCatalogKind(uint32_t header, int kind) {
    if (isRamSong(header)) return SoundCatalog::Kind_None;  // RAM 곡(울음소리 등): 기본은 원본 그대로 (사용자가 고른다)
    switch (kind) {
    case Kind_Music: return SoundCatalog::Kind_Bgm;
    case Kind_Jingle: return SoundCatalog::Kind_Jingle;
    case Kind_Cutscene: return SoundCatalog::Kind_Cutscene;
    }
    return isSfxSong(header) ? SoundCatalog::Kind_Sfx : SoundCatalog::Kind_None;
}

// 곡이 바뀔 때: 사용자 지정과 볼륨을 읽는다 (count = 목록의 나온 횟수를 센다)
void BgmMultiMirror::lookupUser(Player& p, uint32_t header, int kind, bool count) {
    const int fallback = SoundCatalog::Kind_Auto;
    p.user = fallback;
    p.gain = 1.0f;
    (void) kind;
    (void) count;
    if (!catalog_ || !catalog_->Active() || !listable(header)) return;
    int user = SoundCatalog::Kind_Auto, volume = 100;
    catalog_->Lookup(songKey(header), &user, &volume);
    p.user = user == SoundCatalog::Kind_Auto ? fallback : user;
    p.gain = SoundCatalog::PerceivedGain((float) volume);
}

// 사용자 지정을 반영한 곡 종류 (아무것도 아님·효과음은 음악으로 따라가지 않는다)
int BgmMultiMirror::effectiveKind(int kind, int user) {
    switch (user) {
    case SoundCatalog::Kind_Bgm: return Kind_Music;
    case SoundCatalog::Kind_Jingle: return Kind_Jingle;
    case SoundCatalog::Kind_Cutscene: return Kind_Cutscene;
    case SoundCatalog::Kind_None:
    case SoundCatalog::Kind_Sfx: return Kind_Sound;
    }
    return kind;
}

bool BgmMultiMirror::validRamSong(GbaBus& bus, uint32_t header) const {
    if (!isRamSong(header) || (header & 3)) return false;
    const uint8_t n = bus.r8(header);
    return n >= 1 && n <= 16;
}

// 이 소리를 재생기로 처음부터 끝까지 1배속으로 낼지 (효과음 방식)
bool BgmMultiMirror::wantSe(const Player& p, GbaBus& bus, uint32_t header) {
    const int user = p.user;
    if (user == SoundCatalog::Kind_None || user == SoundCatalog::Kind_Cutscene) return false;
    if (isRamSong(header)) {
        // RAM 곡(울음소리 등)은 "RAM 곡 읽기" 가 켜져 있고 사용자가 1배속으로 정했을 때만
        if (!ramSongs_ || !validRamSong(bus, header)) return false;
        if (user == SoundCatalog::Kind_Sfx) return sfx1x_;
        return user == SoundCatalog::Kind_Bgm || user == SoundCatalog::Kind_Jingle;
    }
    if (!header || !rom_->valid(header, 8) || rom_->u8(header) == 0 || rom_->u8(header) > 16) return false;
    if (user == SoundCatalog::Kind_Sfx) return sfx1x_;
    // 효과음용 플레이어로 내는 소리, 또는 스스로 반복하는 짧은 소리를 BGM·팡파레로 정했으면 옵션과 관계없이 1배속
    if (user == SoundCatalog::Kind_Bgm || user == SoundCatalog::Kind_Jingle) return p.se || isShortLoop(header);
    if (p.extra) return false;
    return sfx1x_ && (isSfxSong(header) || isShortLoop(header));
}

// 원본 그대로 나오는 소리의 볼륨: 트랙 volX(게임이 볼륨 조절에 쓰는 값)에 배율을 곱해 넣는다.
// 게임이 volX 를 바꾸면(페이드 등) 그 값을 새 기준으로 삼는다. 트랙 볼륨 계산이 넘치지 않게 위쪽은 자른다
void BgmMultiMirror::applyNativeGain(GbaBus& bus, Player& p, float gain) {
    if (gain == 1.0f && !p.scaled) return;
    if (!p.scaled) {
        for (int i = 0; i < 16; ++i) p.baseVolX[i] = p.writtenVolX[i] = -1;
    }
    uint8_t n = bus.r8(p.info + 8);
    if (n > 16) n = 16;
    const uint32_t tracks = bus.r32(p.info + 0x2C);
    for (uint8_t i = 0; i < n; ++i) {
        const uint32_t t = tracks + i * TRACK_SIZE;
        const uint8_t flags = bus.r8(t);
        if (!(flags & 0x80)) {
            p.baseVolX[i] = p.writtenVolX[i] = -1;
            continue;
        }
        const int cur = bus.r8(t + 0x13);
        if (p.writtenVolX[i] < 0 || cur != p.writtenVolX[i]) p.baseVolX[i] = (int16_t) cur;  // 게임이 바꿈
        const int vol = bus.r8(t + 0x12);
        const int top = vol ? std::min(255, 255 * 32 / vol) : 255;
        int want = (int) std::lround(p.baseVolX[i] * gain);
        if (gain > 1.0f) want = std::min(want, std::max<int>(top, p.baseVolX[i]));
        want = std::max(0, std::min(255, want));
        if (want != cur) {
            bus.w8(t + 0x13, (uint8_t) want);
            bus.w8(t, (uint8_t) (flags | 0x03));  // MPT_FLG_VOLCHG
        }
        p.writtenVolX[i] = (int16_t) want;
    }
    p.scaled = gain != 1.0f;
}

// 분류 창에서 지정을 바꿨으면: 볼륨은 바로 바꾸고, 분류가 바뀐 플레이어는 곡을 다시 판단하게 한다
void BgmMultiMirror::refreshUser(GbaBus& bus) {
    if (!catalog_) return;
    const uint32_t ver = catalog_->UserVersion();
    if (ver == userVer_) return;
    userVer_ = ver;
    for (auto& p : players_) {
        if (p.header == 0xFFFFFFFF || p.header == 0xFFFFFFFEu) continue;
        const int before = p.user;
        Player probe;
        lookupUser(probe, p.header, classify(p.header), false);
        p.gain = probe.gain;
        if (probe.user != before) p.header = 0xFFFFFFFEu;  // 다음 단계에서 다시 판단
    }
    (void) bus;
}

void BgmMultiMirror::noteOnly(GbaBus& bus) {
    if (!catalog_ || !note_ || !catalog_->Collecting()) return;
    for (auto& p : players_) countStart(p, readBgm(bus, p.info));
}

// 목록에 넣을 소리인지: 롬 안 곡은 트랙이 있는 것만 (트랙 없는 '빈 곡' 은 게임이 소리를 끄는 용도), RAM 곡은 모두
bool BgmMultiMirror::listable(uint32_t header) const {
    if (!header) return false;
    if (isRamSong(header)) return true;
    return rom_->valid(header, 8) && rom_->u8(header) >= 1 && rom_->u8(header) <= 16;
}

// 나온 횟수: 게임이 소리를 새로 시작할 때마다(다른 곡, 또는 같은 곡을 처음부터 다시 - MPlayStart 가 clock 을
// 0 으로 되돌림) 실제로 울리기 시작하는 프레임에 한 번 센다. 목록에 없으면(수집 중일 때) 넣는다
void BgmMultiMirror::countStart(Player& p, const BgmSnapshot& s) {
    if (s.header != p.cntHeader || s.clock < p.cntClock) p.cntPending = true;
    p.cntHeader = s.header;
    p.cntClock = s.clock;
    if (!p.cntPending) return;
    if (!listable(s.header)) {
        p.cntPending = false;
        return;
    }
    const bool playing = !(s.status & STATUS_PAUSE) && (s.status & STATUS_TRACK);
    if (!playing) return;
    p.cntPending = false;
    if (!catalog_ || !note_ || !catalog_->Active()) return;
    catalog_->NoteSeen(songKey(s.header), isRamSong(s.header) ? SoundCatalog::Type_Cry : SoundCatalog::Type_Song,
                       songName(s.header), autoCatalogKind(s.header, classify(s.header)), nullptr, nullptr);
}

SoundInfoSnapshot BgmMultiMirror::lastSoundInfo() const {
    std::lock_guard<std::mutex> lk(lastMtx_);
    return lastSi_;
}

bool BgmMultiMirror::lastRamSong(uint32_t& header, std::shared_ptr<const RamImage>& ram) const {
    std::lock_guard<std::mutex> lk(lastMtx_);
    if (!lastRamHeader_ || !lastRam_) return false;
    header = lastRamHeader_;
    ram = lastRam_;
    return true;
}

// ---- RAM 곡이 쓰는 데이터 모으기 ----

namespace {

bool isRamAddr(uint32_t a) {
    return a >= 0x02000000u && a < 0x04000000u;
}

bool isDataAddr(uint32_t a) {
    return isRamAddr(a) || (a >= 0x08000000u && a < 0x0A000000u);
}

// 되풀이되는 주소를 한 곳으로 (EWRAM 256KB, IWRAM 32KB)
uint32_t canonRam(uint32_t a) {
    return a < 0x03000000u ? 0x02000000u + ((a - 0x02000000u) & 0x3FFFFu) : 0x03000000u + ((a - 0x03000000u) & 0x7FFFu);
}

uint32_t ramEnd(uint32_t canon) {
    return canon < 0x03000000u ? 0x02040000u : 0x03008000u;
}

// 재생기(Mp2kEngine)가 읽는 순서를 그대로 따라간다: 곡 머리 -> 트랙 명령 (GOTO·PATT·REPT·MEMACC 갈래 포함)
// -> 쓰인 악기 -> 건반 나눔·드럼의 하위 악기 -> 파형. 재생기가 못 읽는 주소(RAM·롬 밖)는 RAM 전체를 복사해도
// 못 읽으므로 건너뛴다
class RamSongWalker {
public:
    explicit RamSongWalker(GbaBus& bus) : bus_(bus) {}
    bool walk(uint32_t header);
    std::vector<std::pair<uint32_t, uint32_t>> ranges;  // (시작, 길이), RAM 부분만

private:
    struct Run {
        uint32_t ptr;
        uint8_t runningStatus;
        bool pattern;  // PATT 로 들어간 부분 (PEND 에서 돌아간다)
    };
    GbaBus& bus_;
    int budget_ = 300000;  // 읽기 횟수 상한 (넘으면 RAM 전체 복사)
    std::set<uint32_t> visited_;
    std::set<uint32_t> voices_;
    std::set<uint32_t> waves_;

    uint8_t u8(uint32_t a) {
        --budget_;
        return bus_.r8(a);
    }
    uint32_t u32(uint32_t a) {
        budget_ -= 4;
        return bus_.r32(a);
    }
    void add(uint32_t a, uint32_t len) {
        if (!isRamAddr(a) || !len) return;
        const uint32_t c = canonRam(a);
        const uint32_t end = ramEnd(c);
        ranges.emplace_back(c, std::min<uint32_t>(len, end - c));
    }
    bool track(uint32_t start);
    void voice(uint32_t addr, bool sub);
    void wave(uint32_t wav, bool cgb);
};

bool RamSongWalker::walk(uint32_t header) {
    const uint8_t n = u8(header);
    if (n == 0 || n > 16) return true;
    add(header, 8 + 4u * n);
    const uint32_t tone = u32(header + 4);
    for (uint8_t i = 0; i < n; ++i) {
        if (!track(u32(header + 8 + 4u * i))) return false;
    }
    if (isDataAddr(tone)) {
        for (uint32_t idx : voices_) voice(tone + idx * 12u, false);
    }
    return budget_ > 0;
}

bool RamSongWalker::track(uint32_t start) {
    std::vector<Run> todo;
    todo.push_back({start, 0, false});
    while (!todo.empty()) {
        Run r = todo.back();
        todo.pop_back();
        uint32_t p = r.ptr;
        uint8_t rs = r.runningStatus;
        if (!isDataAddr(p) || !visited_.insert(p).second) continue;
        const uint32_t runStart = p;
        bool end = false;
        while (!end) {
            if (budget_ <= 0) return false;
            if (!isDataAddr(p)) break;
            uint8_t cmd = u8(p);
            if (cmd < 0x80) {
                if (!rs) break;  // 재생기도 여기서 트랙을 끝낸다
                cmd = rs;
            } else {
                ++p;
                if (cmd >= 0xBD) rs = cmd;
            }
            if (cmd >= 0xCF) {
                // 음표: 뒤에 0x80 미만 인자 0~3개 (건반, 세기, 길이 보정)
                for (int i = 0; i < 3 && u8(p) < 0x80; ++i) ++p;
                continue;
            }
            if (cmd < 0xB1) continue;  // 기다림
            switch (cmd) {
            case 0xB1: end = true; break;  // FINE
            case 0xB2:                     // GOTO
                todo.push_back({u32(p), rs, r.pattern});
                p += 4;
                end = true;
                break;
            case 0xB3:  // PATT: 부분을 따라가고 이어서 간다
                todo.push_back({u32(p), rs, true});
                p += 4;
                break;
            case 0xB4:  // PEND: 부분 안이면 돌아간다 (밖이면 재생기도 그냥 지나간다)
                if (r.pattern) end = true;
                break;
            case 0xB5:  // REPT
                todo.push_back({u32(p + 1), rs, r.pattern});
                p += 5;
                break;
            case 0xB9: {  // MEMACC: 비교하는 것은 갈래 주소가 뒤따른다
                const uint8_t op = u8(p);
                p += 3;
                if (op >= 6 && op <= 17) {
                    todo.push_back({u32(p), rs, r.pattern});
                    p += 4;
                }
                break;
            }
            case 0xBD:  // VOICE
                voices_.insert(u8(p));
                ++p;
                break;
            case 0xBA: case 0xBB: case 0xBC: case 0xBE: case 0xBF: case 0xC0:
            case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: case 0xC8:
                ++p;
                break;
            case 0xCC: p += 2; break;  // PORT
            case 0xCD: {               // XCMD
                const uint8_t x = u8(p++);
                if (x == 1) {  // xwave: 파형을 직접 고른다
                    wave(u32(p), false);
                    wave(u32(p), true);
                    p += 4;
                } else if ((x >= 2 && x <= 11) && x != 3) {
                    ++p;
                } else if (x == 12) {
                    p += 2;
                } else if (x == 13) {
                    p += 4;
                } else {
                    end = true;  // 재생기도 트랙을 끝낸다
                }
                break;
            }
            case 0xCE:  // EOT: 0x80 미만 인자 0~1개
                if (u8(p) < 0x80) ++p;
                break;
            default: end = true; break;  // 재생기도 트랙을 끝낸다
            }
        }
        add(runStart, p > runStart ? p - runStart + 8 : 8);  // 끝에서 다음 바이트를 엿보므로 조금 더
    }
    return true;
}

void RamSongWalker::voice(uint32_t a, bool sub) {
    if (!isDataAddr(a) || budget_ <= 0) return;
    add(a, 12);
    const uint8_t type = u8(a);
    const uint32_t wav = u32(a + 4);
    if (type & 0xC0) {  // 건반 나눔(0x40)·드럼(0x80): 하위 악기표. 재생기도 한 단계만 따라간다
        if (sub || !isDataAddr(wav)) return;
        if (type & 0x40) {
            const uint32_t table = u32(a + 8);
            if (!isDataAddr(table)) return;
            add(table, 128);
            std::set<uint8_t> used;
            for (uint32_t k = 0; k < 128; ++k) used.insert(u8(table + k));
            for (uint8_t i : used) voice(wav + i * 12u, true);
        } else {
            for (uint32_t k = 0; k < 128; ++k) voice(wav + k * 12u, true);
        }
        return;
    }
    // XCMD 가 악기 종류를 바꿀 수 있으므로 파형은 두 가지로 다 본다
    wave(wav, (type & 7) != 0);
}

// cgb: 게임보이 소리 채널의 파형(16바이트), 아니면 다이렉트 사운드 파형(머리 16바이트 + 표본)
void RamSongWalker::wave(uint32_t w, bool cgb) {
    if (!isRamAddr(w)) return;  // 롬에 있으면 복사할 것이 없다
    if (cgb) {
        add(w, 16);
        return;
    }
    if (!waves_.insert(w).second) return;
    const uint32_t size = u32(w + 12);
    const uint16_t wtype = (uint16_t) (u8(w) | (u8(w + 1) << 8));
    if (size == 0 || size >= 0x40000u) {
        add(w, 16);
        return;
    }
    const uint32_t bytes = wtype ? 16 + ((size + 63) / 64) * 0x21 : 16 + size;
    add(w, bytes + 1);
}

} // namespace

std::shared_ptr<const RamImage> copyRamSong(GbaBus& bus, uint32_t header) {
    auto img = std::make_shared<RamImage>();
    RamSongWalker walker(bus);
    if (!walker.walk(header)) {
        // 따라가지 못함: RAM 전체
        RamSegment ew, iw;
        ew.addr = 0x02000000u;
        ew.bytes.resize(0x40000);
        bus.read(ew.addr, ew.bytes.data(), ew.bytes.size());
        iw.addr = 0x03000000u;
        iw.bytes.resize(0x8000);
        bus.read(iw.addr, iw.bytes.data(), iw.bytes.size());
        img->push_back(std::move(ew));
        img->push_back(std::move(iw));
        return img;
    }
    // 겹치거나 가까운 조각을 합친다
    std::vector<std::pair<uint32_t, uint32_t>> r = walker.ranges;
    std::sort(r.begin(), r.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;  // (시작, 끝)
    for (const auto& x : r) {
        const uint32_t s = x.first, e = x.first + x.second;
        if (!merged.empty() && s <= merged.back().second + 32 && ramEnd(s) == ramEnd(merged.back().first)) {
            merged.back().second = std::max(merged.back().second, std::min(e, ramEnd(s)));
        } else {
            merged.emplace_back(s, e);
        }
    }
    for (const auto& m : merged) {
        RamSegment seg;
        seg.addr = m.first;
        seg.bytes.resize(m.second - m.first);
        bus.read(seg.addr, seg.bytes.data(), seg.bytes.size());
        img->push_back(std::move(seg));
    }
    return img;
}
