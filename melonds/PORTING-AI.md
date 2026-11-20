# melonDS (NDS) 이식 안내서 — ff-bgm1x

기준: melonDS **1.1** 태그. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

## 1. 구조 한눈에

| 새 파일 (`module/src/frontend/qt_sdl/`) | 하는 일 |
|---|---|
| `BgmReplacer.cpp/.h` | 중심. 게임(ARM9)이 사운드 드라이버(ARM7)로 보내는 명령을 IPC FIFO 에서 지켜보고, 시퀀스 시작·정지·볼륨 명령을 따라 한다. 카트리지의 사운드 데이터(SDAT)를 색인해 곡을 분류하고(음악/팡파레/연출/효과음, 스트림(STRM) 곡이 나오는 동안도 연출), 대체할 소리는 원본 쪽 플레이어의 페이더를 무음 값으로 바꿔 끈 다음 자체 재생기로 1배속 연주한다. 연출 중 배속 멈춤 판단, 효과음 볼륨도 여기. `EmuInstance` 마다 하나. |
| `NitroStrm.cpp/.h` | 스트림 곡(STRM) 재생기 (PCM8/16, IMA-ADPCM, 반복 구간). 사용자가 BGM 등으로 정한 STRM 을 롬에서 읽어 1배속으로 낸다 |
| `SoundCatalog.cpp/.h` | 롬의 SDAT 소리 목록(시퀀스, 효과음 묶음 안의 소리, STRM, 뱅크, 파형 묶음), 나온 소리 목록, 사용자 분류·볼륨, 게임별 파일(`ff-bgm1x-sounds/<게임 코드>.txt`)과 내보내기·가져오기. 이름표 `seq:S:N`, `ssar:S:A:N`, `strm:S:N` (S = 롬 안 SDAT 순서). melonDS·DeSmuME 공용 |
| `SoundPreview.cpp/.h` | 소리 분류 창의 재생. 롬에서 시퀀스·뱅크·파형 묶음을 꺼내 따로 만든 메모리에서 NitroSynth 로 연주 (뱅크의 파형 묶음 칸을 채움), STRM 은 NitroStrm. 출력은 SDL(melonDS) / waveOut(DeSmuME). 공용 |
| `SoundCatalogDialog.cpp/.h` | 소리 분류 창 (Qt). 목록, 재생/일시정지·중지, 분류·볼륨(칸 두 번 클릭으로도), 기본값으로, 가져오기·내보내기 |
| `NitroSynth.cpp/.h` | NDS 표준 사운드 드라이버의 시퀀스(SSEQ) 재생기. 게임이 메인 메모리에 올린 시퀀스·뱅크(SBNK)·웨이브(SWAR)를 그대로 읽어 실제 시간 기준으로 연주한다 (16채널 믹서). |

