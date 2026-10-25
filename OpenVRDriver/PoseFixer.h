#pragma once

#include <array>
#include <chrono>
#include <map>
#include <mutex>

#include <openvr_driver.h>

#include "ReleaseBoost.h"
#include "Settings.h"
#include "VdFrameFix.h"
#include "VelocityEstimator.h"

// VD 컨트롤러 포즈의 속도를 포즈 기록 기반 추정값으로 교체한다.
class PoseFixer {
public:
    static PoseFixer& I();

    void initSettings();
    void tick();  // RunFrame에서 호출: 설정 재로드, 통계 로그
    void process(uint32_t deviceIndex, vr::DriverPose_t& pose);

    // IVRDriverInput 훅에서 호출 (그립 해제 감지용)
    void onComponentCreated(vr::PropertyContainerHandle_t container, const char* name, vr::VRInputComponentHandle_t handle, bool scalar);
    void onScalarUpdated(vr::VRInputComponentHandle_t handle, float value, double timeOffset);
    void onBooleanUpdated(vr::VRInputComponentHandle_t handle, bool value, double timeOffset);

private:
    static constexpr auto SETTINGS_POLL_INTERVAL = std::chrono::seconds(1);
    static constexpr auto STATS_LOG_INTERVAL = std::chrono::seconds(30);

    enum class DeviceKind { Unknown, Target, Ignored };

    struct DeviceState {
        DeviceKind kind = DeviceKind::Unknown;
        controllerfix::VelocityEstimator estimator;
        controllerfix::ReleaseBoost releaseBoost;
        bool hasScalarGrip = false;  // 값(0~1) 그립이 있으면 click 은 무시
        bool frameLogged = false;    // vdFrameFix: 첫 포즈의 qDriverFromHeadRotation 로그 여부
        controllerfix::VdFrameDetector detector;  // vdFrameFix=auto: 보정 필요 여부 판별
        controllerfix::VdFrameDetector::Verdict loggedVerdict = controllerfix::VdFrameDetector::Verdict::Undecided;
        uint64_t replacedCount = 0;
        uint64_t boostCount = 0;
    };

    PoseFixer();
    DeviceKind classify(uint32_t deviceIndex) const;
    void applySettings();
    int deviceForContainer(vr::PropertyContainerHandle_t container);
    void onGripValue(vr::VRInputComponentHandle_t handle, double value, double timeOffset);
    void applyVdFrameFix(DeviceState& dev, uint32_t deviceIndex, vr::DriverPose_t& pose);
    bool useVdFrameFix(DeviceState& dev, uint32_t deviceIndex, const vr::DriverPose_t& original, const vr::DriverPose_t& fixed);

    static constexpr const char* GRIP_VALUE_PATH = "/input/grip/value";
    static constexpr const char* GRIP_CLICK_PATH = "/input/grip/click";

    struct GripComponent {
        vr::PropertyContainerHandle_t container;
        bool scalar;
    };

    std::mutex m_mutex;
    std::map<vr::VRInputComponentHandle_t, GripComponent> m_gripComponents;
    std::map<vr::PropertyContainerHandle_t, uint32_t> m_containerToDevice;
    controllerfix::SettingsFile m_settingsFile;
    controllerfix::Settings m_settings;
    std::array<DeviceState, vr::k_unMaxTrackedDeviceCount> m_devices;
    std::chrono::steady_clock::time_point m_nextSettingsPoll{};
    std::chrono::steady_clock::time_point m_nextStatsLog{};
};
