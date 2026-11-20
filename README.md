# ff-bgm1x 이식 자료 (AI 용)

> 에뮬레이터 새 버전이 나오면 아래 프롬프트로 AI 코딩 도구에 맡겨 직접 적용할 수 있습니다.
> 예: Azahar 2126.2 → 2127.0-rc3 시험 이식에서 연결 패치 36개 파일 중 28개가 그대로 들어갔고, PC판은 충돌 3곳·API 변경 1곳만 고쳐 약 10분 만에 첫 실행까지 됐습니다.
> 이슈에는 공식 새 버전의 [자동 점검](#새-버전-자동-점검) 결과가 올라옵니다.
>
> *English: see [English](#english) below.*

배속(빨리 감기) 중에도 **BGM 과 효과음을 1배속으로** 들리게 하는 에뮬레이터 수정판(ff-bgm1x)을,
에뮬레이터의 **공식 새 버전**에 다시 적용하기 위한 자료입니다.
새 버전이 나오면 이 저장소(또는 폴더)를 AI 코딩 도구에 주고 아래 프롬프트로 요청하세요.

**무엇을 바꿨는지는 [FEATURES.md](FEATURES.md) 에 정리되어 있습니다** (기능, 에뮬레이터별 방식, 새 버전에 대한 생각).

공식판의 설정·세이브를 수정판으로 옮기는 프로그램은 [tools/settings-mover](tools/settings-mover/README.md) 에 있습니다 (실행 파일은 아래 표).

## 받기

완성된 실행 파일은 [Releases](../../releases/latest) 에 모두 있습니다 (2026.11.20).
수정한 소스는 에뮬레이터마다 원본을 포크한 저장소에 있습니다. 브랜치마다 원본 공식 버전 위의 커밋 하나입니다.

### Windows

| 기기 | 에뮬레이터 | 받기 | 소스 |
|---|---|---|---|
| GBA, GB/GBC | mGBA 0.10.5 | [zip](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-mGBA-0.10.5-win64.zip) | [emu-bgm1x/mgba](https://github.com/emu-bgm1x/mgba/tree/bgm) `bgm` |
| GBA, GB/GBC | VisualBoyAdvance-M 2.2.3 | [zip](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-VBA-M-2.2.3-win64.zip) | [emu-bgm1x/visualboyadvance-m](https://github.com/emu-bgm1x/visualboyadvance-m/tree/bgm) `bgm` |
| NDS | melonDS 1.1 | [zip](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-melonDS-1.1-win64.zip) | [emu-bgm1x/melonDS](https://github.com/emu-bgm1x/melonDS/tree/bgm) `bgm` |
| NDS | DeSmuME 0.9.13 | [zip](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-DeSmuME-0.9.13-win64.zip) | [emu-bgm1x/desmume](https://github.com/emu-bgm1x/desmume/tree/bgm) `bgm` |
| 3DS | Azahar 2126.2 | [zip](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-Azahar-2126.2-win64.zip) | [emu-bgm1x/azahar](https://github.com/emu-bgm1x/azahar/tree/main) `main` |
| (도구) | 설정·세이브 옮기기 | [exe](../../releases/download/2026.11.20/ff-bgm1x-settings-mover.exe) | [tools/settings-mover](tools/settings-mover/README.md) |

### Android

| 기기 | 앱 | 받기 | 설치 | 소스 |
|---|---|---|---|---|
| GBA, GB/GBC | Lemuroid | [64비트 apk](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-Lemuroid-64bit.apk), [32비트 apk](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-Lemuroid-32bit.apk) | 공식 앱과 함께 설치됨 | [Lemuroid](https://github.com/emu-bgm1x/Lemuroid/tree/main), [LibretroDroid](https://github.com/emu-bgm1x/LibretroDroid/tree/main) `main`, [Gambatte](https://github.com/emu-bgm1x/gambatte-libretro/tree/bgm) `bgm`, GBA 코어는 위 mGBA·VBA-M |
| NDS | melonDS | [apk](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-melonDS-android.apk) | 공식 앱을 지우고 설치 (서명이 다름) | [emu-bgm1x/melonDS-android](https://github.com/emu-bgm1x/melonDS-android/tree/main) `main` (코어: [emu-bgm1x/melonDS](https://github.com/emu-bgm1x/melonDS/tree/android-lib) `android-lib`) |
| 3DS | Azahar | [apk](../../releases/download/2026.11.20/ff-bgm1x-2026.11.20-Azahar-2126.2-android.apk) | 공식 앱과 함께 설치됨 | [emu-bgm1x/azahar](https://github.com/emu-bgm1x/azahar/tree/main) `main` |

- 안드로이드판은 Play 스토어가 아닌 APK 로 배포하므로, 설치할 때 "출처를 알 수 없는 앱" 허용이 필요합니다.
- 모바일 앱의 포크에는 브랜치가 두 개 있습니다. `main` 은 통합본(1배속 소리 + 화면·버튼 배치 편집기, 스킨), `skin` 은 배치 편집기·스킨만입니다.

## 이식 자료

| 폴더 | 에뮬레이터 | 기준 버전 | 대상 기기 |
|---|---|---|---|
| `mgba/` | mGBA | 0.10.5 | GBA, GB/GBC |
| `melonds/` | melonDS | 1.1 | NDS |
| `desmume/` | DeSmuME | 0.9.13 | NDS (Windows) |
| `vbam/` | VisualBoyAdvance-M | 2.2.3 | GBA, GB/GBC (Windows) |
| `azahar/` | Azahar | 2126.2 | 3DS (PC, 안드로이드) |
| `lemuroid/`, `libretrodroid/`, `gambatte/` | Lemuroid, LibretroDroid, Gambatte libretro | `3625bc12`, `8835c30`, `b752252` | 안드로이드 GBA, GB/GBC (GBA 코어는 `mgba/` 의 libretro, 선택하면 `vbam/` 의 libretro) |
| `melonds-android/`, `melonds-android-lib/` | melonDS Android | `c42995ca`, `431ab4b` | 안드로이드 NDS |

각 폴더의 구성:

- `PORTING-AI.md` — 이식 안내서. 구조, 원본에 끼우는 지점, 순서, 확인 방법, 알려진 함정. **AI 가 가장 먼저 읽을 문서.**
- `module/` — 새로 만든 파일. 원본 소스 트리와 같은 경로로 들어 있어 그대로 덮어 복사합니다.
- `hooks.patch` — 원본 파일에 끼우는 연결부 전체 (`git apply -3 hooks.patch`).
- `hooks/` — 같은 내용을 원본 파일 하나당 패치 하나로 나눈 것. 충돌 난 파일만 볼 때 씁니다.
- `FEATURES-ko.txt` — 배포판의 읽어주세요. 기능과 메뉴, 동작 규칙 설명.

## 프롬프트 (복사해서 쓰세요)

```
이 폴더는 에뮬레이터 수정판 ff-bgm1x 의 이식 자료다. <에뮬레이터 이름> 의 공식 최신 버전 소스에
이 기능을 다시 적용해서 빌드해 줘.

1. <에뮬레이터 폴더>/PORTING-AI.md 를 처음부터 끝까지 읽고 그 순서대로 진행해.
2. 공식 저장소에서 최신 릴리스 태그를 받아 새 브랜치를 만들고, module/ 을 복사한 뒤
   hooks.patch 를 git apply -3 으로 적용해.
3. 충돌이 나거나 원본 함수 이름·구조가 바뀌었으면, PORTING-AI.md 의 "끼우는 지점" 표에 적힌
   '역할'을 기준으로 새 버전의 같은 역할을 하는 곳을 찾아 옮겨. 추측으로 동작을 바꾸지 말고,
   못 찾은 지점은 나에게 알려 줘.
4. 빌드하고, PORTING-AI.md 의 "확인 방법" 을 나와 함께 진행해. 게임 화면이나 소리는 내가 확인한다.
5. 원본에 없던 기능 변경이나 정리는 하지 마. 기능은 FEATURES-ko.txt 에 적힌 그대로 유지한다.
```

## 설정·세이브 옮기기 (Windows 도구)

공식판 에뮬레이터(mGBA, VBA-M, melonDS, DeSmuME, Azahar)의 **설정 파일**과 **세이브·상태 저장**을
ff-bgm1x 수정판이 읽는 곳으로 복사하는 프로그램입니다. 실행 파일 `ff-bgm1x-settings-mover.exe` 는 Releases 에 있습니다 (설치 없음).

- 에뮬레이터와 두 폴더(공식판, ff-bgm1x)를 고르고, "미리 보기" 로 무엇을 어디로 복사하는지 확인한 뒤 "옮기기" 를 누릅니다.
- 받는 쪽에 같은 파일이 있으면 먼저 `ff-bgm1x-backup-<날짜시각>` 폴더에 백업합니다. 에뮬레이터가 실행 중이면 복사하지 않습니다.
- 롬 옆이나 절대 경로에 저장하는 세이브는 두 판이 이미 같은 파일을 쓰므로 복사하지 않습니다.
- 에뮬레이터별로 옮기는 파일과 빌드 방법은 [tools/settings-mover](tools/settings-mover/README.md) 에 있습니다.

## 새 버전 자동 점검

GitHub Actions(`.github/workflows/upstream-check.yml`)가 매주 월요일 다섯 에뮬레이터(mGBA·melonDS·Azahar·VBA-M·DeSmuME)의 공식 최신 릴리스를 확인합니다.
안드로이드판(Lemuroid·LibretroDroid·Gambatte·melonDS Android)은 점검하지 않습니다.
기준 버전보다 새 릴리스가 있으면 그 소스에 `hooks.patch` 가 그대로 들어가는지 시험하고, 결과를
이슈(`[upstream] <에뮬레이터> <버전>`)로 남깁니다. 같은 버전은 한 번만 올립니다.

- "그대로 적용됨" 이어도 빌드나 소리까지 확인한 것은 아닙니다.
- "충돌" 이면 이슈에 적힌 파일만 `PORTING-AI.md` 의 '역할' 을 보고 옮기면 됩니다.

Actions 탭에서 `Run workflow` 로 바로 돌려 볼 수도 있습니다.

## 알아 둘 점

- 새 버전에서 그대로 동작한다는 보장은 없습니다. 에뮬레이터의 오디오 내부 구조가 크게 바뀌면
  연결부를 다시 짜야 하고, 소리가 맞는지는 게임을 실제로 돌려 봐야 알 수 있습니다.
- 롬(게임 파일)과 롬에서 뽑은 데이터는 이 자료에 없고, 배포해서도 안 됩니다.
- 라이선스: [LICENSE.md](LICENSE.md). 각 폴더는 그 에뮬레이터의 라이선스를 따릅니다.
  수정판을 배포할 때는 해당 라이선스대로 소스를 함께 공개하세요.

---

## English

When a new emulator version comes out, you can re-apply the mod yourself with an AI coding assistant and the prompt below.
For example, in a trial port of Azahar 2126.2 → 2127.0-rc3, 28 of the 36 files in the hook patch applied unchanged, and the PC build
ran for the first time after about 10 minutes, with only 3 conflicts and 1 API change to fix.
Issues show the results of the [automatic upstream check](#새-버전-자동-점검) (described in Korean above).

ff-bgm1x is a set of emulator modifications that keep **BGM and sound effects playing at 1x speed while
fast-forwarding**, so the game runs fast but sounds normal. This repository is a *porting kit*: it contains
everything an AI coding assistant needs to re-apply the feature to a **newer official
release** of each emulator.
A tool that copies settings and saves from the official emulators to the ff-bgm1x builds is in [tools/settings-mover](tools/settings-mover/README.md).

**What was changed is summarized in [FEATURES.md](FEATURES.md)** (features, how each emulator works, notes on new versions).

### Downloads

All prebuilt files are in [Releases](../../releases/latest) (2026.11.20). The modified source of each emulator is in a fork of
the upstream repository; each branch is a single commit on top of the official upstream version.
The download and source links are the same as in the two tables above ([Windows](#windows), [Android](#android)).
The Android builds are APKs (not on the Play Store), so installing them requires allowing "unknown sources".
The GBA/GB (Lemuroid) and 3DS APKs use different package names and install next to the official apps; the NDS (melonDS)
APK uses the official package name with a different signature, so uninstall the official app first.
The forks of the mobile apps have two branches: `main` (integrated: 1x sound plus the screen/button layout editor and skins)
and `skin` (layout editor and skins only).

### Porting kit

| Folder | Emulator | Base version | System |
|---|---|---|---|
| `mgba/` | mGBA | 0.10.5 | GBA, GB/GBC |
| `melonds/` | melonDS | 1.1 | NDS |
| `desmume/` | DeSmuME | 0.9.13 | NDS (Windows) |
| `vbam/` | VisualBoyAdvance-M | 2.2.3 | GBA, GB/GBC (Windows) |
| `azahar/` | Azahar | 2126.2 | 3DS (PC, Android) |
| `lemuroid/`, `libretrodroid/`, `gambatte/` | Lemuroid, LibretroDroid, Gambatte libretro | `3625bc12`, `8835c30`, `b752252` | Android GBA, GB/GBC (GBA core: libretro part of `mgba/`, or optionally of `vbam/`) |
| `melonds-android/`, `melonds-android-lib/` | melonDS Android | `c42995ca`, `431ab4b` | Android NDS |

Each folder has:

- `PORTING-AI.md` — the porting guide (architecture, where the hooks go and *what role* each one plays,
  steps, how to verify, known pitfalls). Written in Korean; AI assistants can read it as is.
- `module/` — new source files, laid out at their paths in the upstream tree.
- `hooks.patch` — all edits to upstream files (`git apply -3 hooks.patch`); `hooks/` has one patch per file.
- `FEATURES-ko.txt` — the user-facing readme: menus, options and behavior rules.

### Settings mover (Windows tool)

Copies the **settings file** and **save/savestate folders** of an official emulator install (mGBA, VBA-M, melonDS,
DeSmuME, Azahar) to where the ff-bgm1x build reads them. The exe `ff-bgm1x-settings-mover.exe` is in Releases (no install).

- Pick the emulator and both folders (official, ff-bgm1x), check "미리 보기" (preview), then press "옮기기" (move). The UI is in Korean.
- Existing files on the receiving side are backed up first to `ff-bgm1x-backup-<timestamp>`. Nothing is copied while the emulator is running.
- Saves stored next to the ROM or in absolute paths are already shared by both builds and are not copied.
- Files per emulator and the build command: [tools/settings-mover](tools/settings-mover/README.md).

### Prompt

```
This folder is the porting kit for ff-bgm1x, an emulator mod. Re-apply the feature to the latest
official release of <emulator> and build it.

1. Read <emulator folder>/PORTING-AI.md completely and follow its steps in order.
2. Check out the latest release tag from the official repository on a new branch, copy module/,
   and apply hooks.patch with `git apply -3`.
3. If a hunk conflicts or upstream code was restructured, use the "role" column of the hook table in
   PORTING-AI.md to find the place in the new version that does the same job. Do not change behavior
   by guessing; tell me about any hook point you cannot find.
4. Build it, then go through the "verification" section of PORTING-AI.md with me. I will check the
   game screen and sound myself.
5. Do not add features or refactor. Keep the behavior exactly as described in FEATURES-ko.txt.
```

### Notes

- There is no guarantee that a newer release works unchanged. If the emulator's audio internals change
  a lot, the hooks must be reworked, and whether the sound is right can only be checked by playing.
- No ROMs or ROM-derived data are included, and none may be distributed.
- License: see [LICENSE.md](LICENSE.md). Each folder follows the license of its emulator.
