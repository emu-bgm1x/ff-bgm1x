# Lemuroid (Android, GBA·GB/GBC) 이식 안내서 — ff-bgm1x

GBA·GB/GBC 를 안드로이드에서 돌리는 판입니다. 네 저장소가 한 앱이 됩니다.

| 폴더 | 원본 저장소 | 기준 커밋 | 하는 일 |
|---|---|---|---|
| `mgba/` (libretro 부분) | mgba-emu/mgba | 0.10.5 | GBA 코어. BGM·효과음 1배속, 색 보정·잔상(프레임 섞기)을 코어 안에서 한다 |
| `gambatte/` | libretro/gambatte-libretro | `b752252` | GB/GBC 코어. 숨겨진 mGBA GB 코어로 BGM·효과음 1배속 (포켓몬 금·은·크리스탈 엔진) |
| `libretrodroid/` | Swordfish90/LibretroDroid | `8835c30` | 코어를 돌리는 라이브러리. 코어의 1배속 소리를 출력에 섞고, 센서·늘이기를 넘긴다 |
| `lemuroid/` | Swordfish90/Lemuroid | `3625bc12` | 앱. 설정 화면, 배속(16배), 치트, 화면 방향, 버튼·화면 자유 배치 편집기 |

기능 설명은 맨 위 `FEATURES.md` 의 "모바일판" 을 보세요.

## 1. 구조 한눈에

- **소리**: PC mGBA 와 같은 재생기(`mgba/module-common/`, `mgba/module/src/platform/qt/BgmReplacer.*`)를 libretro 코어에도 넣었다.
  코어(`mgba/module/src/platform/libretro/ff_bgm1x.cpp`)가 두 함수를 내보낸다.
  - `ff_bgm1x_mix(int16_t* buf, size_t frames, int rate)` — 실제 시간 기준(1배속)의 BGM·효과음을 출력 버퍼에 더한다.
  - `ff_bgm1x_speed_hold()` — 연출 곡 중이면 1 (배속을 잠시 멈춤).
  LibretroDroid 는 코어를 열 때 `dlsym` 으로 두 함수를 찾고, 있으면 오디오 출력 뒤(리샘플 후)에 섞는다. 없는 코어에서는 아무것도 안 한다.
- **설정**: 코어 옵션 `mgba_ff_bgm1x`, `mgba_ff_bgm1x_cutscenes`, `mgba_ff_bgm1x_sfx`, `mgba_ff_bgm1x_sfx_volume`,
  `mgba_color_correction`, `mgba_interframe_blending`. 레무로이드 설정 화면(`GameSystem.kt`)에 드러낸다.
- **GB/GBC**: Gambatte 코어(`gambatte/`)가 같은 두 함수를 내보낸다. 옵션은 `gambatte_ff_bgm1x`, `gambatte_ff_bgm1x_sfx`.
  화면의 1배속 기능 버튼은 코어에 맞는 옵션 이름(`mgba_…` / `gambatte_…`)을 쓴다 (`BaseGameScreenViewModel.ffBgm1xKey`).
- **센서**: LibretroDroid `sensors.cpp` 가 NDK `ASensorManager` 로 가속도·자이로·조도를 읽어 libretro `GET_SENSOR_INTERFACE` 로 넘긴다
  (요시 퍼즐, 보크타이). 화면 회전에 맞춰 축을 돌린다(`setScreenRotation`).
- **배치 편집기**: `lemuroid-touchinput/.../custom/CustomLayout.kt` 가 중심. 레무로이드 원래 배치 위에 항목마다
  이동량(화면 비율)·배율을 덧씌운다(`Modifier.customLayoutElement`). 원래 자리는 `graphicsLayer` 바깥에서 재므로 이동·배율이
  들어가지 않은 값이다. 그룹, 기능 버튼(`CustomButtonFunction`, 키 번호 20000 이상), 숨긴 기본 버튼(크기 0)도 여기서 다룬다.
  편집 화면은 `lemuroid-app/.../game/CustomLayoutEditor.kt`, 저장은 `GameViewModelCustomLayout.kt`(기종·가로세로·기본/이 게임별 JSON).

## 2. 원본에 끼우는 지점

### libretrodroid (`hooks/`)

| 파일 | 역할 |
|---|---|
| `core.cpp/.h` | 코어를 열 때 `ff_bgm1x_mix`·`ff_bgm1x_speed_hold` 를 선택적으로 찾는다 |
| `audio.cpp/.h` | 출력 콜백에서 리샘플한 뒤 후처리 믹서(`setPostMix`)를 부른다 (뮤텍스로 보호) |
| `libretrodroid.cpp/.h` | 코어 불러올 때 후처리 믹서 연결, 내리기 전에 끊기. 배속 중 `speed_hold` 면 1배속으로. 센서 정리. 늘이기(`setStretch`)·화면 비율 읽기 |
| `environment.cpp` | `RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE` 응답 |
| `video*.cpp/.h`, `videolayout*` | 늘이기: 켜면 게임 비율 대신 영역 비율로 그린다 |
| `libretrodroidjni.cpp/.h`, `LibretroDroid.java`, `GLRetroView.kt` | 위 기능의 JNI·자바 쪽 (`setScreenRotation`, `setStretch`, `getAspectRatio`, 치트 적용) |
| `CMakeLists.txt` | `sensors.cpp` 추가, `android` 라이브러리 링크 |

