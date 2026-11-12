# melonDS Android (NDS) 이식 안내서 — ff-bgm1x

| 폴더 | 원본 저장소 | 기준 커밋 |
|---|---|---|
| `melonds-android/` | rafaelvcaetano/melonDS-android | `c42995ca` |
| `melonds-android-lib/` | rafaelvcaetano/melonDS-android-lib (위 저장소의 하위 저장소) | `431ab4b` |

`melonDS-android-lib` 는 melonDS 1.1 과 같은 바탕이라, PC 판(`melonds/`)의 재생기를 그대로 옮겼습니다.
기능 설명은 맨 위 `FEATURES.md` 의 "모바일판" 을 보세요.

## 1. 구조 한눈에

- `module/app/src/main/cpp/bgm1x/` — PC 판 `BgmReplacer`·`NitroSynth` 와 같은 코드 (SSEQ 시퀀서로 BGM·효과음을 1배속 재생).
- `MelonInstance` 가 `BgmReplacerHost` 를 갖고, `OboeCallback` 이 출력 버퍼에 1배속 소리를 더한다.
- 배속 루프(`MelonDSAndroidJNI.cpp`)는 연출 곡이면 1배속으로 돈다 (`MelonDS.cpp` 의 `isBgmSpeedHold`).
- 설정: 오디오 설정 화면(`pref_audio.xml`) → `SharedPreferencesSettingsRepository` → `EmulatorConfiguration` → `Configuration.h`.
- 배치 편집기: 원래 편집기(`LayoutEditorView`)에 X·Y 숫자, 정렬, 꽉 채우기, 여러 개 선택, 그룹, 서로 맞추기, 등간격을 더했다.
  새 떠 있는 패널은 `module/.../layouteditor/ui/FfLayoutPanel.kt` (Compose), 원래 아래 패널과 골라 쓴다.
- 기본 배치 4개에 일시정지(메뉴) 버튼 추가 (`DefaultLayoutProvider.kt`).

## 2. 원본에 끼우는 지점

| 파일 | 역할 |
|---|---|
| `melonds-android-lib/src/NDS.cpp/.h` | ARM9 이 IPC FIFO 에 쓰는 사운드 명령을 재생기가 지켜보게 하는 연결 (`SoundFifoHook`) |
| `melonds-android-lib/src/SPU.cpp` | 소리 해상도 "자동" 을 롬을 다시 불러와도 16비트로 고정 (같은 설정에서 소리가 달라지지 않게) |
| `app/CMakeLists.txt` | `bgm1x/` 소스 추가 |
| `MelonInstance.cpp/.h`, `MelonDS.cpp/.h` | 재생기 만들기·롬 불러올 때 알림·`isBgmSpeedHold` |
| `OboeCallback.cpp` | 출력 버퍼에 1배속 소리 더하기 |
| `MelonDSAndroidJNI.cpp` | 배속 루프에서 연출 곡이면 1배속 |
| `Configuration.h`, `MelonDSAndroidConfiguration.cpp`, `EmulatorConfiguration.kt`, `SharedPreferencesSettingsRepository.kt`, `pref_audio.xml`, `strings.xml` | 설정 항목 4개 (BGM 1배속, 연출 중에도 배속, 효과음 1배속, 효과음 볼륨) |
| `layouteditor/*`, `view_layout_editor_manager.xml` | 배치 편집기 기능, 새 패널 연결 |
| `DefaultLayoutProvider.kt` | 기본 배치에 일시정지 버튼 |

## 3. 진행 순서

1. `melonds-android-lib/` 에 module(없음)·`hooks.patch` 적용.
2. `melonds-android/` 에 module 복사, `hooks.patch` 적용.
3. `./gradlew :app:assembleGitHubProdRelease` (JDK 21). **디버그 빌드는 최적화가 꺼져 매우 느리다** — 속도 확인은 릴리스로.
   릴리스 서명은 각자 키로 (`local.properties` 등, 이 자료에는 키가 없다).

## 4. 확인 방법

- 배속 중 BGM·효과음이 1배속인지 (포켓몬 하트골드 등). 연출 곡에서 배속이 잠시 멈추는지.
- 설정 → 입력 → 레이아웃 → 새 레이아웃: 새 패널, 여러 개 선택·그룹·서로 맞추기, 원래 패널로 바꾸기.

## 5. 알려진 함정

- 그룹은 레이아웃마다가 아니라 앱 전체에 한 벌로 저장된다 (`ff_layout_editor_groups`).
- melonDS 의 버튼 종류는 앱 안에 정해져 있어(`LayoutComponent`), 같은 버튼을 두 개 둘 수 없다.
