# ff-bgm1x 설정·세이브 옮기기 (Windows)

공식판 에뮬레이터의 **설정 파일**과 **세이브·상태 저장**을 ff-bgm1x 수정판이 읽는 곳으로 복사하는 프로그램입니다.
실행 파일 `ff-bgm1x-settings-mover.exe` 는 [Releases](../../../../releases) 에 있습니다 (설치 없음, .NET Framework 4).

## 쓰는 순서

1. 에뮬레이터를 고릅니다 (mGBA, VBA-M, melonDS, DeSmuME, Azahar).
2. 공식판 폴더와 ff-bgm1x 폴더를 고릅니다. exe 를 ff-bgm1x 폴더들 옆에 두면 ff-bgm1x 폴더는 자동으로 채워집니다.
3. "설정", "세이브·상태 저장" 중 옮길 것을 고릅니다.
4. "미리 보기" 로 무엇을 어디서 어디로 복사하는지 확인합니다.
5. "옮기기" 를 누릅니다. 받는 쪽에 같은 파일이 있으면 먼저 `ff-bgm1x-backup-<날짜시각>` 폴더에 백업합니다.
   해당 에뮬레이터가 실행 중이면 복사하지 않습니다.

## 에뮬레이터별로 옮기는 것

| 에뮬레이터 | 설정 | 세이브·상태 저장 |
|---|---|---|
| mGBA | `config.ini`, `qt.ini` | 설정에 정한 세이브·상태 저장·치트 폴더 |
| VBA-M | `vbam.ini` | 설정의 BatteryDir, StateDir 폴더 |
| melonDS | `melonDS.toml` | 설정에 정한 세이브·상태 저장·치트 폴더 |
| DeSmuME | `desmume.ini` | `Battery`, `States`, `Cheats` 폴더 (설정에 다른 경로가 있으면 그 경로) |
| Azahar | `user\config` | `sdmc`, `nand`, `sysdata`, `states`, `cheats` 폴더 |

- 세이브를 롬 옆에 저장하는 경우(mGBA·VBA-M·melonDS 기본값)는 두 판이 이미 같은 파일을 쓰므로 복사하지 않습니다.
- 설정에 절대 경로로 적힌 폴더는 두 판이 그대로 함께 쓰므로 복사하지 않습니다. 상대 경로면 복사합니다.
- 두 판이 같은 AppData 폴더를 쓰면(예: Azahar `%APPDATA%\Azahar`) 옮길 것이 없다고 알립니다.

## 빌드

```
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe /codepage:65001 /out:ff-bgm1x-settings-mover.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll Mover.cs
```

---

**English.** A small Windows tool that copies the settings file and save/savestate folders of an official emulator
install (mGBA, VBA-M, melonDS, DeSmuME, Azahar) to where the ff-bgm1x build reads them. Existing files are backed up
first to `ff-bgm1x-backup-<timestamp>`. Saves stored next to the ROM, or in absolute paths, are already shared and are
not copied. The UI is in Korean. Prebuilt exe in Releases; build with the command above.
