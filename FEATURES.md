# ff-bgm1x 기능 정리

*English below.*

## 목표

배속(빨리 감기)을 켜도 소리는 배속을 켜지 않았을 때와 같게 듣는 것.
게임은 빨라지고, 음악과 효과음은 1배속·원래 음높이로 납니다.

## 공통 기능

| 기능 | 설명 | mGBA | melonDS | Azahar |
|---|---|---|---|---|
| BGM 1배속 유지 | 게임의 음악은 소리만 끄고, 같은 곡을 실제 시간 기준으로 따로 재생 | ✓ (GBA, GB/GBC) | ✓ | ✓ |
| 팡파레 끝까지 | 반복 없는 짧은 곡(아이템 획득 등)은 1배속으로 끝까지, 그 뒤 곡은 이어서 | ✓ (GBA) | ✓ | ✓ |
| 연출 중 배속 멈춤 | 반복 없는 긴 곡(연출 곡)이 나오는 동안은 배속을 걸어도 1배속 진행. 메뉴 "연출 중에도 배속" 으로 끌 수 있음 | ✓ (GBA) | ✓ | ✓ (동영상 재생 중도) |
| 효과음 1배속 | 효과음도 1배속으로 끝까지 | ✓ (GBA, GB/GBC) | ✓ | ✓ (울음소리 포함) |
| 효과음 볼륨 | 오디오 설정의 볼륨 아래 슬라이더 한 줄 | ✓ (무음 체크 포함, GBA 만) | ✓ | ✓ |
| 메뉴 켜고 끄기 | 기능마다 메뉴 항목, 설정 저장 | ✓ | ✓ | ✓ |
| 단축키 | 메뉴 항목 켜고 끄기 (기본 키 없음) | — | ✓ | ✓ |

## 에뮬레이터별 방식

**mGBA 0.10.5**
- GBA (MP2K 엔진 게임): 매 프레임 게임 메모리의 MP2K 플레이어 상태를 읽어 음악 트랙을 끄고, 같은 곡을 내장 MP2K 재생기로 낸다. 곡 표는 게임별 주소가 아니라 롬 패턴으로 찾는다.
- GB/GBC: 숨겨진 두 번째 GB 코어에서 게임 자신의 사운드 드라이버만 실시간으로 돌린다 (지원 엔진: 포켓몬 금·은·크리스탈).
- 배속 중에 "BGM 1배속 유지" 를 끄면 원본 BGM 이 바로 나온다. 영상 녹화에도 1배속 BGM 이 들어간다.

**melonDS 1.1**
- 게임이 사운드 CPU 에 보내는 명령(IPC FIFO)을 지켜보다가, 닌텐도 표준 사운드 데이터(SDAT)의 같은 시퀀스를 내장 재생기로 연주한다. 원본은 페이더로 끈다.
- 게임이 도중에 멈추는 효과음은 게임 안에서 울린 시간만큼 1배속으로 낸 뒤 멈춘다. 상태 저장을 불러오면 연주 중이던 곡을 같은 위치부터 이어 받는다.

**Azahar 2126.2**
- BGM: 게임 데이터의 스트림 파일(BCSTM, AAC)을 색인해 두고, DSP 음성이 받는 버퍼로 어느 곡인지 알아내 직접 해독해 1배속으로 낸다.
- 효과음: 배속 중에 DSP 음성이 새 소리를 시작하면 그 음성의 실제 출력(볼륨 포함)을 녹음해 1배속으로 다시 낸다. 배속이 꺼져 있으면 원본 그대로.

## 기능과 별개로 고친 것

| 에뮬레이터 | 수정 |
|---|---|
| melonDS | 오디오 비트 깊이 "자동" 을 항상 16비트로 (롬을 다시 불러오면 DS 에서 10비트로 바뀌어 같은 설정에서 소리가 달라지던 문제) |
| melonDS | 한글 등 비ASCII 롬 경로를 명령줄로 넘길 때 깨지는 문제 (Windows) |
| Azahar | NoCrypto 표시가 꺼져 있지만 실제로는 평문인 롬 덤프도 열리게 |
| mGBA | 공식 배포본처럼 실행 파일 하나로 빌드 (정적 빌드용 초기화), SDL 출력 쪽 섞기 지원 |

## 새 버전에 대한 생각

