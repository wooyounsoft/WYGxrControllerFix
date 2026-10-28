# WYGxrControllerFix

Tunes Samsung **Galaxy XR** controllers for PCVR through **Virtual Desktop**: SteamVR games and VDXR (OpenXR) games.

[한국어 설명](#한국어)

## Features

- **Throw tuning**: throws follow your hand motion in direction and strength, in SteamVR games and VDXR games.
- **Release boost**: letting go slightly after the peak of your swing keeps the throw's speed (on by default).
- **Automatic**: active for Galaxy XR only. Quest and Steam Link are left untouched.
- **Hand position accuracy** (SteamVR games): hand position and angle match your real hand more closely.
- **Optional tuning** (off by default): stronger haptics, controller angle, thumbstick deadzone/curve.

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
| `releaseMaxPeakAgeMs` | `80` | skip the boost when the peak is older than this (stop, then place) |
| `openxrEnabled` | `1` | VDXR games on/off |
| `poseLagFix` | `1` | hand position accuracy (SteamVR games) |
| `hapticBoost` / `hapticGain` / `hapticMinMs` | `0` / `1.5` / `20` | stronger haptics |
| `angleOffset` / `angleOffsetDeg` | `0` / `0` | controller angle (+ = tip up) |
| `stickTuning` / `stickDeadzone` / `stickCurve` | `0` / `0.10` / `1.0` | thumbstick deadzone / response curve |

Manager: run `manager.bat`. It shows whether the add-on and the VDXR layer are registered,
whether the fix was active (`GXR detected -> fix ON`) in the last SteamVR / VDXR game,
and turns the fixes on/off (applies within 1 s, even in game).

**Scope**: SteamVR games and VDXR games through Virtual Desktop.
In VDXR games the effect depends on the game: games that calculate throws from hand movement
themselves already work and are not changed.
Lighthouse controllers (e.g. GripVR) are not touched.

**Steam Link users**: if you also installed the Galaxy XR driver for Steam Link
(GalaxyXR-Driver / Galaxy XR Companion, `GalaxyXRNative`), turn it off while playing through
Virtual Desktop (SteamVR Settings → Startup/Shutdown → Manage Add-ons). It also hooks into
Virtual Desktop sessions, and throws can feel off. Turn it back on for Steam Link.
`galaxyxr_driver_mode.bat` (in the zip, or [download it alone](packaging/galaxyxr_driver_mode.bat))
does this for you: it closes SteamVR and switches between VD mode (driver off) and Steam Link mode (driver on).

## Build

Requires Visual Studio 2022 and CMake 3.20+.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\EstimatorTest.exe
tools\package.ps1 -Version 0.5.0
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
- **손 위치 정확도** (SteamVR 게임): 손 위치와 각도가 실제 손에 더 가깝게 맞습니다.
- **취향 조정** (기본 꺼짐): 진동 강화, 컨트롤러 각도 조정, 스틱 데드존/감도. `manager.bat`에서 켜고 끕니다.

설치: [Releases](../../releases)에서 zip을 받아 고정된 폴더에 풀고, SteamVR을 끈 상태에서 `install.bat`을 실행합니다.
관리 도구: `manager.bat`을 실행하면 설치 상태와 마지막 게임에서 보정이 켜졌는지 보여 주고, 보정을 켜고 끌 수 있습니다.
자세한 설명은 zip 안의 `README.txt`를 보세요.

**Steam Link도 쓰는 분**: Steam Link용 Galaxy XR 드라이버(GalaxyXR-Driver / Galaxy XR Companion, `GalaxyXRNative`)를
설치했다면, Virtual Desktop으로 할 때는 꺼 주세요 (SteamVR 설정 → 시작/종료 → 애드온 관리).
Virtual Desktop 세션에도 개입해서 던지기가 어색해질 수 있습니다. Steam Link으로 할 때 다시 켜면 됩니다.
`galaxyxr_driver_mode.bat` (zip 안에 있음, [따로 받기](packaging/galaxyxr_driver_mode.bat))을 실행하면
SteamVR을 종료하고 VD 모드(드라이버 끔) / Steam Link 모드(드라이버 켬)를 바로 바꿔 줍니다.
