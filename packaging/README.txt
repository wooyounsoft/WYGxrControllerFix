WYGxrControllerFix v0.5.0 - Galaxy XR + Virtual Desktop 컨트롤러 보정
=====================================================================

[무엇을 하나요]
Galaxy XR 컨트롤러를 Virtual Desktop(VD) 환경에 맞게 보정해서
던지는 방향과 세기가 손동작대로 나가게 합니다.
지연이나 떨림은 추가되지 않습니다.

  - 적용: SteamVR 게임 (예: Half-Life: Alyx)
          VDXR(OpenXR)로 실행되는 게임
          (VDXR 게임은 게임에 따라 효과가 다릅니다. 손 움직임으로 던지기를
           직접 계산하는 게임은 원래도 잘 되고, 그대로 둡니다)
  - 자동 판별: 필요한 기기(Galaxy XR)에만 적용하고,
    Quest / Steam Link 에서는 스스로 꺼집니다.
    (판별 전 처음 몇 초는 보정을 적용한 상태로 시작합니다.
     VDXR 게임은 휘두르는 동안 보정을 조금씩 더 정밀하게 다듬습니다)
  - 손 위치 정확도 (SteamVR 게임): 손 위치와 각도가 실제 손에 더 가깝게 맞습니다.
  - 취향 조정 (기본 꺼짐, manager.bat 에서 켜기):
      진동 강화 / 컨트롤러 각도 조정 / 스틱 데드존과 감도
  - OpenVR Space Calibrator, 라이트하우스 컨트롤러(GripVR 등)와 함께 사용 가능
    (라이트하우스 컨트롤러에는 적용되지 않습니다)

[설치]
1. SteamVR을 종료합니다.
2. 이 압축 파일을 지우지 않을 고정된 폴더에 풉니다. 예) C:\Tools\WYGxrControllerFix
   (다운로드 폴더나 압축 파일 안에서 바로 실행하지 마세요)
3. install.bat 을 실행합니다.
4. SteamVR 게임과 VDXR 게임 모두 자동으로 적용됩니다.
   (VDXR 레이어는 현재 사용자 레지스트리에 등록되며 관리자 권한이 필요 없습니다)
(이전 이름 GxrThrowFix 를 설치했었다면: install.bat 이 이전 버전 등록을 자동으로 해제합니다.
 예전 폴더와 %LOCALAPPDATA%\GxrThrowFix 는 지워도 됩니다. 설정 파일 위치가
 %LOCALAPPDATA%\WYGxrControllerFix\settings.ini 로 바뀌어 새 기본값으로 시작합니다)

[제거]
1. SteamVR을 종료하고 uninstall.bat 을 실행합니다.
2. 폴더를 지웁니다.
(임시로 끄기: SteamVR 설정 > 시작/종료 > 애드온 관리 에서 wygxrcontrollerfix 끄기 후 SteamVR 재시작)

[설정]  manager.bat 으로 주요 항목을 켜고 끌 수 있습니다.
       직접 고치려면 %LOCALAPPDATA%\WYGxrControllerFix\settings.ini
게임 실행 중에 저장하면 1초 안에 반영됩니다 (재시작 불필요).
  enabled=0/1            전체 끄기/켜기 (바로 비교해 볼 수 있음)
  openxrEnabled=0/1      VDXR 게임 보정만 끄기/켜기
  poseLagFix=0/1         손 위치 정확도, SteamVR 게임 (기본 켜짐)
  hapticBoost=0/1        진동 강화 (기본 꺼짐), hapticGain=1.5 세기 배율, hapticMinMs=20 최소 길이
  angleOffset=0/1        컨트롤러 각도 조정 (기본 꺼짐), angleOffsetDeg=0 (+ = 앞쪽을 위로)
  stickTuning=0/1        스틱 조정 (기본 꺼짐), stickDeadzone=0.10, stickCurve=1.0 (클수록 가운데가 덜 민감)
  vdFrameFix=auto        auto = 자동 판별 (권장), 1 = 항상 적용, 0 = 끄기
  linearScale            던지는 힘 배율 (기본 1.0). 너무 세게 느껴지면 0.85~0.9
  angularScale           손목 회전 배율 (기본 1.0)
  releaseBoost=0/1       놓는 순간 직전 최고 속도를 잠깐 유지 (기본 켜짐).
                         최고 속도보다 조금 늦게 놓아도 힘이 빠지지 않게 합니다.
                         너무 세게 느껴지면 0 으로 끄세요.
  releasePeakMs=150      놓기 직전 이 시간(ms) 안의 최고 속도를 사용
  releaseHoldMs=60       놓은 뒤 이 시간(ms) 동안 최고 속도를 유지
                         힘이 아직 빠지면 200~300 / 80~100 으로 늘려 보세요.
  releaseMaxPeakAgeMs=80 최고 속도가 놓기보다 이 시간(ms) 이상 앞서면 적용 안 함
                         (휘두른 뒤 멈췄다가 내려놓을 때 물건이 튀어 나가지 않게)

