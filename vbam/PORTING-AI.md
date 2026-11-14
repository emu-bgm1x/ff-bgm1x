# VBA-M (GBA, GB/GBC) 이식 안내서 — ff-bgm1x

기준: VisualBoyAdvance-M **2.2.3** (태그 `v2.2.3`, 커밋 `48ed03b`). 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.
mGBA 판(`mgba/`)과 같은 재생기 코드를 VBA-M 의 wx 프런트엔드에 연결한 것입니다.

## 1. 구조 한눈에

새 파일은 모두 `module/src/wx/ffbgm/` 에 있습니다.

| 새 파일 | 하는 일 |
|---|---|
| `bgm_mirror.*`, `mp2k_engine.*`, `rom.*`, `winutil.h` | mGBA 판의 `module-common/` 과 같은 GBA 재생기 (MP2K 곡 재생, 플레이어 상태 읽기, 곡 분류). UTF-8 BOM 으로 저장 (MSVC 가 한글 주석을 cp949 로 읽지 않게) |
| `gb_driver_bgm.*` | GB/GBC: 숨겨진 GB 코어(**mGBA 0.10.5 의 GB 코어**, MPL-2.0)에 같은 ROM 을 올려 게임 자신의 사운드 드라이버만 돌린다. mGBA 판 `GbDriverBgm` 과 같은 코드인데, 원본 코어를 `GbMainCore` 인터페이스(`r8`, `r8Bank1`, `setChannelsDisabled`)로 다룬다 |
| `vbam_bgm.cpp/.h` | VBA-M 과의 연결 (`ffbgm::OnGbaLoaded/OnGbLoaded/OnUnload/OnFrame/SpeedHold/MixRecording`). 1배속 소리는 **따로 연 SDL3 오디오 스트림**(48000Hz, float, 끌어가기 콜백)으로 낸다 |

왜 따로 스트림인가: VBA-M 은 코어가 프레임마다 소리를 드라이버에 밀어 넣고, 드라이버가 속도에 맞춰 재생 속도를 바꾸거나
배속 중 소리를 버린다(`speedup_mute`). 그래서 코어 쪽에 섞으면 1배속 소리도 같이 빨라지거나 버려진다.
스트림 장치는 VBA-M 오디오 설정에서 고른 장치(`kSoundAudioDevice`)와 이름이 같은 SDL 장치를 고른다.

GB 숨겨진 코어: VBA-M 의 GB 코어는 전역 상태라 하나 더 띄울 수 없어서, mGBA 의 GB 코어를 정적 라이브러리(`LIBMGBA_ONLY`, GB 만)로 함께 링크한다.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `src/core/gba/gbaSound.cpp/.h` | `apply_muting` | `soundSetFfbgmMuteMask(mask)`: 1배속으로 대신 내는 PSG 채널을 출력에서 끈다 (사운드 채널 켜기/끄기와 같은 자리) |
| `src/core/gb/gbSound.cpp/.h` | `apply_effects`, `gbSoundTick` 의 변경 감지 | `gbSoundSetFfbgmMuteMask(mask)`: GB 채널을 출력에서 끈다 |
| `src/wx/sys.cpp` | `systemFrame` | `ffbgm::OnFrame()` |
| 〃 | `systemReadJoypad` | `ffbgm::SpeedHold()` 이면 `KEYM_SPEED` 를 지운다 (연출 중 배속 멈춤) |
| 〃 | `systemOnWriteDataToSoundBuffer` | 녹화 중일 때만 녹화 쪽 사본에 1배속 소리를 더해 `AddFrame` (드라이버로 가는 소리는 그대로) |
| `src/wx/panel.cpp` | 롬 불러오기(패치 적용 뒤), `UnloadGame` | `OnGbaLoaded`/`OnGbLoaded`, `OnUnload` |
| `src/wx/config/option-id.h`, `option-proxy.h`, `internal/option-internal.cpp` | 설정 표 | `Sound/FfbgmBgm1x`(켬), `FfbgmFfCutscenes`(끔), `FfbgmSfx1x`(켬), `FfbgmSfxVolume`(0~256, 256) |
| `src/wx/cmdevents.cpp` | 명령 | 메뉴 체크 항목 3개의 토글 |
| `src/wx/xrc/MainMenu.xrc` | Options → Audio | 체크 항목 3개 |
| `src/wx/xrc/SoundConfig.xrc`, `src/wx/dialogs/sound-config.cpp` | 사운드 설정 창 | "SFX volume (1x)" 슬라이더 |
| `src/wx/CMakeLists.txt` | wx 대상 | `ffbgm/` 소스 추가, 캐시 변수 `FFBGM_MGBA_SOURCE_DIR`(mGBA 소스), `FFBGM_MGBA_BUILD_DIR`(mGBA 빌드), `mgba.lib`·`shlwapi`·`ws2_32` 링크 (키워드 없는 `target_link_libraries`) |

