# melonDS (NDS) 이식 안내서 — ff-bgm1x

기준: melonDS **1.1** 태그. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

## 1. 구조 한눈에

| 새 파일 (`module/src/frontend/qt_sdl/`) | 하는 일 |
|---|---|
| `BgmReplacer.cpp/.h` | 중심. 게임(ARM9)이 사운드 드라이버(ARM7)로 보내는 명령을 IPC FIFO 에서 지켜보고, 시퀀스 시작·정지·볼륨 명령을 따라 한다. 카트리지의 사운드 데이터(SDAT)를 색인해 곡을 분류하고(음악/팡파레/연출/효과음), 대체할 소리는 원본 쪽 플레이어의 페이더를 무음 값으로 바꿔 끈 다음 자체 재생기로 1배속 연주한다. 연출 중 배속 멈춤 판단, 효과음 볼륨도 여기. `EmuInstance` 마다 하나. |
| `NitroSynth.cpp/.h` | NDS 표준 사운드 드라이버의 시퀀스(SSEQ) 재생기. 게임이 메인 메모리에 올린 시퀀스·뱅크(SBNK)·웨이브(SWAR)를 그대로 읽어 실제 시간 기준으로 연주한다 (16채널 믹서). |

에뮬레이터 내부 의존 (새 버전에서 확인할 것):
`melonDS::NDS` 의 `MainRAM`, `MainRAMMask`, `ARM7Read8/16/32`, `ARM7Write16`, `ConsoleType`, `NDSCartSlot.GetCart()`, `UserData`,
`EmuInstance` 의 `nds`, `audioFreq`, `audioVolume`, `audioMuted`, `getLocalConfig()`, `osdAddMessage`, `CRC32.h`.
DSi 모드(`ConsoleType != 0`)에서는 동작하지 않게 되어 있다.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `src/NDS.h` | `NDS` 클래스 public 멤버 (`UserData` 옆) | 함수 포인터 `SoundFifoHook(NDS&, u32)` |
| `src/NDS.cpp` | `NDS::ARM9IOWrite32` 의 IPC FIFO 쓰기(`IPCFIFO9.Write(val)`) 직후 | `SoundFifoHook` 호출 — 사운드 명령 감시. **가장 중요한 지점** |
| `src/SPU.cpp` | `SPU::SetDegrade10Bit`, `Auto` 분기 | 자동이면 항상 16비트 (원본은 롬을 다시 불러올 때만 DS=10비트로 바뀌어 같은 설정에서 소리가 달라졌다). 기능과는 별개의 보정 |
| `qt_sdl/CMakeLists.txt` | `SOURCES_QT_SDL` | `BgmReplacer.cpp`, `NitroSynth.cpp` 추가 |
| `qt_sdl/EmuInstance.h` | `HK_*` 열거형, 멤버, friend | 단축키 2개(`HK_BgmToggle`, `HK_CutsceneFfToggle`), `BgmReplacer* bgmReplacer`, `friend class BgmReplacer; friend class AudioSettingsDialog;` |
| `qt_sdl/EmuInstance.cpp` | 생성자에서 `audioInit()` **전** / 소멸자 / `loadState` 성공 후 | 생성해 설정값(`Bgm1x`, `FfCutscenes`, `MenuSounds1x`, `SfxVolume`) 적용 / 해제 / `onStateLoaded()` |
| `qt_sdl/EmuInstanceAudio.cpp` | `audioCallback` 두 곳 (원본 소리가 없을 때의 무음 분기, 정상 분기 끝) | `mixInto(stream, len, audioVolume, 새로 채운 길이)` — 1배속 소리를 출력에 더한다. 새로 채우지 않은 뒷부분은 지난번에 더한 값을 빼서 원본 그대로 되돌린다 |
| `qt_sdl/EmuThread.cpp` | `nds->RunFrame()` 직후 | `afterMainFrame()` — 게임 프레임 기준 추적 |
| 〃 | 배속 단축키 처리 부분 | 단축키 2개 처리, 그리고 `cutsceneHold`(연출 중 + "연출 중에도 배속" 꺼짐)면 배속을 무시하고 프레임 제한을 건다 |
| `qt_sdl/EmuInstanceInput.cpp`, `InputConfig/InputConfigDialog.h` | 단축키 이름·목록 | 단축키 2개를 General hotkeys 탭에 노출 |
| `qt_sdl/Config.cpp` | 기본값·범위 표 | `Instance*.Bgm1x`(true), `FfCutscenes`(false), `MenuSounds1x`(true), `SfxVolume`(256, 0~256) |
| `qt_sdl/Window.cpp/.h` | Config 메뉴, "Audio sync" 다음 | 체크 항목 3개 (Keep BGM at 1x speed / Fast forward during cutscenes / Keep sound effects at 1x speed). 메뉴를 열 때 현재 값 반영 |
| `qt_sdl/AudioSettingsDialog.ui/.cpp/.h` | Volume 줄 **바로 아래** | "Sound effect volume" 슬라이더 (0~256, 움직이면 바로 적용, Cancel 이면 원래 값) |
| `qt_sdl/main.cpp` | `MelonApplication` 생성 | (Windows, 기능과 별개) 한글 등 비ASCII 롬 경로를 명령줄로 넘길 때 깨지는 문제 보정. 새 버전이 고쳤으면 빼도 된다 |

