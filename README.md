# WYGxrControllerFix

SteamVR add-on that fixes Samsung **Galaxy XR** controller issues when playing PCVR through **Virtual Desktop**.

[한국어 설명](#한국어)

## What it fixes

- **Throws going sideways or weak**: objects thrown with Galaxy XR controllers in SteamVR games
  now fly straight and with full strength.
- **Late releases losing the throw**: if you let go slightly after the peak of your swing,
  the throw keeps its speed (release boost, on by default).
- **Automatic**: active only when needed for Galaxy XR. Quest and Steam Link are left untouched.

**Planned**: keep controllers following your body when they leave the headset cameras,
and smooth re-acquisition.

## Install

1. Download the latest zip from [Releases](../../releases).
2. Close SteamVR, extract to a permanent folder (e.g. `C:\Tools\WYGxrControllerFix`).
3. Run `install.bat`, then start SteamVR.

Uninstall: close SteamVR, run `uninstall.bat`, delete the folder.
Temporarily disable: SteamVR Settings → Startup/Shutdown → Manage Add-ons → `wygxrcontrollerfix`.

Settings: `%LOCALAPPDATA%\WYGxrControllerFix\settings.ini` (applied live while SteamVR runs).

| Key | Default | Meaning |
|---|---|---|
| `vdFrameFix` | `auto` | `auto` = detect, `1` = always, `0` = off |
| `linearScale` | `1.0` | throw strength multiplier |
| `releaseBoost` | `1` | keep throw speed after grip release |
| `releasePeakMs` / `releaseHoldMs` | `150` / `60` | release boost window / hold time |

Check it works: search `vrserver.txt` (Steam\logs) for `WYGxrControllerFix`.
`GXR detected -> fix ON` means the fix is active.

**Scope**: SteamVR games only. VDXR (OpenXR) games are not affected.
Lighthouse controllers (e.g. GripVR) are not touched.

## Build

Requires Visual Studio 2022 and CMake 3.20+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\EstimatorTest.exe
tools\package.ps1 -Version 0.3.2
```

## License

MIT. Third-party: MinHook (BSD 2-Clause), OpenVR headers (BSD 3-Clause), OpenXR headers (Apache 2.0).

---

## 한국어

Galaxy XR을 Virtual Desktop으로 SteamVR에 연결했을 때 생기는 컨트롤러 문제를 고치는 SteamVR 애드온입니다.

- **던지기 보정**: SteamVR 게임에서 물건이 옆으로 날아가거나 약하게 나가던 문제를 고칩니다.
- **놓을 때 속도 보정**: 휘두르는 정점보다 살짝 늦게 놓아도 던지는 힘이 빠지지 않습니다 (기본 켜짐).
- **자동 판별**: Galaxy XR에서만 켜지고 Quest, Steam Link에서는 꺼집니다.
- **예정**: 컨트롤러가 카메라 시야를 벗어났을 때 몸을 따라오게 하기, 다시 잡힐 때 부드럽게 이어 붙이기.

설치: [Releases](../../releases)에서 zip을 받아 고정된 폴더에 풀고, SteamVR을 끈 상태에서 `install.bat`을 실행합니다.
자세한 설명은 zip 안의 `README.txt`를 보세요.