### lemuroid (`hooks/`)

| 파일 | 역할 |
|---|---|
| `buildSrc/.../deps.kt` | LibretroDroid 를 원격 대신 로컬(mavenLocal, `com.swordfish:libretrodroid:unspecified`)에서 받는다 |
| `GameSystem.kt` | GBA 코어 옵션에 ff-bgm1x·색 보정·잔상 추가 (원래 있던 `mgba_frameskip` 항목은 값이 코어와 맞지 않아 뺐다). GB·GBC(Gambatte) 에 ff-bgm1x 옵션 2개 |
| `lemuroid-app/build.gradle.kts` | `abiFilters` 를 ABI 하나로 (실제 코어 파일을 모두 묶으면 APK 가 너무 커서). 기본 `arm64-v8a`, 32비트 기기용은 `-PffbgmAbi=armeabi-v7a` 로 따로 빌드 (이때 mGBA·Gambatte 코어도 armeabi-v7a 로 빌드해 넣는다) |
| `BaseGameScreenViewModel.kt` | 배속 배수, 화면 버튼 입력 가로채기(빨리 감기·기능 버튼), 1배속 옵션 켜기/끄기, 배치 저장·불러오기 |
| `BaseGameActivity.kt`, `GameMenu*` | 게임 메뉴: 배속 배수·화면 방향(이 게임), 치트 화면, 배치 편집기 열기 |
| `GameViewModelTouchControls.kt` | 메뉴 버튼 한 번 누르면 바로 메뉴 |
| `MobileGameScreen.kt` | 게임 화면에 배치 적용, 빨리 감기·기능 버튼 그리기, 편집기 띄우기, 늘이기·비율 전달 |
| `LemuroidControl*.kt` | 각 컨트롤에 `customLayoutElement` 붙이기 |
| `SettingsManager.kt`, `SettingsScreen.kt`, `keys.xml`, `strings.xml` | 기본 배속 배수·화면 방향 설정, 문구(영어·한국어) |

## 3. 진행 순서

0. **Gambatte 코어**: `gambatte/PORTING-AI.md` 대로 빌드해 `libgambatte_libretro_android.so` 로 넣는다.
1. **mGBA libretro 코어**: `mgba/` 를 PC 판과 같이 적용한 뒤 `mgba/android/build_mgba_libretro.sh` 로 빌드한다
   (Android NDK, CMake; `MGBA_SRC` 에 mGBA 소스 폴더). 나온 `libmgba_libretro_android.so` 를
   `Lemuroid/lemuroid-cores/bundled-cores/src/main/jniLibs/<abi>/` 에 넣는다.
2. **LibretroDroid**: module 복사, `git apply -3 hooks.patch`, `./gradlew :libretrodroid:publishToMavenLocal` (JDK 17).
3. **Lemuroid**: module 복사, `git apply -3 hooks.patch`, `./gradlew :lemuroid-app:assembleFreeBundleDebug` (JDK 17).

## 4. 확인 방법

- 배속을 켜고 BGM·효과음이 1배속인지 (포켓몬 파이어레드 등 MP2K 게임). 벽에 부딪히는 소리처럼 짧게 반복되는 효과음이
  원본과 1배속 소리로 겹쳐 들리지 않는지.
- 요시 퍼즐(기울이기) 화면이 넘어가는지, 보크타이 조도 센서.
- 게임 메뉴 → 버튼 배치: 옮기기, 숫자(px) 입력, 여러 개 선택(길게 누르기), 그룹, 버튼 추가·삭제, 저장 후 다시 켜도 남는지.

## 5. 알려진 함정

- 레무로이드의 게임은 따로 된 프로세스에서 돈다. 설정은 Harmony(여러 프로세스 공유) SharedPreferences 에 있다.
- LibretroDroid 를 원격 버전으로 받으면 후처리 믹서가 없어 1배속 소리가 안 난다 (`deps.kt`).
- 레무로이드 저장소의 `lemuroid-cores/bundled-cores/src/main/jniLibs/*` 는 각 코어 모듈(`lemuroid_core_*`)의 파일을 가리키는
  **git 심볼릭 링크**라서, Windows 에서 `core.symlinks=false` 로 받으면 경로가 적힌 작은 텍스트 파일이 된다.
  그대로 빌드하면 그 코어로 게임이 열리지 않는다 (android-v1 의 GB/GBC). 링크가 가리키는 실제 파일로 바꿔 넣을 것.
- 화면 컨트롤을 PadKit 이 `boundsInRoot` 로 판정하므로, 옮기기·크기는 반드시 `graphicsLayer` 로 해야 보이는 자리와 누르는 자리가 같다.
