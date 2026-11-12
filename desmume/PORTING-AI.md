# DeSmuME (NDS, Windows) 이식 안내서 — ff-bgm1x

기준: DeSmuME 공식 저장소(https://github.com/TASEmulators/desmume) **master `3c4b95a`** (2026-01, 창 제목 `0.9.14 git#3c4b95a`).
마지막 정식 릴리스는 0.9.13(2022-05)이고, 그 뒤로는 Git 빌드만 나온다. 기능 설명과 메뉴는 `FEATURES-ko.txt` 를 보세요.

재생기 부분은 melonDS 판(`../melonds/`)과 같은 코드다. melonDS 판의 `BgmReplacer`/`NitroSynth` 를 DeSmuME 의 메모리·ROM 접근으로 연결한 것이다.

## 1. 구조 한눈에

| 새 파일 (`module/desmume/src/ffbgm/`) | 하는 일 |
|---|---|
| `BgmReplacer.cpp/.h` | 중심. 게임(ARM9)이 사운드 드라이버(ARM7)로 보내는 명령을 IPC FIFO 에서 지켜보고, 시퀀스 시작·정지·볼륨 명령을 따라 한다. 카트리지의 사운드 데이터(SDAT)를 색인해 곡을 분류하고(음악/팡파레/연출/효과음), 대체할 소리는 원본 쪽 플레이어의 페이더를 무음 값으로 바꿔 끈 다음 자체 재생기로 1배속 연주한다. 연출 중 배속 멈춤 판단, 효과음 볼륨도 여기. 전역 객체 하나. 맨 아래에 `ffbgm.h` 함수들의 구현이 있다. |
| `NitroSynth.cpp/.h` | NDS 표준 사운드 드라이버의 시퀀스(SSEQ) 재생기. 게임이 메인 메모리에 올린 시퀀스·뱅크(SBNK)·웨이브(SWAR)를 그대로 읽어 실제 시간 기준으로 연주한다 (16채널 믹서). melonDS 판과 같은 파일. |
| `ffbgm.h` | DeSmuME 쪽 호출부가 쓰는 함수 목록 (`ffbgm_onArm9Fifo`, `ffbgm_mix`, `ffbgm_afterMainFrame`, 설정 함수 등). |
| `ffbgm_glue.h` | melonDS 타입 이름(`melonDS::u8` 등)을 DeSmuME 타입에 잇고, 메모리 접근을 감싼다. |

에뮬레이터 내부 의존 (새 버전에서 확인할 것):
`MMU.MAIN_MEM`, `_MMU_MAIN_MEM_MASK`, `_MMU_read08/16/32<ARMCPU_ARM7, MMU_AT_DEBUG>`, `_MMU_write16<ARMCPU_ARM7, MMU_AT_DEBUG>`,
`gameInfo.reader`/`fROM`/`romsize`/`headerOffset` (롬 전체를 한 번 읽어 SDAT 색인), `DESMUME_SAMPLE_RATE`, zlib 의 `crc32`.
코드는 C++11 로 맞췄다 (MinGW Makefile 이 `-std=gnu++11`): `std::clamp` 대신 `ffclamp`, `std::lock_guard<std::mutex>`.

## 2. 원본에 끼우는 지점 (`hooks/`)

| 원본 파일 | 끼우는 곳 | 역할 |
|---|---|---|
| `desmume/src/MMU.cpp` | ARM9 32비트 IO 쓰기의 `case REG_IPCFIFOSEND: IPC_FIFOsend(ARMCPU_ARM9, val);` 직후 | `ffbgm_onArm9Fifo(val)` — 사운드 명령 감시. **가장 중요한 지점** |
| `desmume/src/SPU.cpp` | `SPU_Emulate_user` 에서 `soundProcessor->UpdateAudio(postProcessBuffer, ...)` 직전 | `ffbgm_mix(postProcessBuffer, 샘플 수)` — 1배속 소리를 출력에 더한다 |
| `desmume/src/NDSSystem.cpp` | `NDS_LoadROM` 성공 경로에서 `NDS_Reset()` 을 부르기 직전 / `NDS_Reset()` 안 (롬이 있을 때) | `ffbgm_onRomLoaded()` (게임 표시, 롬 색인 다시) / `ffbgm_onReset()` (추적 초기화) |
| `desmume/src/saves.cpp` | `savestate_load(EMUFILE&)` 이 성공해 `return true` 하기 직전 | `ffbgm_onStateLoaded()` — 상태를 불러오면 연주 중이던 곡을 드라이버에서 다시 읽어 이어 받는다 |
| `frontend/windows/main.cpp` | 메인 루프의 `NDS_exec<false>()` 직후 | `ffbgm_afterMainFrame()` — 게임 프레임 기준 추적, 자체 재생기 출력 채우기 |
| 〃 | `StepRunLoop_Throttle` | 연출 중 배속 멈춤(`ffbgm_cutsceneHold()`), 배속 배율에 맞춘 화면 생략 수와 `SpeedThrottle()` 호출 |
| 〃 | 설정 읽기(`FrameLimit` 을 읽는 곳 다음) / 메뉴 체크 표시(`IDC_FRAMELIMIT` 옆) / `WM_COMMAND` | `desmume.ini` 의 `[ff-bgm1x]` (`Bgm1x`, `Sfx1x`, `FfCutscenes`, `SfxVolume`, `FastForwardSpeed`), 메뉴 항목 처리 |
| 〃 | `SoundSettingsDlgProc` | "SFX Volume (1x)" 슬라이더 (움직이면 바로 적용, Cancel 이면 원래 값, OK 면 저장) |
| `frontend/windows/throttle.cpp/.h` | `SpeedThrottle()` 첫머리, `FastForward` 선언 옆 | 원본은 배속이면 바로 빠져나간다. 연출 멈춤 중에는 평소 목표 속도로 기다리고, 배속 배율(`FastForwardSpeed`, 0 = 무제한)이 있으면 원래 속도(100%) × 배율로 기다린다. 원본처럼 배속 중에는 + / - 속도 설정을 쓰지 않는다 |
| `frontend/windows/hotkey.cpp/.h` | `FrameLimitToggle` 옆 | 단축키 2개 (`FfbgmBgm1xToggle`, `FfbgmCutsceneToggle`, 기본 키 없음) |
| `frontend/windows/resource.h`, `resources.rc` | Config 메뉴 "Emulation Settings" 위, 사운드 설정 대화상자, 문자열 표 | 메뉴 `Fast forward speed` (2x/3x/4x/6x/8x/Unlimited), `ff-bgm1x (1x audio)` (항목 3개), 슬라이더, 단축키 이름 |
| `frontend/windows/DeSmuME.vcxproj`, `Makefile` | 소스 목록 (`SPU.cpp` 옆) | `ffbgm/` 파일 추가 |

## 3. 진행 순서

1. 공식 저장소에서 최신 커밋(또는 태그)을 받는다.
2. `module/` 을 소스 루트에 덮어 복사한다 (`desmume/src/ffbgm/` 가 생긴다).
3. `git apply -3 <kit>/desmume/hooks.patch`. 충돌은 위 표의 '역할' 기준으로 옮긴다.
4. 1절의 의존 이름이 그대로인지 확인한다 (특히 IPC FIFO 쓰기 위치, `SPU_Emulate_user` 의 출력 버퍼, `gameInfo` 필드).
5. 빌드: **MSVC** (Visual Studio 2022 Build Tools, C++ 데스크톱 워크로드).
   `MSBuild desmume/src/frontend/windows/DeSmuME.vcxproj /p:Configuration=Release /p:Platform=x64`.
   프로젝트의 7z 풀기 단계가 `un7z_and_touch.bat` 를 부르므로 그 폴더(`frontend/windows`)를 PATH 에 넣고 실행한다.
   결과: `frontend/windows/__bins/DeSmuME-VS2022-x64-Release.exe`.
6. 아래 확인 방법.

## 4. 확인 방법 (게임 필요, 사람이 듣고 판단)

배속(Tab, 또는 Fast Forward Toggle)을 켜고:
- 필드 BGM 이 1배속, 음정이 원래와 같다.
- 팡파레는 1배속으로 끝까지, 그 뒤 곡이 자연스럽게 이어진다.
- 연출 곡 동안은 배속이 멈춘다 (기본). 하트골드는 시작 영상·타이틀 곡이 연출 곡이라 타이틀 화면에서도 1배속이다.
- 효과음이 1배속으로 끝까지 난다. 울음소리는 게임 속도 그대로 (대상 아님).
- Sound Settings 의 SFX Volume (1x) 가 1배속 효과음에만 적용된다.
- 메뉴 "Keep BGM at 1x speed" 를 끄면 원본 BGM 이 돌아온다.
- Config → Fast forward speed 의 배율대로 돈다 (View → Display FPS 로 확인: 3x 면 약 180fps).
- 상태 저장/불러오기 뒤에도 소리가 맞게 이어진다.

확인한 범위 (2026-11-13, 하트골드): 명령 감시·곡 분류(로그), 시작 영상 연출 멈춤, 메뉴·설정 저장, 빌드.
BGM·효과음이 귀로 듣기에 맞는지와 상태 불러오기는 아직 충분히 확인하지 않았다.

## 5. 알려진 함정

- **MinGW 로 빌드하지 말 것.** MSYS2 MinGW(`frontend/windows/Makefile`) 로도 빌드는 되지만, 그 빌드는 수정하지 않은 원본도
  Control Config·Hotkey Config 대화상자를 열면 `USER32.dll` 접근 위반으로 죽었다. 공식 배포본(MSVC)은 멀쩡했다. 원인은 찾지 않았다.
- **`SpeedThrottle()` 은 배속이면 첫 줄에서 바로 빠져나간다.** 연출 멈춤과 배속 배율은 반드시 이 함수 안에서 처리해야 한다
  (`StepRunLoop_Throttle` 만 고치면 멈추지 않는다).
- **`NDS_LoadROM` 은 안에서 `NDS_Reset` 을 부른다.** `ffbgm_onReset()` 이 "게임 없음" 으로 바꾸면 기능 전체가 꺼진다 (실제로 겪음).
- **`savestate_load` 도 안에서 먼저 `NDS_Reset` 을 부른다.** 그래서 `onReset` 은 표시만 하고 정리는 다음 프레임에 한다.
  여기서 바로 정리하거나 프레임 번호 조건(60프레임)에 걸리면, 불러온 뒤 곡을 이어 받지 못해 BGM 이 안 나오거나 원본이 게임 속도로 난다 (실제로 겪음).
- 같은 효과음을 게임 시간 2초(`kRepeatWindow`) 안에 되풀이하면 '되풀이' 로 본다. 앞의 것을 1배속으로 낸 뒤 실제로 흐른 시간이
  게임 안에서 되풀이한 간격의 0.9배보다 짧으면 이번 것은 내지 않는다 (원본은 이미 무음). 낮은 체력 경고음이 배속에 걸려 빨라지던 문제.
- 상태를 불러오면(`stateLoaded`) 같은 플레이어에서 같은 반복 곡(시퀀스 주소)이 계속 나오는 경우 자체 재생기를 끊지 않는다
  (`reloadKeepingMusic`, GBA 판과 같은 동작). 그 밖의 곡은 `recoverFromDriver` 로 드라이버 위치부터 이어 받는다.
- 효과음 묶음(SSAR)에서 트는 소리는 대상이 아니다. 대상은 음악이 아닌 반복 없는 시퀀스(SSEQ)이고, 이름이 `SEQ_PV*`(울음소리)인 것과 길이 0 인 것은 뺀다.
- 게임이 효과음을 멈추라고 하면 바로 끊지 않고, 게임 안에서 울린 시간만큼은 1배속으로 낸 뒤 멈춘다.
- 하트골드에서는 시퀀스 명령의 플레이어 번호가 곡마다 0→15 로 돌아가며 바뀌었다 (로그로 확인). 드라이버 상태 비트도 같은 번호를 쓴다.
- Tab(누르는 동안 배속)과 Fast Forward Toggle 은 같은 변수를 쓴다. 토글 키에 Tab 이 들어가면(예: Shift+Tab) 키를 뗄 때 배속이 바로 꺼진다.
- 게임 시간 환산은 59.8261 fps. 출력 샘플레이트는 `DESMUME_SAMPLE_RATE` (44100).
- 줄바꿈: 이 자료는 LF. 새 파일은 MSVC 의 cp949 경고를 피하려고 UTF-8 BOM 이 붙어 있다.