기능의 동작 규칙(무엇을 1배속으로 하고 무엇을 게임 속도로 둘지, 팡파레·연출·효과음을 어떻게 다룰지)은 만든 사람이 직접 정했고,
그 규칙과 원본 코드에 끼우는 지점이 각 폴더의 `PORTING-AI.md` 와 `FEATURES-ko.txt` 에 그대로 적혀 있습니다.

만든 사람은 에뮬레이터의 새 버전이 나와도 사운드 부분(DSP·사운드 채널 처리, 오디오 출력)은 크게 바뀌지 않을 것으로 예상합니다.
그렇다면 AI 코딩 도구에 이 자료를 주는 것만으로 대부분 최신 버전에 다시 적용할 수 있을 것입니다.
다만 이것은 예상이며, 사운드 부분이 크게 바뀐 버전에서는 연결부를 다시 짜야 하고 소리가 맞는지는 게임으로 확인해야 합니다.

---

# ff-bgm1x features (English)

## Goal

Fast-forward the game, but keep the sound exactly as it is without fast-forward:
music and sound effects play at 1x speed and original pitch.

## Common features

| Feature | Description | mGBA | melonDS | Azahar |
|---|---|---|---|---|
| Keep BGM at 1x | The game's music is muted and the same song is played separately in real time | ✓ (GBA, GB/GBC) | ✓ | ✓ |
| Fanfares to the end | Short non-looping songs (item jingles etc.) play to the end at 1x, then the music resumes | ✓ (GBA) | ✓ | ✓ |
| Hold fast-forward in cutscenes | While a long non-looping (cutscene) song plays, the game runs at 1x even with fast-forward on; can be turned off | ✓ (GBA) | ✓ | ✓ (also during videos) |
| Sound effects at 1x | Sound effects also play at 1x to the end | ✓ (GBA, GB/GBC) | ✓ | ✓ (cries included) |
| Sound effect volume | One extra slider below the volume slider in audio settings | ✓ (with mute, GBA only) | ✓ | ✓ |
| Menu toggles | A menu item per feature; settings are saved | ✓ | ✓ | ✓ |
| Hotkeys | Toggle the menu items (no default keys) | — | ✓ | ✓ |

## How each one works

**mGBA 0.10.5**
- GBA (MP2K engine games): every frame, reads the game's MP2K player state from RAM, mutes the music tracks, and plays the same song with a built-in MP2K player. Song tables are found by ROM pattern, not per-game addresses.
- GB/GBC: a hidden second GB core runs only the game's own sound driver in real time (supported engine: Pokémon Gold/Silver/Crystal).
- Turning "Keep BGM at 1x" off during fast-forward brings the original BGM back immediately. Video recording also gets the 1x BGM.

**melonDS 1.1**
- Watches the sound commands the game sends to the sound CPU (IPC FIFO) and plays the same sequences from the cartridge's standard sound data (SDAT) with a built-in player; the originals are muted via the fader.
- Sound effects the game stops early play at 1x for as long as they sounded in game time. After loading a save state, the current song continues from the same position.

**Azahar 2126.2**
- BGM: indexes the stream files (BCSTM, AAC) in the game data, recognizes which song a DSP voice is playing from its buffers, and decodes it itself at 1x.
- Sound effects: while fast-forwarding, when a DSP voice starts a new sound, its actual output (with volume) is recorded and replayed at 1x. Without fast-forward, the original is used.

## Other fixes

| Emulator | Fix |
|---|---|
| melonDS | Audio bit depth "Automatic" is always 16-bit (reloading a ROM switched DS mode to 10-bit, so the same setting sounded different) |
| melonDS | Non-ASCII ROM paths passed on the command line were garbled (Windows) |
| Azahar | Opens ROM dumps whose NoCrypto flag is off but whose content is actually plaintext |
| mGBA | Single-executable build like the official release (static build init), mixing support for SDL output |

## About new versions

The behavior rules (what plays at 1x and what stays at game speed, how fanfares, cutscenes and sound effects are handled)
were decided by the author, and they are written down — together with where the code hooks into the emulator —
in each folder's `PORTING-AI.md` and `FEATURES-ko.txt`.

The author expects the sound parts of these emulators (DSP / sound channel handling, audio output) not to change much
in new releases. If so, giving this kit to an AI coding assistant should be enough to re-apply the mod to most new versions.
This is an expectation, not a guarantee: if the sound code changes a lot, the hooks have to be reworked,
and whether the sound is right can only be checked by playing.
