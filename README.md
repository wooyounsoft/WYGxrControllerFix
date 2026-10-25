# WYGxrControllerFix

Tunes Samsung **Galaxy XR** controllers for PCVR through **Virtual Desktop**: SteamVR games and VDXR (OpenXR) games.

[한국어 설명](#한국어)

## Features

- **Throw tuning**: throws follow your hand motion in direction and strength, in SteamVR games and VDXR games.
- **Release boost**: letting go slightly after the peak of your swing keeps the throw's speed (on by default).
- **Automatic**: active for Galaxy XR only. Quest and Steam Link are left untouched.

**Planned**: keep controllers following your body when they leave the headset cameras,
and smooth re-acquisition.

## Install

1. Download the latest zip from [Releases](../../releases).
2. Close SteamVR, extract to a permanent folder (e.g. `C:\Tools\WYGxrControllerFix`).
3. Run `install.bat`. It registers the SteamVR add-on and the VDXR layer (current user, no admin needed).

Uninstall: close SteamVR, run `uninstall.bat`, delete the folder.
Temporarily disable: SteamVR Settings → Startup/Shutdown → Manage Add-ons → `wygxrcontrollerfix`.

Settings: `%LOCALAPPDATA%\WYGxrControllerFix\settings.ini` (applied live while a game runs).

| Key | Default | Meaning |
|---|---|---|
| `vdFrameFix` | `auto` | `auto` = detect, `1` = always, `0` = off |
| `linearScale` | `1.0` | throw strength multiplier |
| `releaseBoost` | `1` | keep throw speed after grip release |
| `releasePeakMs` / `releaseHoldMs` | `150` / `60` | release boost window / hold time |
| `openxrEnabled` | `1` | VDXR games on/off |

Manager: run `manager.bat`. It shows whether the add-on and the VDXR layer are registered,
whether the fix was active (`GXR detected -> fix ON`) in the last SteamVR / VDXR game,
and turns the fixes on/off (applies within 1 s, even in game).

**Scope**: SteamVR games and VDXR games through Virtual Desktop.
In VDXR games the effect depends on the game: games that calculate throws from hand movement
themselves already work and are not changed.
Lighthouse controllers (e.g. GripVR) are not touched.

## Build

Requires Visual Studio 2022 and CMake 3.20+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\EstimatorTest.exe
tools\package.ps1 -Version 0.4.0
```

## License

MIT. Third-party: MinHook (BSD 2-Clause), OpenVR headers (BSD 3-Clause), OpenXR headers (Apache 2.0).

---

## 한국어

Galaxy XR 컨트롤러를 Virtual Desktop 환경에 맞게 다듬습니다. SteamVR 게임과 VDXR 게임 모두 적용됩니다.

- **던지기 보정**: 던지는 방향과 세기가 손동작대로 나갑니다.
- **놓을 때 속도 보정**: 휘두르는 정점보다 살짝 늦게 놓아도 던지는 힘이 빠지지 않습니다 (기본 켜짐).
- **자동 판별**: Galaxy XR에서만 켜지고 Quest, Steam Link에서는 꺼집니다.
- **VDXR 게임**: 게임에 따라 효과가 다릅니다. 손 움직임으로 던지기를 직접 계산하는 게임은 원래도 잘 되고, 그대로 둡니다.
- **예정**: 컨트롤러가 카메라 시야를 벗어났을 때 몸을 따라오게 하기, 다시 잡힐 때 부드럽게 이어 붙이기.

설치: [Releases](../../releases)에서 zip을 받아 고정된 폴더에 풀고, SteamVR을 끈 상태에서 `install.bat`을 실행합니다.
관리 도구: `manager.bat`을 실행하면 설치 상태와 마지막 게임에서 보정이 켜졌는지 보여 주고, 보정을 켜고 끌 수 있습니다.
자세한 설명은 zip 안의 `README.txt`를 보세요.
