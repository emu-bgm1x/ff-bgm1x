# mGBA (GBA, GB/GBC) 이식 안내서 — ff-bgm1x

기준: mGBA **0.10.5**. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

## 1. 구조 한눈에

| 새 파일 | 하는 일 |
|---|---|
| `module/src/ffbgm-common/` | 에뮬레이터와 무관한 GBA 코드. `rom` (ROM 읽기, RAM 사본 겹쳐 보기, 곡 목록 번호 찾기 `findSongNumbers`), `mp2k_engine` (GBA 표준 사운드 엔진 MP2K 의 곡 재생기, RAM 곡용 `startWithRam`), `bgm_mirror` (게임의 MP2K 플레이어 상태를 프레임마다 읽어 곡을 분류하고, 음악·팡파레·효과음을 대체 재생기로 1배속 연주, 원본 플레이어 트랙은 무음 처리, 소리 분류의 사용자 지정·볼륨·나온 횟수), `sound_catalog` (소리 분류 목록과 게임별 파일, 표준 C++ 만 사용). `BgmMultiMirror` 가 플레이어마다 재생기를 하나씩 둔다. |
| `module/src/platform/qt/BgmReplacer.cpp/.h` | mGBA 와의 연결. 프레임마다 `BgmMultiMirror` 를 돌리고, 실시간 출력과 녹화 스트림에 1배속 소리를 섞는다. GB/GBC 면 `GbDriverBgm` 로 넘긴다. 소리 분류 목록(`SoundCatalog`)을 가진다. |
| `module/src/platform/qt/GbDriverBgm.cpp/.h` | GB/GBC: 원본 코어의 채널 상태에서 지금 곡·효과음 번호를 읽어 자체 재생기(`GscPlayer`)에 넘기고, 원본 코어의 해당 하드웨어 채널은 무음. 소리 분류(곡·효과음·울음소리 전체), 원본 채널 볼륨, 울음소리 1배속. 엔진은 ROM 패턴으로 찾는다 (지금은 GB 2세대 사운드 엔진 하나). |
| `module/src/platform/qt/GscPlayer.cpp/.h` | GB/GBC: GB 2세대 사운드 엔진(pret 의 2세대 디컴파일 소스 `audio/engine.asm`)을 옮긴 재생기와 GB 사운드 합성기. 실제 시간으로 1배속 연주, 울음소리(`_PlayCry`), 채널별 볼륨. 에뮬레이터 코어와 무관 |
| `module/src/platform/qt/FfSoundCatalogDialog.cpp/.h` | 소리 분류 창 (Qt). 창이 앞이면 게임 일시정지, 에뮬레이터 창을 누르면 수집. 재생·분류·볼륨·이름·가져오기/내보내기 |
| `module/src/platform/qt/FfSoundPreview.cpp/.h` | 분류 창의 재생. SDL 오디오 장치를 따로 열어 일시정지 중에도 낸다 (GBA 는 `Mp2kEngine`, GB 는 `GscPlayer`) |
| `module/src/platform/qt/EpoxyStaticInit.cpp` | (선택) 공식판처럼 exe 하나로 만드는 **정적 빌드**에서만 필요. 정적 libepoxy 초기화. 일반 빌드는 무관 |

GBA 에서 게임 메모리를 읽는 위치는 하드코딩하지 않고 ROM 에서 MP2K 플레이어 표 패턴으로 찾는다 (여러 게임 지원이 목표).

