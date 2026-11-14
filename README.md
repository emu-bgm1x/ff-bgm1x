# ff-bgm1x 이식 자료 (AI 용)

> **관리하지 않는 저장소입니다.** 만든 사람은 더 이상 새 버전을 만들거나 문의에 답하지 않습니다.
> 에뮬레이터 새 버전에 적용하려면 아래 프롬프트로 AI 에게 맡기세요.
> 이슈에는 [자동 점검](#새-버전-자동-점검) 결과만 올라옵니다.
>
> *English: this repository is **not maintained**. See [English](#english) below.*

배속(빨리 감기) 중에도 **BGM 과 효과음을 1배속으로** 들리게 하는 에뮬레이터 수정판(ff-bgm1x)을,
에뮬레이터의 **공식 새 버전**에 다시 적용하기 위한 자료입니다.
새 버전이 나오면 이 저장소(또는 폴더)를 AI 코딩 도구에 주고 아래 프롬프트로 요청하세요.

**무엇을 바꿨는지는 [FEATURES.md](FEATURES.md) 에 정리되어 있습니다** (기능, 에뮬레이터별 방식, 새 버전에 대한 생각).

완성된 실행 파일(Windows)은 [Releases](../../releases) 에 있습니다.
공식판의 설정·세이브를 수정판으로 옮기는 프로그램은 [tools/settings-mover](tools/settings-mover/README.md) 에 있습니다 (실행 파일은 Releases).

| 폴더 | 에뮬레이터 | 기준 버전 | 대상 기기 |
|---|---|---|---|
| `mgba/` | mGBA | 0.10.5 | GBA, GB/GBC |
| `melonds/` | melonDS | 1.1 | NDS |
| `desmume/` | DeSmuME | 0.9.13 | NDS (Windows) |
| `vbam/` | VisualBoyAdvance-M | 2.2.3 | GBA, GB/GBC (Windows) |
| `azahar/` | Azahar | 2126.2 | 3DS (PC, 안드로이드) |
| `lemuroid/`, `libretrodroid/`, `gambatte/` | Lemuroid, LibretroDroid, Gambatte libretro | `3625bc12`, `8835c30`, `b752252` | 안드로이드 GBA, GB/GBC (GBA 코어는 `mgba/` 의 libretro, 선택하면 `vbam/` 의 libretro) |
| `melonds-android/`, `melonds-android-lib/` | melonDS Android | `c42995ca`, `431ab4b` | 안드로이드 NDS |

안드로이드판은 Play 스토어가 아닌 APK 로 배포하므로, 설치할 때 "출처를 알 수 없는 앱" 허용이 필요합니다.
릴리스의 GBA·3DS APK 는 공식 앱과 패키지 이름이 달라 함께 설치되고, NDS(melonDS) APK 는 공식 앱과 서명이 달라 공식 앱을 지우고 설치해야 합니다.

각 폴더의 구성:

- `PORTING-AI.md` — 이식 안내서. 구조, 원본에 끼우는 지점, 순서, 확인 방법, 알려진 함정. **AI 가 가장 먼저 읽을 문서.**
- `module/` — 새로 만든 파일. 원본 소스 트리와 같은 경로로 들어 있어 그대로 덮어 복사합니다.
  (mGBA 는 `module-common/` 도 있습니다. mGBA 소스 폴더 **옆**의 `common/` 폴더가 됩니다.)
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

## 새 버전 자동 점검

GitHub Actions(`.github/workflows/upstream-check.yml`)가 매주 월요일 세 에뮬레이터의 공식 최신 릴리스를 확인합니다.
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

**This repository is not maintained.** The author no longer makes new versions or answers questions.
Issues are only used for the automatic upstream check below.

ff-bgm1x is a set of emulator modifications that keep **BGM and sound effects playing at 1x speed while
fast-forwarding**, so the game runs fast but sounds normal. This repository is a *porting kit*: it contains
everything an AI coding assistant needs to re-apply the feature to a **newer official
release** of each emulator. Prebuilt Windows binaries of the last version are in [Releases](../../releases).
A tool that copies settings and saves from the official emulators to the ff-bgm1x builds is in [tools/settings-mover](tools/settings-mover/README.md).

**What was changed is summarized in [FEATURES.md](FEATURES.md)** (features, how each emulator works, notes on new versions).

| Folder | Emulator | Base version | System |
|---|---|---|---|
| `mgba/` | mGBA | 0.10.5 | GBA, GB/GBC |
| `melonds/` | melonDS | 1.1 | NDS |
| `desmume/` | DeSmuME | 0.9.13 | NDS (Windows) |
| `vbam/` | VisualBoyAdvance-M | 2.2.3 | GBA, GB/GBC (Windows) |
| `azahar/` | Azahar | 2126.2 | 3DS (PC, Android) |
| `lemuroid/`, `libretrodroid/`, `gambatte/` | Lemuroid, LibretroDroid, Gambatte libretro | `3625bc12`, `8835c30`, `b752252` | Android GBA, GB/GBC (GBA core: libretro part of `mgba/`, or optionally of `vbam/`) |
| `melonds-android/`, `melonds-android-lib/` | melonDS Android | `c42995ca`, `431ab4b` | Android NDS |

The Android builds are distributed as APKs (not on the Play Store), so installing them requires allowing
"unknown sources". The GBA and 3DS APKs use different package names and install next to the official apps; the NDS (melonDS) APK uses the official package name with a different signature, so uninstall the official app first.

Each folder has:

- `PORTING-AI.md` — the porting guide (architecture, where the hooks go and *what role* each one plays,
  steps, how to verify, known pitfalls). Written in Korean; AI assistants can read it as is.
- `module/` — new source files, laid out at their paths in the upstream tree (mGBA also has `module-common/`,
  which becomes a `common/` folder **next to** the mGBA source folder).
- `hooks.patch` — all edits to upstream files (`git apply -3 hooks.patch`); `hooks/` has one patch per file.
- `FEATURES-ko.txt` — the user-facing readme: menus, options and behavior rules.

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
