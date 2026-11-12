# mGBA (GBA, GB/GBC) 이식 안내서 — ff-bgm1x

기준: mGBA **0.10.5**. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

## 1. 구조 한눈에

| 새 파일 | 하는 일 |
|---|---|
| `module-common/` → mGBA 소스 폴더 **옆**의 `common/` | 에뮬레이터와 무관한 GBA 코드. `rom` (ROM 읽기), `mp2k_engine` (GBA 표준 사운드 엔진 MP2K 의 곡 재생기), `bgm_mirror` (게임의 MP2K 플레이어 상태를 프레임마다 읽어 곡을 분류하고, 음악·팡파레·효과음을 대체 재생기로 1배속 연주, 원본 플레이어 트랙은 무음 처리). `BgmMultiMirror` 가 플레이어마다 재생기를 하나씩 둔다. |
| `module/src/platform/qt/BgmReplacer.cpp/.h` | mGBA 와의 연결. 프레임마다 `BgmMultiMirror` 를 돌리고, 실시간 출력과 녹화 스트림에 1배속 소리를 섞는다. GB/GBC 면 `GbDriverBgm` 로 넘긴다. |
| `module/src/platform/qt/GbDriverBgm.cpp/.h` | GB/GBC: 숨겨진 두 번째 GB 코어에 같은 ROM 을 올리고, 게임 자체의 사운드 드라이버(곡 시작/프레임 갱신/효과음 시작 루틴)만 실시간으로 불러 소리를 만든다. 원본 코어의 해당 하드웨어 채널은 무음. 엔진은 ROM 패턴으로 찾는다 (지금은 포켓몬 금·은·크리스탈 엔진 하나). |
| `module/src/platform/qt/EpoxyStaticInit.cpp` | (선택) 공식판처럼 exe 하나로 만드는 **정적 빌드**에서만 필요. 정적 libepoxy 초기화. 일반 빌드는 무관 |

GBA 에서 게임 메모리를 읽는 위치는 하드코딩하지 않고 ROM 에서 MP2K 플레이어 표 패턴으로 찾는다 (여러 게임 지원이 목표).

에뮬레이터 내부 의존 (새 버전에서 확인할 것): `mCore` (`busRead*`/`busWrite*`, `board`, `platform`, `frequency`, `getAudioChannel`, `setAVStream`; GB 숨겨진 코어용 `init`/`loadROM`/`reset`/`runFrame`/`setVideoBuffer`/`setAudioBufferSize`), `mCoreInitConfig`, `mAVStream` 콜백 구조, `<mgba/internal/gba/gba.h>`, `<mgba/internal/gb/gb.h>`, Qt `ConfigController` 옵션, `Action`/`ConfigOption`.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `src/platform/qt/CMakeLists.txt` | 앞부분 | `../common` 의 파일을 빌드 폴더로 복사해 `gbabgm` 정적 라이브러리(C++17)로 빌드하고 `${BINARY_NAME}-qt` 에 링크. `SOURCE_FILES` 에 `BgmReplacer.cpp`, `GbDriverBgm.cpp`. (정적 libepoxy 일 때만 `EpoxyStaticInit.cpp`) |
| `src/platform/qt/CoreController.h` | 멤버 | `std::unique_ptr<BgmReplacer> m_bgm`, `bool m_bgmSpeedHold` |
| `src/platform/qt/CoreController.cpp` | 생성자 | `m_bgm.reset(new BgmReplacer(core))` |
| 〃 | 코어 리셋 콜백 | `m_bgm->onReset(core)` |
| 〃 | 프레임 콜백, `finishFrame()` 전 | `m_bgm->onFrame(core)`; `speedHold()` 가 바뀌면 `updateFastForward()` — 연출 중 배속 멈춤 |
| 〃 | `loadConfig` | 설정 `bgmReplace`(1), `bgmFfCutscenes`(0), `bgmSfx1x`(1), `bgmSfxVolume`(256)/`bgmSfxMute`(0), `volume`/`mute` 를 BgmReplacer 에 전달 |
| 〃 | `setAVStream` / `clearAVStream` | `m_bgm->wrapStream(...)` / `unwrapStream()` — 녹화(영상 저장)에도 1배속 소리가 들어가게 스트림을 감싼다 |
| 〃 | `updateFastForward` | `m_bgmSpeedHold` 면 배속하지 않음 |
| `src/platform/qt/AudioDevice.cpp/.h` | `setFormat`, `readData` 끝 | 샘플레이트 기억, `BgmReplacer::mixActive(data, frames, rate)` — Qt Multimedia 출력에 섞기 |
| `src/platform/sdl/sdl-audio.c/.h` | 전역 함수 포인터, `_mSDLAudioCallback` | `mSDLAudioPostMix` 훅 — SDL 오디오 출력을 쓸 때 섞기 (BgmReplacer 가 등록) |
| `src/platform/qt/Window.cpp` | `setupMenu`, Audio/Video 메뉴 | 체크 항목 3개 (BGM 1배속 유지 / 연출 중에도 배속 / 효과음 1배속), 설정값 기본값 등록 |
| 〃 | `setupOptions` | `bgmSfxVolume`, `bgmSfxMute` 가 바뀌면 `reloadConfig()` |
| `src/platform/qt/SettingsView.ui/.cpp` | 오디오 설정, 빨리 감기 볼륨 줄 **바로 아래** | "효과음 볼륨" 슬라이더(0~256) + 무음 체크, 저장/불러오기. 아래 줄들의 row 번호를 하나씩 민다 |

