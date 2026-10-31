#pragma once

#include <deque>

#include "VelocityEstimator.h"

namespace controllerfix {

enum class ReleaseButton { Grip, Trigger };

struct ReleaseBoostConfig {
    static constexpr double DEFAULT_PEAK_WINDOW_SEC = 0.150;  // 그립 해제 직전 이 구간의 최고 속도를 사용
    static constexpr double DEFAULT_HOLD_SEC = 0.060;         // 해제 후 이 시간 동안 최고 속도를 유지
    static constexpr double DEFAULT_MIN_SPEED = 1.0;          // m/s, 최고 속도가 이보다 느리면 던지기가 아님
    // 최고 속도가 해제보다 이만큼 이상 앞서면 던지기가 아님 (멈췄다가 내려놓기).
    // 실측 (Alyx, 2026-10-28): 던지기 11~67 ms, 내려놓기 100~145 ms
    static constexpr double DEFAULT_MAX_PEAK_AGE_SEC = 0.080;
    static constexpr double DEFAULT_GRIP_PRESS = 0.7;         // 그립 값 히스테리시스
    static constexpr double DEFAULT_GRIP_RELEASE = 0.4;
    // 최고 속도 지점 앞뒤 이 구간의 평균 방향을 쓴다 (한 프레임 방향은 튄다). 0 = 끔
    static constexpr double DEFAULT_DIRECTION_WINDOW_SEC = 0.065;

    bool enabled = true;
    double peakWindowSec = DEFAULT_PEAK_WINDOW_SEC;
    double holdSec = DEFAULT_HOLD_SEC;
    double minSpeed = DEFAULT_MIN_SPEED;
    double maxPeakAgeSec = DEFAULT_MAX_PEAK_AGE_SEC;
    double gripPress = DEFAULT_GRIP_PRESS;
    double gripRelease = DEFAULT_GRIP_RELEASE;
    double directionWindowSec = DEFAULT_DIRECTION_WINDOW_SEC;
    double maxRatio = 0;  // 보정 속도 상한 (놓는 순간 속도의 배수). 0 = 제한 없음
    ReleaseButton button = ReleaseButton::Grip;  // 게임에서 물건을 잡는 버튼
};

struct ReleaseEvent {
    double peakSpeed = 0;     // m/s
    double currentSpeed = 0;  // m/s, 해제 시점
    double peakAgeSec = 0;    // 최고 속도가 해제보다 얼마나 앞섰나
};

// 그립 해제 순간 보정.
// 사람은 휘두르는 최고 속도 지점보다 늦게 그립을 놓기 쉬워서(특히 GXR 컨트롤러),
// 해제 순간 속도만 쓰는 게임에서는 물건이 뚝 떨어진다.
// 해제가 감지되면 직전 peakWindowSec 의 최고 속도를 holdSec 동안 대신 내보낸다.
class ReleaseBoost {
public:
    void setConfig(const ReleaseBoostConfig& config) { m_config = config; }

    // 매 포즈마다 추정 속도 기록
    void addVelocity(double timeSec, const VelocityEstimate& v);
    // 그립 값 갱신 (0~1). 해제 순간이고 보정이 걸리면 true + event.
    bool updateGrip(double timeSec, double value, ReleaseEvent& event);
    // 보정 유지 중이면 true + 내보낼 속도
    bool override(double timeSec, VelocityEstimate& out) const;
    void reset();

private:
    struct Sample {
        double timeSec;
        VelocityEstimate velocity;
    };

    ReleaseBoostConfig m_config;
    std::deque<Sample> m_history;
    bool m_gripHeld = false;
    double m_holdUntilSec = 0;
    VelocityEstimate m_holdVelocity;
};

}  // namespace controllerfix
