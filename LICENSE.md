# License / 라이선스

This repository contains modifications to emulators and Android frontends. Each folder is distributed under the
license of the emulator it modifies; the full license texts are in [`LICENSES/`](LICENSES/).

이 저장소는 에뮬레이터와 안드로이드 앱의 수정 내용입니다. 각 폴더는 그 에뮬레이터의 라이선스로 배포되며,
라이선스 전문은 [`LICENSES/`](LICENSES/) 에 있습니다.

| Folder | Emulator | License | Upstream |
|---|---|---|---|
| `mgba/` | mGBA | Mozilla Public License 2.0 (`LICENSES/mGBA-MPL-2.0.txt`) | https://github.com/mgba-emu/mgba |
| `melonds/` | melonDS | GNU General Public License v3.0 (`LICENSES/melonDS-GPL-3.0.txt`) | https://github.com/melonDS-emu/melonDS |
| `azahar/` | Azahar | GNU General Public License v2.0 or later (`LICENSES/Azahar-GPL-2.0.txt`) | https://github.com/azahar-emu/azahar |
| `lemuroid/` | Lemuroid | GNU General Public License v3.0 (`LICENSES/Lemuroid-GPL-3.0.txt`) | https://github.com/Swordfish90/Lemuroid |
| `libretrodroid/` | LibretroDroid | GNU General Public License v3.0 (`LICENSES/LibretroDroid-GPL-3.0.txt`) | https://github.com/Swordfish90/LibretroDroid |
| `melonds-android/`, `melonds-android-lib/` | melonDS Android | GNU General Public License v3.0 (`LICENSES/melonDS-android-GPL-3.0.txt`) | https://github.com/rafaelvcaetano/melonDS-android |

- `hooks.patch` / `hooks/` contain excerpts of the upstream source (patch context lines); those parts
  remain under the upstream license and copyright.
- New files in `module/` (and `mgba/module-common/`) are released under the same license as the emulator
  in that folder.
- Root documents (`README.md`, `LICENSE.md`) and the automation in `.github/` may be used freely.

If you distribute a build made from this kit, you must follow the emulator's license (for GPL: provide
the complete corresponding source code).

ROMs and data extracted from games are not included and are not covered by any of these licenses.
