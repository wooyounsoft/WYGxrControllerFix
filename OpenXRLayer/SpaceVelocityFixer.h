#pragma once

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "ControllerTuning.h"
#include "Dispatch.h"
#include "ReleaseBoost.h"
#include "Settings.h"
#include "VdFrameFit.h"
#include "VelocityEstimator.h"

// 컨트롤러 action space 의 xrLocateSpace(s) 결과에서 속도를 보정한다.
//   vdFrameFix=auto : SteamVR 쪽과 같은 공식으로 시작해서 움직임으로 다듬고, 필요 없는 기기면 끔
//   vdFrameFix=1    : SteamVR 쪽과 같은 고정 공식
//   vdFrameFix=0    : 포즈 기록 기반 추정값
// 그립 해제 보정, 시야 밖 따라오기, 각도 조정, 스틱/진동 조정도 여기서 처리한다.
class SpaceVelocityFixer {
public:
    enum class Hand { Unknown, Left, Right };
    enum class BindingKind { Other, Grip, Stick };

    static SpaceVelocityFixer& I();

    static Hand handOf(const std::string& path);
    static BindingKind kindOf(const std::string& bindingPath);

    // 대상 런타임이면 true
    bool onInstanceCreated(const std::string& appName, const std::string& runtimeName);
    void onBinding(XrAction action, Hand hand, BindingKind kind);
    void onActionSpaceCreated(XrSpace space, XrSession session, XrAction action, Hand hand);
    void onSpaceDestroyed(XrSpace space);
    // 앱이 읽은 버튼 값 (그립 해제 감지)
    void onActionState(XrAction action, Hand hand, double value);
    // 스틱 액션이면 데드존/감도 조정
    void onStickState(XrAction action, XrVector2f& value);
    // 진동 세기/길이 조정
    void tuneHaptic(XrHapticVibration& vibration);

    // location 은 이미 런타임이 채운 값. velocity 는 앱이 요청하지 않았으면 nullptr.
    void process(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time,
                 XrSpaceLocationFlags locationFlags, XrPosef& pose, XrSpaceVelocityFlags* velocityFlags,
                 XrVector3f* linear, XrVector3f* angular);

private:
    static constexpr auto SETTINGS_POLL_INTERVAL = std::chrono::seconds(1);
    static constexpr auto STATS_LOG_INTERVAL = std::chrono::seconds(30);

    using SpacePair = std::pair<XrSpace, XrSpace>;

    struct ActionSpace {
        XrSession session = XR_NULL_HANDLE;
        XrAction action = XR_NULL_HANDLE;
        Hand hand = Hand::Unknown;  // subactionPath 로 정해진 손 (없으면 바인딩으로 판단)
    };

    struct PairState {
        controllerfix::VelocityEstimator estimator;
        controllerfix::VdFrameFit fit;
        controllerfix::VdFrameFit::Verdict loggedVerdict = controllerfix::VdFrameFit::Verdict::Undecided;
        controllerfix::ReleaseBoost releaseBoost;
        double lastSampleSec = 0;
    };

    SpaceVelocityFixer();
    void pollSettings();
    void applySettings();
    void logStats();
    void capture(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time);
    void closeCapture();
    Hand resolveHand(XrAction action, Hand hand) const;
    Hand spaceHand(XrSpace space) const;
    void logVerdict(XrSpace space, PairState& ps);
    controllerfix::Vec3 fixVelocity(PairState& ps, const controllerfix::Quat& rotation, const controllerfix::Vec3& reported) const;
    // 속도 보정. 앱에 돌려줄 값을 바꿨으면 true, fixActive = 프레임 보정이 적용됐는지
    bool fixVelocities(PairState& ps, XrSpace space, const controllerfix::PoseSample& sample, XrSpaceVelocityFlags flags,
                       controllerfix::VelocityEstimate& out, bool& fixActive);

    mutable std::mutex m_mutex;
    controllerfix::SettingsFile m_settingsFile;
    controllerfix::Settings m_settings;
    std::string m_appName;
    std::map<XrSpace, ActionSpace> m_actionSpaces;
    std::map<XrAction, std::set<Hand>> m_actionHands;
    std::map<XrAction, BindingKind> m_actionKinds;
    std::map<SpacePair, PairState> m_pairs;
    std::map<SpacePair, XrTime> m_lastCaptureTime;
    std::map<XrSpace, size_t> m_baseSpaceIndex;  // 캡처에서 기준 공간 구분용
    FILE* m_captureFile = nullptr;
    XrTime m_captureStartTime = 0;
    uint64_t m_locateCount = 0;
    uint64_t m_velocityRequestCount = 0;
    uint64_t m_replacedCount = 0;
    uint64_t m_boostCount = 0;
    uint64_t m_hapticCount = 0;
    std::chrono::steady_clock::time_point m_nextSettingsPoll{};
    std::chrono::steady_clock::time_point m_nextStatsLog{};
};
