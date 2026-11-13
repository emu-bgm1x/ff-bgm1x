# Gambatte libretro (안드로이드 Lemuroid 의 GB/GBC) 이식 안내서 — ff-bgm1x

기준: libretro/gambatte-libretro 커밋 **`b752252`** (Lemuroid 1.17.0 에 들어 있는 Gambatte 코어가 보고하는 버전 `v0.5.0-netlink b752252`).
앱 쪽은 `lemuroid/PORTING-AI.md` 를 보세요.

## 1. 구조 한눈에

PC mGBA·VBA-M 판의 GB/GBC 방식과 같습니다. Gambatte 코어 안에 **숨겨진 두 번째 GB 코어(mGBA 0.10.5 의 GB 코어, 수정 없음)** 를 두고,
같은 ROM 에서 게임 자신의 사운드 드라이버만 실시간으로 돌려 1배속 소리를 만듭니다. Gambatte 쪽은 해당 채널만 출력에서 끕니다.
지원 엔진은 포켓몬 금·은·크리스탈 엔진(한글판 포함)이고, 다른 게임은 아무것도 바꾸지 않습니다.

| 새 파일 (`module/libgambatte/libretro/`) | 하는 일 |
|---|---|
| `ffbgm/gb_driver_bgm.cpp/.h` | VBA-M 판 `module/src/wx/ffbgm/gb_driver_bgm.*` 와 같은 파일. 엔진 찾기, 숨겨진 코어, 원본 상태 읽기(`GbMainCore`) |
| `ff_bgm1x_gb.cpp/.h` | Gambatte 와의 연결. `ff_bgm1x_mix`(출력 버퍼에 1배속 소리 더하기), `ff_bgm1x_speed_hold`(항상 false) 를 내보낸다. 프런트엔드가 `ff_bgm1x_mix` 를 한 번이라도 부르기 전에는 원본 채널을 끄지 않는다 |

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `libgambatte/src/sound.cpp/.h` | `PSG::mapSo` | 끌 채널 마스크(`setFfbgmMuteMask`)를 NR51 적용 단계에서 뺀다. 게임이 NR51 을 쓸 때와 같은 경로라 깔끔히 꺼진다. 마지막 NR51 을 기억해 마스크가 바뀌면 다시 적용 |
| `libgambatte/src/gambatte-memory.h` | `Memory` | I/O 레지스터 읽기(`ffbgmIo`), 채널 끄기 전달 |
| `libgambatte/include/gambatte.h`, `src/gambatte.cpp` | `GB` (`__LIBRETRO__` 블록) | `ffbgmRead`(C000~DFFF 현재 뱅크, FF00~), `ffbgmReadBank1`(D000~DFFF 를 WRAM 뱅크 1 에서), `ffbgmSetMuteMask` |
| `libgambatte/libretro/libretro.cpp` | `retro_load_game`, `retro_unload_game`, `retro_run`(프레임 끝), `retro_reset`, `retro_unserialize`, `check_variables` | 롬 넘기기, 매 프레임 원본 곡·효과음 상태 읽기와 채널 끄기, 리셋·상태 불러오기 때 숨겨진 코어 새로 맞추기, 옵션 읽기 |
| `libgambatte/libretro/libretro_core_options.h` | 옵션 표 | `gambatte_ff_bgm1x`(enabled), `gambatte_ff_bgm1x_sfx`(enabled) |
| `libgambatte/libretro/link.T` | 내보낼 심볼 | `ff_bgm1x_*` 추가 |
| `libgambatte/libretro/jni/Android.mk` | 빌드 | 숨겨진 코어 정적 라이브러리(`libmgba.a`)와 연결 코드 모듈(C++17)을 따로 만들어 `retro` 에 링크 |

## 3. 진행 순서

1. `android/build_mgba_gbcore_android.sh` 로 숨겨진 GB 코어를 빌드한다 (공식 mGBA 0.10.5 소스, `LIBMGBA_ONLY`, GB 만, Android NDK·CMake).
   결과 `mgba-gbcore-android/build-<abi>/libmgba.a`. `Android.mk` 는 이 위치와 mGBA 소스 위치를 `FFBGM_MGBA_BUILD`, `FFBGM_MGBA_SRC` 로 받는다.
2. gambatte-libretro 를 기준 커밋으로 받아 `module/` 복사, `git apply -3 hooks.patch`.
3. `libgambatte/libretro/jni` 에서 `ndk-build APP_ABI=arm64-v8a APP_CPPFLAGS=-std=gnu++11`.
   결과 `libgambatte/libretro/libs/<abi>/libretro.so` 를 Lemuroid 의
   `lemuroid-cores/bundled-cores/src/main/jniLibs/<abi>/libgambatte_libretro_android.so` 로 넣는다.

## 4. 확인 방법

- 포켓몬 금·은·크리스탈: 배속 중 BGM·효과음 1배속, 지직거림 없음, 설정에서 "BGM 1배속 유지" 를 끄면 원본 소리.
- 다른 GB 게임: 원래 Gambatte 와 같은지.

## 5. 알려진 함정

- **NDK 27 의 기본 C++ 표준(C++17)** 에서는 Gambatte 의 `std::auto_ptr` 가 없어 빌드가 안 된다. 공식 Makefile 이 일부 플랫폼에서 쓰는
  `-std=gnu++11` 을 `APP_CPPFLAGS` 로 준다. 연결 코드 모듈만 `-std=c++17`.
- 연결 코드 모듈도 본체와 같은 `COREFLAGS`(`__LIBRETRO__` 등)와 `INCFLAGS` 로 빌드해야 `gambatte.h` 의 선언이 맞는다.
- `crc32` 이름 겹침을 피하려고 숨겨진 코어와 연결 코드를 `-Dcrc32=mgba_crc32` 로 빌드한다.
- 숨겨진 코어의 소리 크기는 mGBA 기준이라 Gambatte 의 원래 소리 크기와 다를 수 있다 (확인하지 않음).
- 그 밖의 GB 숨겨진 코어 함정(소리 꺼내기, 게임 설정 바이트·스테레오)은 `vbam/PORTING-AI.md` 5절과 같다.