## 3. 진행 순서

1. 공식 저장소(`https://github.com/visualboyadvance-m/visualboyadvance-m`)에서 최신 릴리스 태그를 받는다.
2. `module/` 를 소스 루트에 덮어 복사하고 `git apply -3 <kit>/vbam/hooks.patch`. 충돌은 위 표의 '역할' 기준으로 옮긴다.
3. 숨겨진 GB 코어용 mGBA(공식 0.10.5 소스, 수정 없음)를 MSVC 로 빌드한다. x64 Native Tools 환경에서:
   ```
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIBMGBA_ONLY=ON -DM_CORE_GBA=OFF -DM_CORE_GB=ON
         "-DCMAKE_C_FLAGS=/utf-8 /Dcrc32=mgba_crc32" "-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG"
         "-DCMAKE_CXX_FLAGS=/utf-8 /Dcrc32=mgba_crc32" "-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG" <mgba 소스>
   ninja
   ```
   (`crc32` 는 VBA-M 이 링크하는 zlib 과 이름이 겹쳐서 바꾼다. `/MT` 는 VBA-M 의 정적 런타임과 맞춘다.)
4. VBA-M 을 공식 방식(MSVC + Ninja + vcpkg `x64-windows-static`)으로 빌드한다:
   `cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DUPSTREAM_RELEASE=OFF -DFFBGM_MGBA_SOURCE_DIR=<mgba 소스> -DFFBGM_MGBA_BUILD_DIR=<3의 빌드 폴더>` → `ninja`.
5. 아래 확인 방법.

## 4. 확인 방법 (게임 필요, 사람이 듣고 판단)

- GBA (MP2K 게임): 터보(배속) 중 BGM·효과음 1배속, 연출 곡에서 배속 멈춤, 효과음 볼륨 슬라이더.
- GB/GBC (포켓몬 금·은·크리스탈, **한글판 포함**): 배속 중 BGM 1배속, 지직거림이 없는지 (5절 마지막 항목).
- 최근 롬 목록에서 같은 롬을 여러 번 다시 열어도 꺼지지 않는지 (5절 잠금).
- 녹화(Tools → Record → Start sound/AVI recording)에 1배속 소리가 들어가는지.

## 5. 알려진 함정

- **공식 빌드 환경**: `UPSTREAM_RELEASE=ON` 은 공식 개발자의 코드 서명 인증서를 읽으려 해서 없으면 설정이 실패한다 → OFF.
  gettext 도구(CMake 가 받는 NuGet `Gettext.Tools`), `vcpkg/bootstrap-vcpkg.bat`, PATH 의 `zip.exe` 가 필요했다.
  셸에 `NoDefaultCurrentDirectoryInExePath=1` 이 있으면 `.bat` 를 전체 경로로 실행할 것.
- **잠금**: `BgmMultiMirror` 의 `onFrame`/`stopAll`/`setRuntimeParams` 는 내부 잠금이 없고 `render` 만 잠근다.
  에뮬레이션 스레드(프레임)와 오디오 콜백이 같은 잠금(`vbam_bgm.cpp` 의 `g_mtx`)을 써야 한다. 빠뜨리면 롬을 다시 열 때 힙이 깨져 꺼졌다.
