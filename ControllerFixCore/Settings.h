#pragma once

#include <filesystem>
#include <string>

#include "ControllerTuning.h"
#include "ReleaseBoost.h"
#include "VdFrameFix.h"
#include "VelocityEstimator.h"

namespace controllerfix {

// settings.ini (key=value) — 실행 중 파일이 바뀌면 다시 읽는다.
struct Settings {
    bool enabled = true;
    bool replaceLinear = true;
    bool replaceAngular = true;
    std::string modelMatch = "Quest";    // 대상 컨트롤러 모델명에 포함된 문자열 (VD는 Quest 컨트롤러로 위장)
    // 헤드셋 모델명에 포함돼야 동작 (빈 값 = 모든 헤드셋).
    // VD는 Galaxy XR도 "Meta Quest 3"로 보고하므로 기본값은 비워 둔다 (GalaxyXRNative 드라이버가 있을 때만 "Galaxy XR"로 보임).
    std::string hmdMatch;
    // 시리얼에 포함되면 대상에서 제외 (빈 값 = 제외 없음). Steam Link 은 Quest 컨트롤러로 보고하지만 시리얼이 "VRLINK..."
    std::string ignoreSerial = "VRLINK";
    std::string runtimeMatch = "VirtualDesktop";  // OpenXR 레이어: 런타임 이름에 포함돼야 동작 (빈 값 = 모든 런타임)
    bool openxrEnabled = true;
    bool vdDump = false;  // driver_oculus DriverPose 와 VD 공유 메모리 원본을 CSV 로 기록 (진단용)
    bool openxrCapture = false;  // 런타임 원래 포즈/속도를 CSV로 기록 (진단용)
    bool blendEnabled = true;
    EstimatorConfig estimator;
    BlendConfig blend;
    ReleaseBoostConfig releaseBoost;
    VdFrameFixConfig vdFix;  // 켜면 추정값 대신 VD 속도를 정확히 역변환해서 사용
    static constexpr double DEFAULT_POSE_LAG_SEC = 0.037;  // 실측 35~39 ms
    bool poseLagFix = true;  // SteamVR 손 위치 정확도 (보정 속도로 위치/회전 다시 맞춤)
    double poseLagSec = DEFAULT_POSE_LAG_SEC;
    double poseLeadSec = 0;  // 보정 속도로 손 포즈를 앞당김 (0 = 끔)
    // VD 보정 시 각속도 출력: true = world (v0.4.0, Alyx 체감 정상), false = qRotation 기준 로컬 (v0.5.0)
    bool angularOutputWorld = true;
    HapticConfig haptic;
    AngleOffsetConfig angleOffset;
    StickConfig stick;
};

class SettingsFile {
public:
    explicit SettingsFile(std::filesystem::path path) : m_path(std::move(path)) {}

    // 파일이 없으면 기본값으로 생성한다.
    void ensureExists() const;
    // 변경됐으면 다시 읽고 true.
    bool reloadIfChanged(Settings& settings);
    const std::filesystem::path& path() const { return m_path; }

private:
    static void parse(std::istream& in, Settings& settings);

    std::filesystem::path m_path;
    std::filesystem::file_time_type m_lastWrite{};
};

}  // namespace controllerfix
