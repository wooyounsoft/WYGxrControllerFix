#include "Settings.h"

#include <fstream>
#include <sstream>

namespace controllerfix {

namespace {

constexpr double MS_PER_SEC = 1000.0;

const char* DEFAULT_INI =
    "; WYGxrControllerFix settings - 저장하면 1초 안에 반영됩니다\n"
    "enabled=1\n"
    "replaceLinear=1\n"
    "replaceAngular=1\n"
    "; Galaxy XR 던지기 보정. auto = 필요한 기기인지 자동 판별해서 그때만 적용 (권장)\n"
    ";   1 = 항상 적용, 0 = 끄고 아래 추정/블렌딩 방식 사용\n"
    "vdFrameFix=auto\n"
    "; 기본 틀어짐 각도 (자동 보정 전, 또는 자동 보정을 끈 경우)\n"
    "vdFramePitchDeg=15\n"
    "; 각속도 전용 pitch\n"
    "vdAngularPitchDeg=15\n"
    "; 1 = 손 움직임으로 세션마다 틀어짐을 자동으로 맞춤 (권장), 0 = 위 고정 각도만 사용\n"
    "vdAutoCalibrate=1\n"
    "; 1 = 속도 변환에 VD 외삽 전 손 회전을 사용 (권장), 0 = 이전 방식\n"
    "vdFrameUndoLag=1\n"
    "; 진단: 컨트롤러 원본 데이터를 captures 폴더에 CSV로 기록\n"
    "vdDump=0\n"
    "; 회귀에 쓰는 과거 구간 (ms)\n"
    "windowMs=70\n"
    "linearScale=1.0\n"
    "angularScale=1.0\n"
    "; world | local\n"
    "angularFrame=world\n"
    "; 이보다 빠른 위치 점프(m/s)는 재추적으로 보고 기록 리셋\n"
    "maxStepSpeed=40\n"
    "; 새 포즈가 이 시간(ms) 동안 없으면 추적 끊김으로 판단 (카메라 밖에서는 위치가 멈출 수 있음)\n"
    "freezeMs=100\n"
    "; 회귀선에서 이 거리(m) 이상 벗어난 샘플은 순간 튐으로 제외\n"
    "outlierResidual=0.03\n"
    "; 대상 컨트롤러 모델명 포함 문자열\n"
    "modelMatch=Quest\n"
    "; 헤드셋 모델명에 이 문자열이 있을 때만 동작 (비우면 모든 헤드셋)\n"
    "; VD는 Galaxy XR도 'Meta Quest 3'로 보고하므로 보통은 비워 둡니다\n"
    "hmdMatch=\n"
    "; 시리얼에 이 문자열이 있는 컨트롤러는 건드리지 않음 (기본: Steam Link 컨트롤러)\n"
    "ignoreSerial=VRLINK\n"
    "; OpenXR 레이어 (VDXR 게임): 켜기/끄기, 대상 런타임 이름 포함 문자열\n"
    "openxrEnabled=1\n"
    "runtimeMatch=VirtualDesktop\n"
    "; 진단: 런타임 원래 포즈/속도를 %LOCALAPPDATA%\\WYGxrControllerFix\\captures 에 CSV로 기록\n"
    "openxrCapture=0\n"
    "; 속도 블렌딩: low 이하 = 런타임 원래값(떨림 없음), high 이상 = 추정값(던지기)\n"
    "blend=1\n"
    "blendLinearLow=0.5\n"
    "blendLinearHigh=1.5\n"
    "blendAngularLow=2.0\n"
    "blendAngularHigh=6.0\n"
    "; 그립 해제 순간 보정: 그립 판정이 늦으면, 놓기 직전 손이 아직 빨랐던 마지막 순간의 속도를\n"
    "; releaseHoldMs 동안 내보냄 (세기와 방향 모두 그 순간).\n"
    "releaseBoost=1\n"
    "releasePeakMs=150\n"
    "releaseHoldMs=60\n"
    "; '아직 빨랐던' 기준: 최고 속도의 이 비율 이상\n"
    "releaseFastRatio=0.85\n"
    "; 놓는 순간보다 이 시간(ms) 앞의 속도를 기준으로 사용 (빠르게 던질 때 아래로 꺾이는 것 방지, 0 = 놓는 순간)\n"
    "; SteamVR 게임용\n"
    "releaseLookbackMs=20\n"
    "; VDXR 게임용 (일찍 놓을 때 방향이 틀어져 0 이 기본)\n"
    "openxrReleaseLookbackMs=0\n"
    "; 그 순간이 놓기보다 이 시간(ms) 이상 앞서면 보정 안 함 (멈췄다가 내려놓을 때 튀어 나가지 않게)\n"
    "releaseMaxPeakAgeMs=80\n"
    "releaseMinSpeed=1.0\n"
    "; 그 순간 앞뒤 이 시간(ms)의 평균 방향을 사용 (한 프레임 방향 튐 방지, 0 = 끔). 최대값\n"
    "releaseDirWindowMs=30\n"
    "; 빠를수록 구간을 줄임: 구간 동안 곡선이 이 각도만 돌도록 (4 m/s 30 ms, 8 m/s 16 ms). 0 = 고정\n"
    "releaseDirArcDeg=6\n"
    "; 보정 속도 상한: 놓는 순간 속도의 배수 (0 = 제한 없음)\n"
    "releaseMaxRatio=0\n"
    "; 물건을 잡는 버튼: grip | trigger\n"
    "releaseButton=grip\n"
    "gripPress=0.7\n"
    "gripRelease=0.4\n"
    "; 순간 튐 방지: 이보다 빠른 값(m/s)은 추적 오류로 보고 직전 값을 최대 spikeHoldMs 동안 유지\n"
    "spikeGuard=1\n"
    "spikeMaxSpeed=25\n"
    "spikeHoldMs=150\n"
    "; SteamVR 손 위치 정확도\n"
    "poseLagFix=1\n"
    "poseLagMs=37\n"
    "; 실험용: 손 포즈를 이만큼(ms) 앞당김 (0 = 끔)\n"
    "poseLeadMs=0\n"
    "; 실험용: 각속도 출력 기준 world (기본) / local\n"
    "angularOutput=world\n"
    "; 진동 강화: 세기 배율, 최소 길이(ms)\n"
    "hapticBoost=0\n"
    "hapticGain=1.5\n"
    "hapticMinMs=20\n"
    "; 컨트롤러 각도 조정 (도, + = 앞쪽을 위로)\n"
    "angleOffset=0\n"
    "angleOffsetDeg=0\n"
    "; 스틱: 데드존(0~0.5), 감도 곡선(1 = 그대로, 클수록 가운데 근처가 덜 민감)\n"
    "stickTuning=0\n"
    "stickDeadzone=0.10\n"
    "stickCurve=1.0\n"
    "; D-pad: 왼손을 얼굴 가까이 댄 채 왼쪽 스틱을 밀면 D-pad (게임의 D-pad 입력 또는 가상 Xbox 패드, ViGEmBus 필요)\n"
    "dpadEmulation=1\n"
    "; gesture = 왼손을 얼굴 가까이, stick = 오른쪽 스틱을 누른 채\n"
    "dpadTrigger=gesture\n"
    "; D-pad 에 들어갈 때 왼손 진동\n"
    "dpadHaptic=1\n"
    "dpadGestureDist=0.22\n"
    "dpadThreshold=0.5\n";

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

}  // namespace

void SettingsFile::ensureExists() const {
    std::error_code ec;
    if (std::filesystem::exists(m_path, ec)) return;
    std::filesystem::create_directories(m_path.parent_path(), ec);
    std::ofstream(m_path) << DEFAULT_INI;
}

bool SettingsFile::reloadIfChanged(Settings& settings) {
    std::error_code ec;
    auto writeTime = std::filesystem::last_write_time(m_path, ec);
    if (ec || writeTime == m_lastWrite) return false;
    std::ifstream in(m_path);
    if (!in) return false;
    m_lastWrite = writeTime;
    Settings parsed;
    parse(in, parsed);
    settings = parsed;
    return true;
}

void SettingsFile::parse(std::istream& in, Settings& s) {
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        try {
            if (key == "enabled") s.enabled = std::stoi(val) != 0;
            else if (key == "replaceLinear") s.replaceLinear = std::stoi(val) != 0;
            else if (key == "replaceAngular") s.replaceAngular = std::stoi(val) != 0;
            else if (key == "windowMs") s.estimator.windowSec = std::stod(val) / MS_PER_SEC;
            else if (key == "linearScale") s.estimator.linearScale = std::stod(val);
            else if (key == "angularScale") s.estimator.angularScale = std::stod(val);
            else if (key == "angularFrame") s.estimator.angularFrame = (val == "local") ? AngularFrame::Local : AngularFrame::World;
            else if (key == "maxStepSpeed") s.estimator.maxStepSpeed = std::stod(val);
            else if (key == "freezeMs") s.estimator.freezeSec = std::stod(val) / MS_PER_SEC;
            else if (key == "outlierResidual") s.estimator.outlierResidual = std::stod(val);
            else if (key == "modelMatch") s.modelMatch = val;
            else if (key == "hmdMatch") s.hmdMatch = val;
            else if (key == "ignoreSerial") s.ignoreSerial = val;
            else if (key == "runtimeMatch") s.runtimeMatch = val;
            else if (key == "openxrEnabled") s.openxrEnabled = std::stoi(val) != 0;
            else if (key == "vdDump") s.vdDump = std::stoi(val) != 0;
            else if (key == "openxrCapture") s.openxrCapture = std::stoi(val) != 0;
            else if (key == "blend") s.blendEnabled = std::stoi(val) != 0;
            else if (key == "blendLinearLow") s.blend.linearLow = std::stod(val);
            else if (key == "blendLinearHigh") s.blend.linearHigh = std::stod(val);
            else if (key == "blendAngularLow") s.blend.angularLow = std::stod(val);
            else if (key == "blendAngularHigh") s.blend.angularHigh = std::stod(val);
            else if (key == "releaseBoost") s.releaseBoost.enabled = std::stoi(val) != 0;
            else if (key == "releasePeakMs") s.releaseBoost.peakWindowSec = std::stod(val) / MS_PER_SEC;
            else if (key == "releaseHoldMs") s.releaseBoost.holdSec = std::stod(val) / MS_PER_SEC;
            else if (key == "releaseMaxPeakAgeMs") s.releaseBoost.maxPeakAgeSec = std::stod(val) / MS_PER_SEC;
            else if (key == "releaseMinSpeed") s.releaseBoost.minSpeed = std::stod(val);
            else if (key == "gripPress") s.releaseBoost.gripPress = std::stod(val);
            else if (key == "vdFrameFix") s.vdFix.mode = val == "auto" ? VdFixMode::Auto : (std::stoi(val) != 0 ? VdFixMode::On : VdFixMode::Off);
            else if (key == "vdFramePitchDeg") s.vdFix.pitchDeg = std::stod(val);
            else if (key == "gripRelease") s.releaseBoost.gripRelease = std::stod(val);
            else if (key == "vdAngularPitchDeg") s.vdFix.angularPitchDeg = std::stod(val);
            else if (key == "vdAutoCalibrate") s.vdFix.autoCalibrate = std::stoi(val) != 0;
            else if (key == "vdFrameUndoLag") s.vdFix.undoLagFrame = std::stoi(val) != 0;
            else if (key == "releaseDirWindowMs") s.releaseBoost.directionWindowSec = std::stod(val) / MS_PER_SEC;
            else if (key == "releaseFastRatio") s.releaseBoost.fastRatio = std::stod(val);
            else if (key == "releaseLookbackMs") s.releaseBoost.minLookbackSec = std::stod(val) / MS_PER_SEC;
            else if (key == "openxrReleaseLookbackMs") s.openxrReleaseLookbackSec = std::stod(val) / MS_PER_SEC;
            else if (key == "releaseDirArcDeg") s.releaseBoost.directionArcDeg = std::stod(val);
            else if (key == "releaseMaxRatio") s.releaseBoost.maxRatio = std::stod(val);
            else if (key == "releaseButton") s.releaseBoost.button = val == "trigger" ? ReleaseButton::Trigger : ReleaseButton::Grip;
            else if (key == "dpadEmulation") s.dpad.enabled = std::stoi(val) != 0;
            else if (key == "dpadThreshold") s.dpad.threshold = std::stod(val);
            else if (key == "dpadTrigger") s.dpad.gesture = val != "stick";
            else if (key == "dpadHaptic") s.dpad.haptic = std::stoi(val) != 0;
            else if (key == "dpadGestureDist") s.dpad.gestureDistance = std::stod(val);
            else if (key == "spikeGuard") s.spikeGuard.enabled = std::stoi(val) != 0;
            else if (key == "spikeMaxSpeed") s.spikeGuard.maxSpeed = std::stod(val);
            else if (key == "spikeHoldMs") s.spikeGuard.maxHoldSec = std::stod(val) / MS_PER_SEC;
            else if (key == "poseLagFix") s.poseLagFix = std::stoi(val) != 0;
            else if (key == "poseLagMs") s.poseLagSec = std::stod(val) / MS_PER_SEC;
            else if (key == "poseLeadMs") s.poseLeadSec = std::stod(val) / MS_PER_SEC;
            else if (key == "angularOutput") s.angularOutputWorld = val != "local";
            else if (key == "hapticBoost") s.haptic.enabled = std::stoi(val) != 0;
            else if (key == "hapticGain") s.haptic.gain = std::stod(val);
            else if (key == "hapticMinMs") s.haptic.minDurationSec = std::stod(val) / MS_PER_SEC;
            else if (key == "angleOffset") s.angleOffset.enabled = std::stoi(val) != 0;
            else if (key == "angleOffsetDeg") s.angleOffset.pitchDeg = std::stod(val);
            else if (key == "stickTuning") s.stick.enabled = std::stoi(val) != 0;
            else if (key == "stickDeadzone") s.stick.deadzone = std::stod(val);
            else if (key == "stickCurve") s.stick.curve = std::stod(val);
        } catch (...) {
            // 잘못된 값은 기본값 유지
        }
    }
}

}  // namespace controllerfix
