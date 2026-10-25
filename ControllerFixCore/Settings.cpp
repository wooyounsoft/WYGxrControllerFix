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
    "vdFramePitchDeg=13\n"
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
    "; 그립 해제 순간 보정: 놓기 직전 releasePeakMs 동안의 최고 속도를 releaseHoldMs 동안 내보냄\n"
    "; (최고 속도보다 늦게 놓아서 뚝 떨어지는 문제용. 너무 세면 0, 아직 힘이 빠지면 peak/hold 를 늘림)\n"
    "releaseBoost=1\n"
    "releasePeakMs=150\n"
    "releaseHoldMs=60\n"
    "releaseMinSpeed=1.0\n"
    "gripPress=0.7\n"
    "gripRelease=0.4\n";

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
            else if (key == "releaseMinSpeed") s.releaseBoost.minSpeed = std::stod(val);
            else if (key == "gripPress") s.releaseBoost.gripPress = std::stod(val);
            else if (key == "vdFrameFix") s.vdFix.mode = val == "auto" ? VdFixMode::Auto : (std::stoi(val) != 0 ? VdFixMode::On : VdFixMode::Off);
            else if (key == "vdFramePitchDeg") s.vdFix.pitchDeg = std::stod(val);
            else if (key == "gripRelease") s.releaseBoost.gripRelease = std::stod(val);
        } catch (...) {
            // 잘못된 값은 기본값 유지
        }
    }
}

}  // namespace controllerfix