참고: 처음에는 던지는 힘이 이전보다 세게 느껴질 수 있습니다.

[알려진 한계]
- 게임마다 던지기 처리 방식이 달라서, 라이트하우스 컨트롤러로도 던지기가
  어색한 게임이 있습니다 (예: Battle Sister). 비교는 Half-Life: Alyx 처럼
  던지기가 잘 만들어진 게임에서 해 주세요.
- Steam Link용 Galaxy XR 드라이버(GalaxyXR-Driver, GalaxyXRNative)를 설치했다면
  Virtual Desktop으로 할 때는 꺼 주세요 (SteamVR 설정 → 시작/종료 → 애드온 관리).
  VD 세션에도 개입해서 던지기가 어색해질 수 있습니다. Steam Link으로 할 때 다시 켜면 됩니다.
  galaxyxr_driver_mode.bat 을 실행하면 SteamVR 을 종료하고 VD 모드(끔) / Steam Link 모드(켬)를 바꿔 줍니다.

[문제 해결 / 피드백]
- manager.bat (관리 도구): 설치 상태, 마지막 게임에서 보정이 켜졌는지 보여 주고,
  보정 전체 / VDXR 게임 보정 / 놓을 때 속도 보정을 번호로 켜고 끕니다 (게임 중에도 1초 안에 반영).
- 동작 확인 (SteamVR): C:\Program Files (x86)\Steam\logs\vrserver.txt 에서 "WYGxrControllerFix" 검색
    "TARGET"                       컨트롤러에 적용 중
    "GXR detected -> fix ON"       Galaxy XR 감지, 보정 사용
    "not needed -> fix OFF"        보정이 필요 없는 기기, 보정 안 함
- 동작 확인 (VDXR): %LOCALAPPDATA%\WYGxrControllerFix\openxr_layer.log
    "ACTIVE"                       VDXR 게임에 적용 중
    "GXR detected -> fix ON"       위와 같음
- 피드백 주실 때 위 로그 줄과 게임 이름, 느낌(방향/세기/떨림)을 알려 주세요.
- 백신이 경고할 수 있습니다: SteamVR 안에서 컨트롤러 데이터를 보정하는
  방식이라 그렇습니다. OpenVR Space Calibrator와 같은 방식입니다.

----------------------------------------------------------------
English (short)
Tunes Samsung Galaxy XR controllers through Virtual Desktop, for SteamVR games and
VDXR (OpenXR) games: throws follow your hand motion in direction and strength.
No added latency or jitter.
Also: more accurate hand position in SteamVR games,
and optional tuning (off by default): stronger haptics, controller angle,
thumbstick deadzone/curve. Turn each on/off in manager.bat.
Auto-detection: the fix turns itself off on devices that don't need it
(Quest via VD, Steam Link). Lighthouse controllers are not touched.
If you also use the Galaxy XR driver for Steam Link (GalaxyXR-Driver, GalaxyXRNative),
turn it off while playing through Virtual Desktop (SteamVR Settings > Startup/Shutdown >
Manage Add-ons); it also hooks into VD sessions and throws can feel off.
galaxyxr_driver_mode.bat switches it for you (VD mode = off, Steam Link mode = on).
Install: close SteamVR, extract to a permanent folder, run install.bat
(registers the SteamVR add-on and the VDXR layer for the current user).
Uninstall: close SteamVR, run uninstall.bat, delete the folder.
Settings: %LOCALAPPDATA%\WYGxrControllerFix\settings.ini (live reload)
Manager: run manager.bat to see the status (registration, whether the fix was active in the
last game) and to turn the fixes on/off (applies within 1 s, even in game).
  vdFrameFix=auto|1|0, linearScale (default 1.0),
  releaseBoost=0|1 (default 1: holds the peak speed of the last 150 ms for 60 ms
  after grip release; tune with releasePeakMs / releaseHoldMs).

License: MIT (LICENSE.txt)
Third-party: MinHook (BSD 2-Clause), OpenVR headers (BSD 3-Clause), OpenXR headers (Apache 2.0) - see licenses\