에뮬레이터 내부 의존 (새 버전에서 확인할 것): `mCore` (`busRead*`/`busWrite*`, `rawRead8`, `board`, `platform`, `frequency`, `getAudioChannel`, `setAVStream`), `mCoreInitConfig`, `mAVStream` 콜백 구조, `<mgba/internal/gba/gba.h>`, `<mgba/internal/gb/gb.h>` (`gb->audio.forceDisableCh`, 새로 넣은 `ffbgmGainQ8`), Qt `ConfigController` 옵션, `Action`/`ConfigOption`, `CoreController::setPaused`/`isPaused`.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `src/platform/qt/CMakeLists.txt` | 앞부분 | `src/ffbgm-common` 의 파일(`rom`, `mp2k_engine`, `bgm_mirror`, `sound_catalog`, `winutil.h`)을 빌드 폴더로 복사해 `gbabgm` 정적 라이브러리(C++17)로 빌드하고 링크. `SOURCE_FILES` 에 `BgmReplacer.cpp`, `FfSoundCatalogDialog.cpp`, `FfSoundPreview.cpp`, `GbDriverBgm.cpp`, `GscPlayer.cpp`. (정적 libepoxy 일 때만 `EpoxyStaticInit.cpp`) |
| `src/platform/qt/CoreController.h` | 멤버 | `std::unique_ptr<BgmReplacer> m_bgm`, `bool m_bgmSpeedHold`, `bgmReplacer()` (분류 창용) |
| `src/platform/qt/CoreController.cpp` | 생성자 | `m_bgm.reset(new BgmReplacer(core))` |
| 〃 | 코어 리셋 콜백 | `m_bgm->onReset(core)` |
| 〃 | 프레임 콜백, `finishFrame()` 전 | `m_bgm->onFrame(core)`; `speedHold()` 가 바뀌면 `updateFastForward()` — 연출 중 배속 멈춤 |
| 〃 | `loadConfig` | 설정 `bgmReplace`(1), `bgmFfCutscenes`(0), `bgmSfx1x`(1), `bgmRamSongs`(0), `bgmSfxVolume`(256)/`bgmSfxMute`(0), `volume`/`mute` 를 BgmReplacer 에 전달 |
| 〃 | `setAVStream` / `clearAVStream` | `m_bgm->wrapStream(...)` / `unwrapStream()` — 녹화(영상 저장)에도 1배속 소리가 들어가게 스트림을 감싼다 |
| 〃 | `updateFastForward` | `m_bgmSpeedHold` 면 배속하지 않음 |
| `src/platform/qt/AudioDevice.cpp/.h` | `setFormat`, `readData` 끝 | 샘플레이트 기억, `BgmReplacer::mixActive(data, frames, rate)` — Qt Multimedia 출력에 섞기 |
| `src/platform/sdl/sdl-audio.c/.h` | 전역 함수 포인터, `_mSDLAudioCallback` | `mSDLAudioPostMix` 훅 — SDL 오디오 출력을 쓸 때 섞기 (BgmReplacer 가 등록) |
| `src/platform/qt/Window.cpp` | `setupMenu`, Audio/Video 메뉴 | 체크 항목 4개 (BGM 1배속 유지 / 연출 중에도 배속 / 효과음 1배속 / RAM 곡 읽기), 설정값 기본값 등록, "소리 분류..." 항목(`ffbgmSoundCatalog`, 단축키 설정에 나옴) |
| 〃 | `closeEvent` | `FfSoundCatalogDialog::closeIfOpen()` — 분류 창을 먼저 닫는다 |
| 〃 | `setupOptions` | `bgmSfxVolume`, `bgmSfxMute` 가 바뀌면 `reloadConfig()` |
| `src/platform/qt/SettingsView.ui/.cpp` | 오디오 설정, 빨리 감기 볼륨 줄 **바로 아래** | "효과음 볼륨" 슬라이더(0~256) + 무음 체크, 저장/불러오기. 아래 줄들의 row 번호를 하나씩 민다 |
| `include/mgba/internal/gb/audio.h`, `src/gb/audio.c` | `GBAudio` 구조체, `GBAudioInit`, `GBAudioSamplePSG` | `ffbgmGainQ8[4]` (채널별 볼륨 배율, 256 = 그대로). 채널 1~3 은 배율을 곱해 따로 더하고, 모두 256 이면 원래 계산과 같은 값 |

