WYGxrControllerFix v0.3.2 - Galaxy XR + Virtual Desktop SteamVR 컨트롤러 보정
=====================================================================

[무엇을 하나요]
Galaxy XR 컨트롤러를 Virtual Desktop(VD) + SteamVR 환경에 맞게 보정해서
던지는 방향과 세기가 손동작대로 나가게 합니다.
지연이나 떨림은 추가되지 않습니다.

  - 적용: SteamVR 게임 (예: Half-Life: Alyx)
  - 미적용: VDXR(OpenXR)로 직접 실행되는 게임
  - 자동 판별: 필요한 기기(Galaxy XR)에만 적용하고,
    Quest / Steam Link 에서는 스스로 꺼집니다.
    (판별 전 처음 몇 초는 보정을 적용한 상태로 시작합니다)
  - OpenVR Space Calibrator, 라이트하우스 컨트롤러(GripVR 등)와 함께 사용 가능
    (라이트하우스 컨트롤러에는 적용되지 않습니다)

[설치]
1. SteamVR을 종료합니다.
2. 이 압축 파일을 지우지 않을 고정된 폴더에 풉니다. 예) C:\Tools\WYGxrControllerFix
   (다운로드 폴더나 압축 파일 안에서 바로 실행하지 마세요)
3. install.bat 을 실행합니다.
4. SteamVR을 실행하면 자동으로 적용됩니다.
(이전 이름 GxrThrowFix 를 설치했었다면: install.bat 이 이전 버전 등록을 자동으로 해제합니다.
 예전 폴더와 %LOCALAPPDATA%\GxrThrowFix 는 지워도 됩니다. 설정 파일 위치가
 %LOCALAPPDATA%\WYGxrControllerFix\settings.ini 로 바뀌어 새 기본값으로 시작합니다)

[제거]
1. SteamVR을 종료하고 uninstall.bat 을 실행합니다.
2. 폴더를 지웁니다.
(임시로 끄기: SteamVR 설정 > 시작/종료 > 애드온 관리 에서 wygxrcontrollerfix 끄기 후 SteamVR 재시작)

[설정]  %LOCALAPPDATA%\WYGxrControllerFix\settings.ini
SteamVR 실행 중에 저장하면 1초 안에 반영됩니다 (재시작 불필요).
  enabled=0/1            전체 끄기/켜기 (바로 비교해 볼 수 있음)
  vdFrameFix=auto        auto = 자동 판별 (권장), 1 = 항상 적용, 0 = 끄기
  linearScale            던지는 힘 배율 (기본 1.0). 너무 세게 느껴지면 0.85~0.9
  angularScale           손목 회전 배율 (기본 1.0)
  releaseBoost=0/1       놓는 순간 직전 최고 속도를 잠깐 유지 (기본 켜짐).
                         최고 속도보다 조금 늦게 놓아도 힘이 빠지지 않게 합니다.
                         너무 세게 느껴지면 0 으로 끄세요.
  releasePeakMs=150      놓기 직전 이 시간(ms) 안의 최고 속도를 사용
  releaseHoldMs=60       놓은 뒤 이 시간(ms) 동안 최고 속도를 유지
                         힘이 아직 빠지면 200~300 / 80~100 으로 늘려 보세요.

참고: 처음에는 던지는 힘이 이전보다 세게 느껴질 수 있습니다.

[알려진 한계]
- 컨트롤러가 헤드셋 카메라 시야를 벗어나면 위치가 멈춰 있을 수 있습니다.
  머리 뒤에서 시작하는 던지기는 그 구간이 부정확할 수 있습니다.
- 게임마다 던지기 처리 방식이 달라서, 라이트하우스 컨트롤러로도 던지기가
  어색한 게임이 있습니다 (예: Battle Sister). 비교는 Half-Life: Alyx 처럼
  던지기가 잘 만들어진 게임에서 해 주세요.

[문제 해결 / 피드백]
- 동작 확인: C:\Program Files (x86)\Steam\logs\vrserver.txt 에서 "WYGxrControllerFix" 검색
    "TARGET"                       컨트롤러에 적용 중
    "GXR detected -> fix ON"       Galaxy XR 감지, 보정 사용
    "not needed -> fix OFF"        보정이 필요 없는 기기, 보정 안 함
- 피드백 주실 때 위 로그 줄과 게임 이름, 느낌(방향/세기/떨림)을 알려 주세요.
- 백신이 경고할 수 있습니다: SteamVR 안에서 컨트롤러 데이터를 보정하는
  방식이라 그렇습니다. OpenVR Space Calibrator와 같은 방식입니다.

----------------------------------------------------------------
English (short)
Tunes Samsung Galaxy XR controllers for SteamVR games through Virtual Desktop:
throws follow your hand motion in direction and strength. No added latency or jitter.
Auto-detection: the fix turns itself off on devices that don't need it
(Quest via VD, Steam Link). Lighthouse controllers are not touched.
Install: close SteamVR, extract to a permanent folder, run install.bat.
Uninstall: close SteamVR, run uninstall.bat, delete the folder.
Settings: %LOCALAPPDATA%\WYGxrControllerFix\settings.ini (live reload)
  vdFrameFix=auto|1|0, linearScale (default 1.0),
  releaseBoost=0|1 (default 1: holds the peak speed of the last 150 ms for 60 ms
  after grip release; tune with releasePeakMs / releaseHoldMs).

License: MIT (LICENSE.txt)
Third-party: MinHook (BSD 2-Clause), OpenVR headers (BSD 3-Clause) - see licenses\
