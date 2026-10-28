#pragma once

#include <array>
#include <chrono>
#include <map>
#include <mutex>

#include <openvr_driver.h>

#include "ControllerTuning.h"
#include "ReleaseBoost.h"
#include "Settings.h"
#include "VdFrameFix.h"
#include "VelocityEstimator.h"

// VD 컨트롤러 포즈 보정: 속도 프레임, 손 위치 정확도, 각도 조정.
// 입력 훅에서 그립 해제(놓을 때 속도 보정)와 스틱 조정, 진동 이벤트 조정도 처리한다.
class PoseFixer {
public:
    static PoseFixer& I();

    void initSettings();
    void tick();  // RunFrame에서 호출: 설정 재로드, 통계 로그
    void process(uint32_t deviceIndex, vr::DriverPose_t& pose);

    // IVRDriverInput 훅에서 호출
    void onComponentCreated(vr::PropertyContainerHandle_t container, const char* name, vr::VRInputComponentHandle_t handle, bool scalar);
    // 스틱 축이면 조정한 값을 돌려준다 (아니면 그대로)
    float onScalarUpdated(vr::VRInputComponentHandle_t handle, float value, double timeOffset);
    void onBooleanUpdated(vr::VRInputComponentHandle_t handle, bool value, double timeOffset);
    // IVRServerDriverHost::PollNextEvent 훅에서 호출 (진동 강화)
    void onDriverEvent(vr::VREvent_t& event);

private:
    static constexpr auto SETTINGS_POLL_INTERVAL = std::chrono::seconds(1);
    static constexpr auto STATS_LOG_INTERVAL = std::chrono::seconds(30);
    static constexpr int STICK_AXIS_COUNT = 2;

    enum class DeviceKind { Unknown, Target, Ignored };

    struct DeviceState {
        DeviceKind kind = DeviceKind::Unknown;
        controllerfix::VelocityEstimator estimator;
        controllerfix::ReleaseBoost releaseBoost;
        bool hasScalarGrip = false;  // 값(0~1) 그립이 있으면 click 은 무시
        bool frameLogged = false;    // vdFrameFix: 첫 포즈의 qDriverFromHeadRotation 로그 여부
        controllerfix::VdFrameDetector detector;  // vdFrameFix=auto: 보정 필요 여부 판별
        controllerfix::VdFrameDetector::Verdict loggedVerdict = controllerfix::VdFrameDetector::Verdict::Undecided;
        std::array<double, STICK_AXIS_COUNT> stickRaw{};  // 스틱 x, y 원래 값 (원형 데드존 계산용)
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
    void applyEstimator(DeviceState& dev, double timeSec, vr::DriverPose_t& pose);
    void applyPoseLagFix(const vr::DriverPose_t& original, vr::DriverPose_t& pose) const;
    void applyPoseLead(vr::DriverPose_t& pose) const;
    void applyReleaseBoost(DeviceState& dev, double timeSec, vr::DriverPose_t& pose);
    void applyAngleOffset(vr::DriverPose_t& pose) const;

    static constexpr const char* GRIP_VALUE_PATH = "/input/grip/value";
    static constexpr const char* GRIP_CLICK_PATH = "/input/grip/click";
    static constexpr const char* STICK_AXIS_PATHS[][STICK_AXIS_COUNT] = {
        {"/input/joystick/x", "/input/joystick/y"},
        {"/input/thumbstick/x", "/input/thumbstick/y"},
    };

    struct GripComponent {
        vr::PropertyContainerHandle_t container;
        bool scalar;
    };

    struct StickAxis {
        vr::PropertyContainerHandle_t container;
        int axis;  // 0 = x, 1 = y
    };

    std::mutex m_mutex;
    std::map<vr::VRInputComponentHandle_t, GripComponent> m_gripComponents;
    std::map<vr::VRInputComponentHandle_t, StickAxis> m_stickAxes;
    std::map<vr::PropertyContainerHandle_t, uint32_t> m_containerToDevice;
    controllerfix::SettingsFile m_settingsFile;
    controllerfix::Settings m_settings;
    // 마지막으로 대상 판별에 쓴 조건 (바뀔 때만 다시 판별)
    std::string m_classifiedModelMatch;
    std::string m_classifiedHmdMatch;
    std::string m_classifiedIgnoreSerial;
    std::array<DeviceState, vr::k_unMaxTrackedDeviceCount> m_devices;
    uint64_t m_hapticCount = 0;
    std::chrono::steady_clock::time_point m_nextSettingsPoll{};
    std::chrono::steady_clock::time_point m_nextStatsLog{};
};