## 3. 소리 분류 (분류 창)

- 이름표: GBA 곡 `song:<곡 머리 주소 8자리>`, RAM 곡 `cry:all`, GB 곡 `gbsong:<번호>`, GB 효과음 `gbsfx:<번호>`, GB 울음소리 `cry:all`.
  저장: 실행 파일 옆 `ff-bgm1x-sounds/<게임 코드>.txt` (GBA 는 롬 머리의 게임 코드, GB 는 `GB-<제목>-<체크섬>`).
  파일 한 줄: `<이름표> <분류> [vol=N] [named] [# 이름]` (`named` 가 있으면 `#` 뒤가 사용자가 붙인 이름).
- 창이 닫혀 있고 지정도 없으면 `SoundCatalog::Active()` 가 false 라 에뮬레이션 쪽은 목록을 보지 않는다 (나온 횟수 세기, 지정 읽기 모두 건너뜀).
- GBA 분류: `classify()` 의 자동 판단을 사용자 지정으로 바꾼다 (`effectiveKind`). 효과음 대체 여부는 `wantSe`.
  분류 창에서 지정을 바꾸면 `refreshUser` 가 볼륨은 바로, 분류가 바뀐 플레이어는 곡을 다시 판단하게 한다.
- GBA 원본 소리 볼륨: 대체하지 않는 플레이어의 트랙 `volX` 에 배율을 곱해 넣는다 (`applyNativeGain`). 게임이 volX 를 바꾸면 그 값을 새 기준으로 삼는다.
  100% 초과는 트랙 볼륨 계산(`vol * volX >> 5` 가 u8 로 들어감)이 넘치지 않게 위쪽을 자른다.
- GBA 곡 목록에 없는 플레이어: `MPlayOpen` 이 `SoundInfo.musicPlayerHead`(+0x24) 부터 `MusicPlayerInfo.musicPlayerNext`(+0x3C) 로 잇는다.
  `locate` 에서 이 목록을 따라가 곡 목록(`gMPlayTable`)에 없는 플레이어를 `extra` 로 넣는다. `extra` 는 지정이 없으면 대체하지 않는다.
- RAM 곡(곡 머리가 0x02/0x03 주소): 이름표 `cry:all`, 자동 판단은 아무것도 아님. "RAM 곡 읽기" 가 켜져 있고 1배속으로 정했으면,
  시작하는 순간 EWRAM 256KB·IWRAM 32KB 를 복사해 `Mp2kEngine::startWithRam` 으로 낸다 (엔진은 `Rom` 의 `ewram`/`iwram` 겹쳐 보기로 읽는다).
- GB 울음소리 1배속: 원본 효과음 채널 구조체에서 음높이 보정(`CHANNEL_PITCH_OFFSET` +39)·길이(`CHANNEL_TEMPO` +25)·좌우 출력(`CHANNEL_TRACKS` +27)을 읽어
  `GscPlayer::requestCry` 로 넘긴다. 재생기의 `playCry` 는 엔진의 `_PlayCry` 와 같고, 끝날 때 `RestoreVolume` 처럼 음량을 되돌린다.
  울음소리 표 주소는 `findGscTables` 가 `_PlayCry` 의 `ld hl,Cries` 에서 읽는다.
- GB 원본 소리 볼륨: 코어의 `gb->audio.ffbgmGainQ8[i]` (2절 마지막 줄). 1배속 쪽은 `GscPlayer::setGains(곡, 효과음)` 이 하드웨어 채널마다 고른다.
- 분류 창의 일시정지: 창이 활성화되면 `CoreController::setPaused(true)`, 우리가 건 일시정지만 푼다.
- 재생(`FfSoundPreview`): 롬을 복사해 둔 재생기로 내므로 재생 중에 게임을 닫아도 안전하다. RAM 곡·GB 울음소리는 마지막으로 1배속으로 낸 것을 다시 낸다.