에뮬레이터 내부 의존 (새 버전에서 확인할 것):
`melonDS::NDS` 의 `MainRAM`, `MainRAMMask`, `ARM7Read8/16/32`, `ARM7Write16`, `ConsoleType`, `NDSCartSlot.GetCart()`, `UserData`,
`EmuInstance` 의 `nds`, `audioFreq`, `audioVolume`, `audioMuted`, `getLocalConfig()`, `osdAddMessage`, `CRC32.h`.
DSi 모드(`ConsoleType != 0`)에서는 동작하지 않게 되어 있다.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `src/NDS.h` | `NDS` 클래스 public 멤버 (`UserData` 옆) | 함수 포인터 `SoundFifoHook(NDS&, u32)`, `CartReadHook(NDS&, u32 addr, u32 len)` |
| `src/NDS.cpp` | `NDS::ARM9IOWrite32` 의 IPC FIFO 쓰기(`IPCFIFO9.Write(val)`) 직후 | `SoundFifoHook` 호출 — 사운드 명령 감시. **가장 중요한 지점** |
| `src/NDSCart.cpp` | `CartRetail` / `CartRetailNAND` / `CartHomebrew` 의 `ROMCommandStart`, 명령 `0xB7`(롬 읽기)에서 주소를 구한 직후 (3곳) | `CartReadHook` 호출 — 게임이 롬의 어느 부분을 읽는지. SDAT 안 STRM 데이터를 읽는 동안을 연출로 본다 |
| `src/SPU.h/.cpp` | `SPU` 클래스, `SPU::Mix` 의 채널 출력 직후 | `FfbgmMuteMask`(비트가 켜진 채널은 출력에 섞지 않음)와 `SetFfbgmMuteMask`, `GetChannel`, `GetMasterVolume` — STRM 을 1배속으로 대신 낼 때 |
| `src/SPU.cpp` | `SPU::SetDegrade10Bit`, `Auto` 분기 | 자동이면 항상 16비트 (원본은 롬을 다시 불러올 때만 DS=10비트로 바뀌어 같은 설정에서 소리가 달라졌다). 기능과는 별개의 보정 |
| `qt_sdl/CMakeLists.txt` | `SOURCES_QT_SDL` | `BgmReplacer.cpp`, `NitroSynth.cpp` 추가 |
| `qt_sdl/EmuInstance.h` | `HK_*` 열거형, 멤버, friend | 단축키 2개(`HK_BgmToggle`, `HK_CutsceneFfToggle`), `BgmReplacer* bgmReplacer`, `friend class BgmReplacer; friend class AudioSettingsDialog;` |
| `qt_sdl/EmuInstance.cpp` | 생성자에서 `audioInit()` **전** / 소멸자 / `loadState` 성공 후 | 생성해 설정값(`Bgm1x`, `FfCutscenes`, `MenuSounds1x`, `SfxVolume`) 적용 / 해제 / `onStateLoaded()` |
| `qt_sdl/EmuInstanceAudio.cpp` | `audioCallback` 두 곳 (원본 소리가 없을 때의 무음 분기, 정상 분기 끝) | `mixInto(stream, len, audioVolume, 새로 채운 길이)` — 1배속 소리를 출력에 더한다. 새로 채우지 않은 뒷부분은 지난번에 더한 값을 빼서 원본 그대로 되돌린다 |
| `qt_sdl/EmuThread.cpp` | `nds->RunFrame()` 직후 | `afterMainFrame()` — 게임 프레임 기준 추적 |
| 〃 | 배속 단축키 처리 부분 | 단축키 2개 처리, 그리고 `cutsceneHold`(연출 중 + "연출 중에도 배속" 꺼짐)면 배속을 무시하고 프레임 제한을 건다 |
| `qt_sdl/EmuInstanceInput.cpp`, `InputConfig/InputConfigDialog.h` | 단축키 이름·목록 | 단축키 2개를 General hotkeys 탭에 노출 |
| `qt_sdl/Config.cpp` | 기본값·범위 표 | `Instance*.Bgm1x`(true), `FfCutscenes`(false), `MenuSounds1x`(true), `SfxVolume`(256, 0~256) |
| `qt_sdl/Window.cpp/.h`, `EmuThread.cpp/.h` | Config 메뉴 / 단축키 신호 / `closeEvent` 맨 앞 | 메뉴 "Sound classification... (ff-bgm1x)" 와 단축키 `HK_SoundCatalog`("Open sound classification", 기본 키 없음) -> `onOpenSoundCatalog`. 본 창을 닫을 때 `SoundCatalogDialog::closeFor` |
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
- 연출 곡 동안은 배속이 멈춘다 (기본). 스트림 곡(STRM)이 나오는 동안도 멈춘다.
- 효과음(대화음, 메뉴음, 경험치 소리 등)이 1배속으로 끝까지 난다. 배틀이 끝날 때 짧은 "띡" 같은 잘린 소리가 나지 않는다.
- 울음소리는 게임 속도 그대로 (대상 아님).
- Audio settings 의 Sound effect volume 이 효과음에만 적용된다.
- 메뉴 "Keep BGM at 1x speed" 를 끄면 원본 melonDS 와 똑같다.
- 상태 저장/불러오기 뒤에도 소리가 맞게 이어진다.


## 5. 알려진 함정

- 효과음 묶음(SSAR)에서 트는 소리는 대상이 아니다. 대상은 음악이 아닌 반복 없는 시퀀스(SSEQ)이고, 이름이 `SEQ_PV*`(울음소리)인 것과 길이 0 인 것은 뺀다.
- 게임이 효과음을 멈추라고 하면 바로 끊지 않고, 게임 안에서 울린 시간만큼은 1배속으로 낸 뒤 멈춘다 (배속 중에 바로 끊으면 "띡" 으로 들린다).
- 같은 효과음을 게임 시간 2초(`kRepeatWindow`) 안에 되풀이하면 '되풀이' 로 본다. 앞의 것을 1배속으로 낸 뒤 실제로 흐른 시간이
  게임 안에서 되풀이한 간격의 0.9배보다 짧으면 이번 것은 내지 않는다 (원본은 이미 무음). 낮은 체력 경고음이 배속에 걸려 빨라지던 문제.
