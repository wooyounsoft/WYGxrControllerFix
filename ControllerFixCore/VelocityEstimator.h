#pragma once

#include <deque>

namespace controllerfix {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
};

struct PoseSample {
    double timeSec = 0;  // 단조 증가 시간 (초)
    Vec3 position;       // m, 월드(드라이버) 좌표계
    Quat rotation;
};

enum class AngularFrame { World, Local };

struct EstimatorConfig {
    static constexpr double DEFAULT_WINDOW_SEC = 0.070;
    static constexpr double DEFAULT_MAX_STEP_SPEED = 40.0;      // m/s, 이보다 큰 점프는 재추적으로 보고 기록 리셋
    static constexpr double DEFAULT_FREEZE_SEC = 0.100;         // 새 포즈가 이만큼 없으면 추적 끊김
    static constexpr double DEFAULT_OUTLIER_RESIDUAL_M = 0.030; // 회귀 잔차가 이보다 큰 샘플은 순간 튐으로 제외

    double windowSec = DEFAULT_WINDOW_SEC;
    double maxStepSpeed = DEFAULT_MAX_STEP_SPEED;
    double freezeSec = DEFAULT_FREEZE_SEC;
    double outlierResidual = DEFAULT_OUTLIER_RESIDUAL_M;
    double linearScale = 1.0;
    double angularScale = 1.0;
    AngularFrame angularFrame = AngularFrame::World;
};

struct VelocityEstimate {
    Vec3 linear;   // m/s
    Vec3 angular;  // rad/s, axis-angle
};

// 느릴 때는 런타임 원래 값(부드러움, 떨림 없음), 빠를 때는 추정값(던지기 정확도)을 쓴다.
// 속도가 low 이하면 0, high 이상이면 1, 사이는 smoothstep.
struct BlendConfig {
    static constexpr double DEFAULT_LINEAR_LOW = 0.5;    // m/s
    static constexpr double DEFAULT_LINEAR_HIGH = 1.5;
    static constexpr double DEFAULT_ANGULAR_LOW = 2.0;   // rad/s
    static constexpr double DEFAULT_ANGULAR_HIGH = 6.0;

    double linearLow = DEFAULT_LINEAR_LOW;
    double linearHigh = DEFAULT_LINEAR_HIGH;
    double angularLow = DEFAULT_ANGULAR_LOW;
    double angularHigh = DEFAULT_ANGULAR_HIGH;
};

struct BlendWeights {
    double linear = 0;   // 0 = 원래값, 1 = 추정값
    double angular = 0;
};

BlendWeights computeBlendWeights(const VelocityEstimate& estimated, const BlendConfig& config);
Vec3 lerp(const Vec3& from, const Vec3& to, double weight);

// 최근 포즈 기록으로 선속도/각속도를 추정한다.
// 선속도: 최근 windowSec 구간 위치에 2차 다항식 최소자승 → 최신 샘플 시점의 미분값
//         잔차가 큰 샘플(순간 튐)은 제외하고 다시 맞춘다.
// 각속도: 각 샘플에서 최신 샘플까지의 회전 벡터를 원점 통과 직선으로 회귀
class VelocityEstimator {
public:
    static constexpr size_t MIN_SAMPLES = 3;

    void setConfig(const EstimatorConfig& config) { m_config = config; }
    const EstimatorConfig& config() const { return m_config; }

    // 매 포즈 콜백마다 호출. 중복(동일 포즈) 샘플은 기록하지 않는다.
    // 추적이 끊겼다 돌아오거나(freezeSec 이상 공백) 비정상 점프면 기록을 비운다.
    void addSample(const PoseSample& sample);
    // 샘플 부족 또는 추적 끊김(위치 고정) 중이면 false → 런타임 원래값 사용
    bool estimate(VelocityEstimate& out) const;
    void reset() { m_samples.clear(); }
    size_t sampleCount() const { return m_samples.size(); }

private:
    Vec3 estimateLinear() const;
    Vec3 estimateAngular() const;

    EstimatorConfig m_config;
    std::deque<PoseSample> m_samples;
    double m_lastInputTimeSec = 0;
};

}  // namespace controllerfix
