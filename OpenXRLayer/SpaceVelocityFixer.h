#pragma once

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "Dispatch.h"
#include "Settings.h"
#include "VelocityEstimator.h"

// 컨트롤러 action space 의 xrLocateSpace(s) 결과에서 속도를 포즈 기록 기반 추정값으로 교체한다.
class SpaceVelocityFixer {
public:
    static SpaceVelocityFixer& I();

    // 대상 런타임이면 true
    bool onInstanceCreated(const std::string& appName, const std::string& runtimeName);
    void onActionSpaceCreated(XrSpace space);
    void onSpaceDestroyed(XrSpace space);

    // location 은 이미 런타임이 채운 값. velocity 는 앱이 요청하지 않았으면 nullptr.
    void process(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time,
                 XrSpaceLocationFlags locationFlags, const XrPosef& pose, XrSpaceVelocityFlags* velocityFlags,
                 XrVector3f* linear, XrVector3f* angular);

private:
    static constexpr auto SETTINGS_POLL_INTERVAL = std::chrono::seconds(1);
    static constexpr auto STATS_LOG_INTERVAL = std::chrono::seconds(30);

    using SpacePair = std::pair<XrSpace, XrSpace>;

    SpaceVelocityFixer();
    void pollSettings();
    void logStats();
    void capture(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time);
    void closeCapture();

    std::mutex m_mutex;
    controllerfix::SettingsFile m_settingsFile;
    controllerfix::Settings m_settings;
    std::string m_appName;
    std::set<XrSpace> m_actionSpaces;
    std::map<SpacePair, controllerfix::VelocityEstimator> m_estimators;
    std::map<SpacePair, XrTime> m_lastCaptureTime;
    FILE* m_captureFile = nullptr;
    XrTime m_captureStartTime = 0;
    uint64_t m_locateCount = 0;
    uint64_t m_velocityRequestCount = 0;
    uint64_t m_replacedCount = 0;
    std::chrono::steady_clock::time_point m_nextSettingsPoll{};
    std::chrono::steady_clock::time_point m_nextStatsLog{};
};