- 상태를 불러오면(`stateLoaded`) 같은 플레이어에서 같은 반복 곡(시퀀스 주소)이 계속 나오는 경우 자체 재생기를 끊지 않는다
  (`reloadKeepingMusic`, GBA 판과 같은 동작). 그 밖의 곡은 `recoverFromDriver` 로 드라이버 위치부터 이어 받는다.
- 스트림 곡(STRM)은 사운드 명령에 곡 번호가 없다 (채널 잡기 26, PCM 채널 설정 14, 타이머 시작 12/정지 13 만 온다).
  그래서 게임이 카트에서 읽는 롬 위치로 안다. `indexCart` 가 SDAT 의 STRM 마다 데이터 시작(헤더 0x28)부터 파일 끝까지의 롬 범위를 모으고,
  그 범위를 읽으면 마지막으로 읽은 뒤 30프레임까지를 연출로 본다. 머리(헤더)만 읽는 것은 재생이 아니다 (게임이 시작할 때 STRM 머리를 훑는다).
  STRM 은 대체하지 않는다 (녹음된 소리라 바꿔 낼 것이 없다).
- 명령 번호가 다른 게임이 있다: 3번 이후가 모두 3씩 앞당겨져 있어 플레이어 값 설정이 6번이 아니라 3번으로 온다.
  `onSoundFifo` 가 "3번인데 (플레이어, 4·5·6, 값, 1·2바이트) 모양" 이면 옛 방식(`oldCmdLayout`)으로 보고 번호를 바꿔 읽는다.
  이걸 못 하면 원본 BGM 의 페이더를 끄지 못해 1배속 BGM 과 겹쳐 들린다. 옛 방식의 2번은 무시한다.
- 소리 분류 창: 창이 앞이면 일시정지(일시정지는 창이 건 것만 푼다), 창이 닫혀 있으면 수집하지 않는다(`SoundCatalog::collecting`).
  기능(BGM 1배속)이 꺼져 있어도 창이 열려 있으면 소리 명령·카트 읽기를 보고 목록에만 넣는다. 창을 연 순간 이미 나오던 소리는
  드라이버의 연주 중 비트와 마지막 STRM 으로 넣는다(`noteNowPlaying`). 사용자 분류는 자동 판단보다 먼저 적용된다(`classify`).
- 소리 분류 창의 볼륨: 1배속 쪽은 자체 재생기 출력 배율(`userGain`). 원본 그대로 나오는 시퀀스·효과음 묶음 소리는
  `Cmd_PlayerParam` 을 "우선순위·음량·페이더" 4바이트 쓰기로 바꿔 넣으며 페이더에 `200*log10(볼륨)`(0.1dB 단위)을 더한다
  (곡을 준비할 때 우선순위 설정이 따라오므로 페이더 명령이 없는 게임도 적용). 드라이버가 최대 음량을 넘지 않게 자르므로 100% 초과는 덜 커질 수 있다.
  원본 그대로 나오는 STRM 은 그 채널 출력에 곱한다(코어 `SPU::SetFfbgmGain(mask, q8)`).
  창에서 볼륨을 바꾸면(목록 판 번호가 바뀌면) `applyUserVolumes` 가 지금 플레이어들에 바로 적용한다: 1배속은 `userGain`, 원본은 드라이버 플레이어 페이더를 직접 쓴다.
- 소리 분류 창의 칸 너비: 모두 `QHeaderView::Interactive`, 마지막 칸만 늘림 (한 칸만 Stretch 면 다른 칸을 넓힐 때 그 칸이 대신 줄어 거꾸로 움직이는 것처럼 보인다).
- 효과음 묶음(SSAR)은 모든 소리가 같은 데이터 주소(arg1)를 쓰고 시작 위치(arg2)만 다르다: 같은 소리 판단(`seqBase`)은 arg1+arg2.
  묶음 소리는 길이 0 인 음 하나로 된 것이 많아서, 사용자가 1배속으로 정한 묶음 소리만 트랙이 끝나도 파형 끝까지 울린다(`setHoldOneShots`).
- STRM 을 1배속으로 낼 때 원본은 게임이 잡은 스트림 채널(명령 26)을 SPU 출력에서 뺀다. 음량·팬은 그 채널의 레지스터 값을 따른다.
  곡이 바뀌면 앞 곡이 짧게 줄어드는 동안 그 데이터를 읽으므로 파일 버퍼를 둘 번갈아 쓴다.
- 본 창을 닫을 때 소리 분류 창을 먼저 닫는다 (늦게 닫히면 사라진 에뮬레이터를 건드려 충돌).
- 게임 시간 환산은 59.8261 fps.
- 줄바꿈: 이 자료는 LF.