## 3. 진행 순서

1. 공식 저장소(`https://github.com/melonDS-emu/melonDS`)에서 최신 릴리스 태그를 받는다.
2. `module/` 를 소스 루트에 덮어 복사한다.
3. `git apply -3 <kit>/melonds/hooks.patch`. 충돌은 위 표의 '역할' 기준으로 옮긴다.
4. 1절의 의존 멤버가 그대로인지 확인 (예: IPC FIFO 쓰기 위치, `EmuInstance` 오디오 멤버 이름).
5. 빌드. 원래는 Windows MSYS2 UCRT64 + vcpkg, CMake 프리셋 `release-mingw-x86_64`.
6. 아래 확인 방법.

## 4. 확인 방법 (게임 필요, 사람이 듣고 판단)

배속을 켜고:
- 필드 BGM 이 1배속, 음정이 원래와 같다.
- 팡파레는 1배속으로 끝까지, 그 뒤 곡이 자연스럽게 이어진다.
- 연출 곡 동안은 배속이 멈춘다 (기본).
- 효과음(대화음, 메뉴음, 경험치 소리 등)이 1배속으로 끝까지 난다. 배틀이 끝날 때 짧은 "띡" 같은 잘린 소리가 나지 않는다.
- 울음소리는 게임 속도 그대로 (대상 아님).
- Audio settings 의 Sound effect volume 이 효과음에만 적용된다.
- 메뉴 "Keep BGM at 1x speed" 를 끄면 원본 melonDS 와 똑같다.
- 상태 저장/불러오기 뒤에도 소리가 맞게 이어진다.

원래 확인한 게임: 포켓몬스터 플래티넘, 하트골드.

## 5. 알려진 함정

- 효과음 묶음(SSAR)에서 트는 소리는 대상이 아니다. 대상은 음악이 아닌 반복 없는 시퀀스(SSEQ)이고, 이름이 `SEQ_PV*`(울음소리)인 것과 길이 0 인 것은 뺀다.
- 게임이 효과음을 멈추라고 하면 바로 끊지 않고, 게임 안에서 울린 시간만큼은 1배속으로 낸 뒤 멈춘다 (배속 중에 바로 끊으면 "띡" 으로 들린다).
- 같은 효과음을 게임 시간 2초(`kRepeatWindow`) 안에 되풀이하면 '되풀이' 로 본다. 앞의 것을 1배속으로 낸 뒤 실제로 흐른 시간이
  게임 안에서 되풀이한 간격의 0.9배보다 짧으면 이번 것은 내지 않는다 (원본은 이미 무음). 낮은 체력 경고음이 배속에 걸려 빨라지던 문제.
- 상태를 불러오면(`stateLoaded`) 같은 플레이어에서 같은 반복 곡(시퀀스 주소)이 계속 나오는 경우 자체 재생기를 끊지 않는다
  (`reloadKeepingMusic`, GBA 판과 같은 동작). 그 밖의 곡은 `recoverFromDriver` 로 드라이버 위치부터 이어 받는다.
- 게임 시간 환산은 59.8261 fps.
- 줄바꿈: 이 자료는 LF.
