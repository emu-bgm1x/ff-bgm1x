# Gambatte libretro (안드로이드 Lemuroid 의 GB/GBC) 이식 안내서 — ff-bgm1x

기준: libretro/gambatte-libretro 커밋 **`b752252`** (Lemuroid 1.17.0 에 들어 있는 Gambatte 코어가 보고하는 버전 `v0.5.0-netlink b752252`).
앱 쪽은 `lemuroid/PORTING-AI.md` 를 보세요.

## 1. 구조 한눈에

PC mGBA·VBA-M 판의 GB/GBC 방식과 같습니다. Gambatte 가 재생 중인 곡·효과음 번호를 읽어, GB 2세대
사운드 엔진을 옮긴 **자체 재생기(`GscPlayer`)** 가 실시간으로 1배속 소리를 만듭니다. Gambatte 쪽은 해당 채널만 출력에서 끕니다.
지원 엔진은 GB 2세대 사운드 엔진이고, 다른 게임은 아무것도 바꾸지 않습니다.

| 새 파일 (`module/libgambatte/libretro/`) | 하는 일 |
|---|---|
| `ffbgm/gb_driver_bgm.cpp/.h` | VBA-M 판 `module/src/wx/ffbgm/gb_driver_bgm.*` 와 같은 파일. 엔진 찾기, 원본 상태 읽기(`GbMainCore`), 재생기로 넘기기 |
| `ffbgm/gsc_player.cpp/.h` | 자체 재생기와 GB 사운드 합성기. mGBA 판 `GscPlayer.cpp/.h` 와 같은 파일 (include 경로만 다름) |
| `ff_bgm1x_gb.cpp/.h` | Gambatte 와의 연결. `ff_bgm1x_mix`(출력 버퍼에 1배속 소리 더하기), `ff_bgm1x_speed_hold`(항상 false) 를 내보낸다. 프런트엔드가 `ff_bgm1x_mix` 를 한 번이라도 부르기 전에는 원본 채널을 끄지 않는다 |

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `libgambatte/src/sound.cpp/.h` | `PSG::mapSo` | 끌 채널 마스크(`setFfbgmMuteMask`)를 NR51 적용 단계에서 뺀다. 게임이 NR51 을 쓸 때와 같은 경로라 깔끔히 꺼진다. 마지막 NR51 을 기억해 마스크가 바뀌면 다시 적용 |
| `libgambatte/src/gambatte-memory.h` | `Memory` | I/O 레지스터 읽기(`ffbgmIo`), 채널 끄기 전달 |
| `libgambatte/include/gambatte.h`, `src/gambatte.cpp` | `GB` (`__LIBRETRO__` 블록) | `ffbgmRead`(C000~DFFF 현재 뱅크, FF00~), `ffbgmReadBank1`(D000~DFFF 를 WRAM 뱅크 1 에서), `ffbgmSetMuteMask` |
| `libgambatte/libretro/libretro.cpp` | `retro_load_game`, `retro_unload_game`, `retro_run`(프레임 끝), `retro_reset`, `retro_unserialize`, `check_variables` | 롬 넘기기, 매 프레임 원본 곡·효과음 상태 읽기와 채널 끄기, 리셋·상태 불러오기 때 재생기 새로 맞추기, 옵션 읽기 |
| `libgambatte/libretro/libretro_core_options.h` | 옵션 표 | `gambatte_ff_bgm1x`(enabled), `gambatte_ff_bgm1x_sfx`(enabled) |
| `libgambatte/libretro/link.T` | 내보낼 심볼 | `ff_bgm1x_*` 추가 |
| `libgambatte/libretro/jni/Android.mk` | 빌드 | 연결 코드 모듈(C++17, `gsc_player.cpp` 포함)을 따로 만들어 `retro` 에 링크 |

## 3. 진행 순서

1. gambatte-libretro 를 기준 커밋으로 받아 `module/` 복사, `git apply -3 hooks.patch`.
2. `libgambatte/libretro/jni` 에서 `ndk-build APP_ABI=arm64-v8a APP_CPPFLAGS=-std=gnu++11`.
   결과 `libgambatte/libretro/libs/<abi>/libretro.so` 를 Lemuroid 의
   `lemuroid-cores/bundled-cores/src/main/jniLibs/<abi>/libgambatte_libretro_android.so` 로 넣는다.

## 4. 확인 방법

- 지원 엔진 게임: 배속 중 BGM·효과음 1배속, 지직거림 없음, 설정에서 "BGM 1배속 유지" 를 끄면 원본 소리.
- 다른 GB 게임: 원래 Gambatte 와 같은지.

## 5. 알려진 함정

- **NDK 27 의 기본 C++ 표준(C++17)** 에서는 Gambatte 의 `std::auto_ptr` 가 없어 빌드가 안 된다. 공식 Makefile 이 일부 플랫폼에서 쓰는
  `-std=gnu++11` 을 `APP_CPPFLAGS` 로 준다. 연결 코드 모듈만 `-std=c++17`.
- 연결 코드 모듈도 본체와 같은 `COREFLAGS`(`__LIBRETRO__` 등)와 `INCFLAGS` 로 빌드해야 `gambatte.h` 의 선언이 맞는다.
- 재생기의 소리 크기는 mGBA 기준이라 Gambatte 의 원래 소리 크기와 다를 수 있다.
- 자체 재생기로 바꾼 이유, 낮은 체력 경고음, 게임 설정 바이트·스테레오는 `vbam/PORTING-AI.md` 5절과 같다.
