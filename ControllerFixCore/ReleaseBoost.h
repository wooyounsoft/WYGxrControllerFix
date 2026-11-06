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
    // 사용하는 지점 앞뒤 평균 방향 구간의 최대값 (한 프레임 방향은 튄다). 0 = 끔
    // 65 ms 는 곡선 궤적의 앞쪽 방향이 섞여 VD 에서 아래/옆으로 빗나갔다 (2026-11-03) → 30 ms
    static constexpr double DEFAULT_DIRECTION_WINDOW_SEC = 0.030;
    // 실제 구간은 속도에 반비례: 구간 동안 손이 그리는 곡선이 이 각도만큼만 돌도록.
    //   window = 2 · 반경 · 각도 / 속도   (방향 회전 속도 ≈ 속도 / 팔 반경)
    // 6°, 0.6 m → 4 m/s 30 ms, 8 m/s 16 ms
    static constexpr double DEFAULT_DIRECTION_ARC_DEG = 6.0;
    static constexpr double THROW_ARC_RADIUS_M = 0.6;  // 던질 때 손이 그리는 곡선 반경 (팔 길이 정도)
    // 해제 직전 속도가 최고 속도의 이 비율 이상인 마지막 순간을 쓴다 ("아직 빨랐던 마지막 순간").
    // Steam Link 는 해제 시점 속도가 최고의 0.86 (자연스러움), VD 는 그립 판정이 늦고 들쭉날쭉해 0.42~0.80.
    static constexpr double DEFAULT_FAST_RATIO = 0.85;
    // 해제보다 이만큼 앞선 속도를 기준으로 쓴다 (제때 놓아도). VD 보고 속도는 손 포즈보다 6~10 ms 앞서고
    // Steam Link 는 16 ms 늦다 → 빠르게 던질 때 VD 만 팔로스루(아래)로 꺾인 속도가 들어갔다 (2026-11-03).
    static constexpr double DEFAULT_MIN_LOOKBACK_SEC = 0.020;

    bool enabled = true;
    double peakWindowSec = DEFAULT_PEAK_WINDOW_SEC;
    double holdSec = DEFAULT_HOLD_SEC;
    double minSpeed = DEFAULT_MIN_SPEED;
    double maxPeakAgeSec = DEFAULT_MAX_PEAK_AGE_SEC;
    double gripPress = DEFAULT_GRIP_PRESS;
    double gripRelease = DEFAULT_GRIP_RELEASE;
    double directionWindowSec = DEFAULT_DIRECTION_WINDOW_SEC;
    double fastRatio = DEFAULT_FAST_RATIO;
    double minLookbackSec = DEFAULT_MIN_LOOKBACK_SEC;
    double directionArcDeg = DEFAULT_DIRECTION_ARC_DEG;  // 0 = 속도와 무관하게 directionWindowSec 고정
    double maxRatio = 0;  // 보정 속도 상한 (놓는 순간 속도의 배수). 0 = 제한 없음
    ReleaseButton button = ReleaseButton::Grip;  // 게임에서 물건을 잡는 버튼
};

struct ReleaseEvent {
    enum class Result { None, Fired, NotThrow, OnTime, TooOld };
    Result result = Result::None;
    double peakSpeed = 0;     // m/s, 내보내는 속도 (아직 빨랐던 마지막 순간)
    double currentSpeed = 0;  // m/s, 해제 시점
    double peakAgeSec = 0;    // 그 순간이 해제보다 얼마나 앞섰나
    double maxSpeed = 0;      // m/s, 창 안의 최고 속도 (진단용)
    double maxAgeSec = 0;     // 최고 속도가 해제보다 얼마나 앞섰나 (진단용)
    size_t samples = 0;       // 창 안 샘플 수 (진단용)
    size_t outOfOrder = 0;    // 지난 해제 뒤 시간이 뒤로 간 조회 수 (진단용)
};

const char* releaseResultName(ReleaseEvent::Result r);

// 그립 해제 순간 보정.
// 사람은 휘두르는 최고 속도 지점보다 늦게 그립을 놓기 쉬워서(특히 GXR 컨트롤러),
// 해제 순간 속도만 쓰는 게임에서는 물건이 뚝 떨어진다.
// 해제가 감지되면 직전 peakWindowSec 안에서 속도가 최고의 fastRatio 이상이던 마지막 순간의 속도
// (세기와 방향 모두 그 순간)를 holdSec 동안 대신 내보낸다. 해제 시점이 이미 그만큼 빠르면 그대로 둔다.
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
    size_t m_outOfOrder = 0;
    bool m_gripHeld = false;
    double m_holdUntilSec = 0;
    VelocityEstimate m_holdVelocity;
};

}  // namespace controllerfix