## 3. 진행 순서

1. 공식 저장소(`https://github.com/mgba-emu/mgba`)에서 최신 릴리스 태그를 받는다.
2. `module/` 를 mGBA 소스 루트에 덮어 복사하고, `module-common/` 은 mGBA 소스 폴더와 **같은 부모 폴더**에 `common/` 이라는 이름으로 둔다 (CMakeLists 가 `${CMAKE_SOURCE_DIR}/../common` 을 읽는다).
3. mGBA 소스 루트에서 `git apply -3 <kit>/mgba/hooks.patch`. 충돌은 위 표의 '역할' 기준으로 옮긴다.
4. 1절 의존 API 확인.
5. 빌드. 원래는 Windows MSYS2 UCRT64. **libmgba 는 정적(BUILD_STATIC=ON, BUILD_SHARED=OFF)** 으로 링크해야 한다.
6. 아래 확인 방법.

## 4. 확인 방법 (게임 필요, 사람이 듣고 판단)

GBA (예: 포켓몬 에메랄드/루비/사파이어/파이어레드 등 MP2K 게임), 배속을 켜고:
- 필드 BGM 이 1배속, 음정이 원래와 같다.
- 팡파레는 1배속으로 끝까지, 게임이 다음 곡을 시작하면 끊긴다.
- 연출 곡(반복 없는 12초 넘는 곡) 동안 배속이 멈춘다 (기본).
- 메뉴 선택음·대화음·문 소리 등 효과음이 1배속으로 끝까지 난다.
- 효과음 볼륨/무음이 효과음에만 적용된다.
- 배속 중에 "BGM 1배속 유지" 를 끄면 원본 BGM 이 바로 나온다 (지금 곡을 처음부터).
- 영상 녹화에도 1배속 BGM 이 들어간다.

GB/GBC (포켓몬 금·은·크리스탈):
- 배속 중 BGM 1배속, 대화음·메뉴음·아이템 받는 소리가 1배속. 울음소리는 게임 속도.
- 효과음 볼륨은 GB 에 적용되지 않는다 (효과음과 음악이 숨겨진 코어 하나에서 섞여 나온다) — 의도된 제약.

## 5. 알려진 함정

- **한글 등 비ASCII 경로**: 소스나 빌드 폴더 경로에 한글이 있으면 Qt moc / 오브젝트 경로 문제로 빌드가 깨진다. 그래서 `common/` 을 빌드 폴더로 복사해 쓴다. 빌드는 ASCII 경로에서.
- **libmgba DLL 빌드**: 동적 libmgba 로 빌드하면 pseudo relocation 크래시가 났다. 정적으로 링크할 것.
- **정적 exe 빌드**(선택): 공식판처럼 exe 하나로 만들려면 Qt 정적판, `.a` 만 찾도록 하는 CMake 설정, `USE_LIBZIP=OFF`, libepoxy DllMain 대신 `EpoxyStaticInit.cpp` 의 TLS 콜백이 필요했다.
- GB 엔진 프로필은 하나뿐이다. 다른 GB 게임은 프로필을 추가해야 한다 (`detectGbDriver`).
- 줄바꿈: 이 자료는 LF.

## 6. libretro 코어 (안드로이드 Lemuroid 용)

`src/platform/libretro/` 에 같은 재생기를 붙였습니다. 앱 쪽은 `lemuroid/PORTING-AI.md` 를 보세요.

| 파일 | 역할 |
|---|---|
| `module/src/platform/libretro/ff_bgm1x.cpp/.h` | 재생기(`BgmReplacer`)를 코어에 붙이고 `ff_bgm1x_mix`·`ff_bgm1x_speed_hold` 를 내보낸다 |
| `hooks/src__platform__libretro__libretro.c.patch` | 코어 불러오기·리셋·프레임마다 재생기 호출, 코어 옵션 읽기, 색 보정·프레임 섞기(libretro/mgba 에서 가져옴) |
| `hooks/src__platform__libretro__libretro_core_options.h.patch` | 코어 옵션 정의 |
| `hooks/CMakeLists.txt.patch` | `BUILD_LIBRETRO` 에 C++ 소스(`../common`, qt `BgmReplacer`·`GbDriverBgm`) 추가 |

빌드: `android/build_mgba_libretro.sh` (Android NDK·CMake, `MGBA_SRC` 에 소스 폴더).