## 4. 진행 순서

1. 공식 저장소(`https://github.com/mgba-emu/mgba`)에서 최신 릴리스 태그를 받는다.
2. `module/` 를 mGBA 소스 루트에 덮어 복사한다 (에뮬레이터와 무관한 코드는 `src/ffbgm-common/` 에 들어간다).
3. mGBA 소스 루트에서 `git apply -3 <kit>/mgba/hooks.patch`. 충돌은 위 표의 '역할' 기준으로 옮긴다.
4. 1절 의존 API 확인.
5. 빌드. 원래는 Windows MSYS2 UCRT64. **libmgba 는 정적(BUILD_STATIC=ON, BUILD_SHARED=OFF)** 으로 링크해야 한다. Qt 쪽 코드는 C++14 로 빌드되므로(`std::size` 없음, `static constexpr` 를 참조로 넘기면 링크 오류) 주의.
6. 아래 확인 방법.

## 5. 확인 방법 (게임 필요, 사람이 듣고 판단)

GBA (MP2K 게임), 배속을 켜고:
- 필드 BGM 이 1배속, 음정이 원래와 같다.
- 팡파레는 1배속으로 끝까지, 게임이 다음 곡을 시작하면 끊긴다.
- 연출 곡(반복 없는 12초 넘는 곡) 동안 배속이 멈춘다 (기본).
- 메뉴 선택음·대화음·문 소리 등 효과음이 1배속으로 끝까지 난다.
- 효과음 볼륨/무음이 효과음에만 적용된다.
- 배속 중에 "BGM 1배속 유지" 를 끄면 원본 BGM 이 바로 나온다 (지금 곡을 처음부터).
- 영상 녹화에도 1배속 BGM 이 들어간다.

GB/GBC (지원 엔진 게임):
- 배속 중 BGM 1배속, 대화음·메뉴음·아이템 받는 소리가 1배속. 울음소리는 기본값에서 게임 속도.
- 효과음 볼륨 슬라이더는 GB 에 적용되지 않는다 (효과음과 음악이 자체 재생기 하나에서 섞여 나온다) — 의도된 제약.

소리 분류 창:
- 창이 앞이면 게임이 멈추고, 에뮬레이터 창을 누르면 진행되며 소리가 목록에 쌓인다. 나온 횟수가 소리를 낼 때마다 는다.
- 분류·볼륨을 바꾸면 바로 반영되고(지금 나오는 소리 포함), 게임을 다시 켜도 지정이 남아 있다.
- "RAM 곡 읽기" 를 켜고 울음소리 전체를 효과음으로 정하면 배속 중 울음소리가 1배속, 연출로 정하면 울음소리 동안 배속이 멈춘다.

## 6. 알려진 함정

- **한글 등 비ASCII 경로**: 소스나 빌드 폴더 경로에 한글이 있으면 Qt moc / 오브젝트 경로 문제로 빌드가 깨진다. 그래서 `src/ffbgm-common/` 을 빌드 폴더로 복사해 쓴다. 빌드는 ASCII 경로에서.
- **libmgba DLL 빌드**: 동적 libmgba 로 빌드하면 pseudo relocation 크래시가 났다. 정적으로 링크할 것.
- **정적 exe 빌드**(선택): 공식판처럼 exe 하나로 만들려면 Qt 정적판, `.a` 만 찾도록 하는 CMake 설정, `USE_LIBZIP=OFF`, libepoxy DllMain 대신 `EpoxyStaticInit.cpp` 의 TLS 콜백이 필요했다.
- GB 엔진 프로필은 하나뿐이다. 다른 GB 게임은 프로필을 추가해야 한다 (`detectGbDriver`).
- **잠금**: `BgmMultiMirror` 의 `onFrame` 등은 내부 잠금이 없고 `render` 만 잠근다. 에뮬레이션 스레드의 `BgmReplacer::onFrame` 도
  오디오 쪽(`mixActive`)과 같은 잠금(`s_activeMutex`)을 잡아야 한다. 빠뜨리면 롬을 다시 열 때 힙이 깨질 수 있다.