- **Mute Sound / 배속 소리**: VBA-M 의 "배속 중 소리 끄기"(`speedup_mute`)를 켜 두면 1배속 소리는 나지만 울음소리 등 게임 속도 소리가 사라진다.
- **GB 숨겨진 코어 소리 꺼내기**: mGBA GB 오디오는 출력 버퍼에 일정량 이상 쌓이면 새 표본을 버린다(`gb/audio.c` `_sample`).
  쌓인 것을 먼저 꺼내고 모자랄 때만 한 프레임씩 돌린다(`GbShadowPlayer::mixInto`).
- **GB 게임 설정 바이트(스테레오)**: 숨겨진 코어는 게임 초기화를 거치지 않아 설정 바이트(wOptions)에 아무 값이나 있다.
  한글판 금에서는 이 값 때문에 드라이버가 스테레오로 판단해 매 프레임 원본(모노)과 다른 NR51 을 쓰고, 그 직후 원본 값으로 덮어쓰는 사이
  채널이 잠깐 꺼져 **초당 약 60번 지직거렸다**. 롬에서 `Music_StereoPanning` 패턴으로 설정 바이트 주소를 찾아
  매 프레임 원본 값을 복사한다(`optionsAddr`, `setOptions`). CGB 는 D000~DFFF 가 WRAM 뱅크라 뱅크 1 에서 읽는다(`r8Bank1`).
- MSVC: UTF-8 한글 주석은 BOM 이 있거나 `/utf-8` 이어야 한다.

## 6. libretro 코어 (안드로이드 Lemuroid 용, GBA 만)

`src/libretro/` 에 같은 GBA 재생기(`src/wx/ffbgm/` 의 `bgm_mirror`, `mp2k_engine`, `rom`)를 붙였습니다.
Lemuroid 에서 GBA 코어를 mGBA 와 VBA-M 중에 고를 수 있게 하는 데 씁니다 (앱 쪽은 `lemuroid/PORTING-AI.md`).
GB/GBC 는 Lemuroid 에서 Gambatte 코어가 맡으므로 libretro 판에는 넣지 않았습니다.

| 파일 | 역할 |
|---|---|
| `module/src/libretro/ff_bgm1x.cpp/.h` (새 파일) | 재생기 연결. `ff_bgm1x_mix(int16_t*, int frames, int rate)` 와 `ff_bgm1x_speed_hold()` 를 내보낸다 (mGBA libretro 와 같은 이름). 1배속 소리는 SDL 스트림 대신 프론트엔드가 이 함수로 출력 직전에 섞는다. 이 함수를 부르지 않는 프론트엔드에서는 원본 BGM 을 무음 처리하지 않는다 |
| `hooks/src__libretro__libretro.cpp.patch` | 게임 불러오기(GBA)·내리기·`retro_run` 끝에서 재생기 호출, 코어 옵션 읽기. 그리고 `retropad_device` 기본값을 조이패드로 (아래 함정) |
| `hooks/src__libretro__libretro_core_options.h.patch` | 옵션 4개: `vbam_ff_bgm1x`, `vbam_ff_bgm1x_cutscenes`, `vbam_ff_bgm1x_sfx`, `vbam_ff_bgm1x_sfx_volume` |
| `hooks/src__libretro__Makefile.common.patch` | 소스 4개 추가 |
| `hooks/src__libretro__link.T.patch` | `ff_bgm1x_*` 내보내기 |

빌드: `android/build_vbam_libretro.sh` (Android NDK `ndk-build`, `VBAM_SRC` 에 소스 폴더). 결과 `libvbam_libretro_android.so`.

함정: 원본 libretro 코어는 프론트엔드가 `retro_set_controller_port_device` 로 포트를 조이패드로 정해 줄 때만 버튼을 읽는다
(기본값 0 = 없음). Lemuroid 는 GBA 에서 이것을 부르지 않아 **버튼이 하나도 안 먹었다**. libretro 규칙대로 기본값을 조이패드로 바꿨다.