- **GB/GBC 자체 재생기 (`GscPlayer`)**: 예전에는 숨겨진 두 번째 GB 코어(mGBA 0.10.5 의 GB 코어)에서 게임 자신의 사운드 드라이버를 돌렸다.
  그런데 그 코어의 출력에만 음이 바뀔 때마다 직류가 크게 출렁이는 노이즈가 섞였다(같은 효과음 1초에서 20ms 평균의 출렁임/크기 비율:
  앱 안 원본 코어 0.03, 숨겨진 코어 0.49). 사운드 레지스터 쓰기는 원본과 같았고 원인은 찾지 못해 이 방식을 버렸다.
  지금은 엔진의 `audio/engine.asm` 을 함수 단위로 옮긴 엔진과 자체 GB 사운드 합성기(채널을 0 중심으로 출력,
  mGBA 와 같은 축전기 필터)로 낸다. 엔진 표(곡·효과음·울음소리·음 높이·파형·드럼, `wLowHealthAlarm`)는 엔진 코드의 바이트 패턴으로 찾는다(`findGscTables`).
  에뮬레이터 코어를 쓰지 않으므로 mGBA·VBA-M·Gambatte 가 같은 파일을 쓴다.
- **GB 낮은 체력 경고음**: 엔진의 `PlayDanger` 는 1번 하드웨어 채널에 직접 써서, 원본 쪽 음악 채널 무음 처리에 함께 걸린다.
  켜짐 여부(`wLowHealthAlarm` 비트 7)만 원본에서 읽고, 박자(30프레임 주기)는 재생기가 실제 시간으로 낸다.
- **GB 게임 설정 바이트(스테레오)**: 재생기는 원본의 설정 바이트(wOptions, 롬에서 `Music_StereoPanning` 패턴으로 찾은 `optionsAddr`)를
  매 프레임 받아 스테레오 판단에 쓴다. CGB 는 D000~DFFF 가 WRAM 뱅크라 뱅크 1 에서 읽는다(`rawRead8(addr, 1)`).
- **테스트용 실행**: 정적 빌드가 아닌 일반 빌드 exe 를 다른 폴더로 옮겨 실행하면, 그 PC 의 MSYS2 FFmpeg DLL 이 요구하는 DLL 이 빠져 실행되지 않을 수 있다.
- 줄바꿈: 이 자료는 LF.

## 7. libretro 코어 (안드로이드 Lemuroid 용)

`src/platform/libretro/` 에 같은 재생기를 붙였습니다. 앱 쪽은 `lemuroid/PORTING-AI.md` 를 보세요. 소리 분류 창은 PC 판만 있습니다
(공용 코드는 분류 목록 없이도 그대로 동작합니다).

| 파일 | 역할 |
|---|---|
| `module/src/platform/libretro/ff_bgm1x.cpp/.h` | 재생기(`BgmReplacer`)를 코어에 붙이고 `ff_bgm1x_mix`·`ff_bgm1x_speed_hold` 를 내보낸다 |
| `hooks/src__platform__libretro__libretro.c.patch` | 코어 불러오기·리셋·프레임마다 재생기 호출, 코어 옵션 읽기, 색 보정·프레임 섞기(libretro/mgba 에서 가져옴) |
| `hooks/src__platform__libretro__libretro_core_options.h.patch` | 코어 옵션 정의 |
| `hooks/CMakeLists.txt.patch` | `BUILD_LIBRETRO` 에 C++ 소스(`src/ffbgm-common` 전체, qt `BgmReplacer`·`GbDriverBgm`·`GscPlayer`) 추가 |

빌드: `android/build_mgba_libretro.sh` (Android NDK·CMake, `MGBA_SRC` 에 소스 폴더).
