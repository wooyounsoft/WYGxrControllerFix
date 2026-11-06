#pragma once

#include <array>
#include <cstdint>
#include <deque>

#include "VelocityEstimator.h"

namespace controllerfix {

// Galaxy XR 속도 프레임(B)을 세션마다 스스로 맞춘다.
//   v_world = R · B · v_reported
// GXR 이 보내는 속도의 틀어짐은 접속할 때마다 달라진다 (2026-10-24 X 15°, 11-03 X 15° + Y 6°, 11-02 76~87°).
// 평소 손 움직임에서 포즈 변화(실제 회전/이동)와 보고된 속도를 비교해 B 를 구한다. 던질 필요는 없다.
// 회전(각속도)과 이동(선속도) 둘 다 쓴다: 둘의 B 가 같다 (실측 차이 1° 안팎).
class VdFrameCalibrator {
public:
    static constexpr int MAX_DEVICES = 64;
    static constexpr double MIN_ANGULAR_SPEED = 1.5;    // rad/s, 이보다 느린 회전은 방향이 부정확
    static constexpr double MIN_LINEAR_SPEED = 0.5;     // m/s
    static constexpr double MAX_LINEAR_SPEED = 2.5;     // m/s, 빠른 이동은 지연 영향이 커서 제외
    static constexpr double MIN_SPAN_SEC = 0.015;       // 앞뒤 샘플 간격 (중앙 차분)
    static constexpr double MAX_SPAN_SEC = 0.05;
    static constexpr size_t MAX_PAIRS = 600;            // 약 20초 분량
    static constexpr size_t MIN_PAIRS = 120;            // 첫 보정에 필요한 쌍 (실측 약 3초, 60 이면 적은 데이터로 6° 틀린 적 있음)
    static constexpr double SOLVE_INTERVAL_SEC = 0.5;
    static constexpr double SMOOTHING = 0.3;            // 첫 보정 뒤 새 값으로 옮겨 가는 비율
    static constexpr double MAX_FIT_ERROR_DEG = 15.0;   // 맞춘 뒤 오차가 이보다 크면 데이터가 이상하다고 보고 버림
    static constexpr size_t RECENT_PAIRS = 60;
    static constexpr double RESET_ERROR_DEG = 25.0;     // 최근 쌍이 현재 B 로 이만큼 틀리면 처음부터 다시 맞춤
    static constexpr size_t MIN_LINEAR_PAIRS = 60;      // 선속도만으로 맞출 때 필요한 쌍
    // 회전 + 이동을 한 B 로 맞출 때 허용 오차. 그립 위치는 2~5°, 각속도 규칙이 다른 조준 위치는 13~28° (Titanfall 2026-11-03: 14° 를 받아들여 잠깐 84° 로 틀림)
    static constexpr double MAX_COMBINED_ERROR_DEG = 8.0;

    // prior: 맞추기 전에 쓰는 기본 B
    void setPrior(const Quat& prior);
    void reset();

    // VD 가 손 포즈를 보고 속도로 뒤로 외삽한 시간 (poseLagMs). 실제 움직임은 이걸 되돌린 포즈로 계산한다.
    void setUndoLag(double sec) { m_undoLagSec = sec; }

    // DriverPose 한 프레임 (VD 원본, 보정 전).
    // rotation/position: 포즈, reportedLinear: 보고 선속도, reportedAngular: 보고 각속도를 rotation 으로 돌린 값
    void addSample(int device, double timeSec, const Quat& rotation, const Vec3& position, const Vec3& reportedLinear,
                   const Vec3& reportedAngular);

    const Quat& frame() const { return m_frame; }
    bool calibrated() const { return m_calibrated; }
    // false = 각속도가 같은 B 로 맞지 않아 선속도만으로 맞춤 (VDXR 조준 위치: 런타임이 그립 각속도를 그대로 줌)
    bool angularReliable() const { return m_angularReliable; }
    double fitErrorDeg() const { return m_fitErrorDeg; }
    size_t pairCount() const { return m_pairs.size(); }
    uint64_t solveCount() const { return m_solveCount; }

private:
    struct Sample {  // truth = VD 외삽을 되돌린 포즈
        double timeSec;
        Quat truthRotation;
        Vec3 truthPosition;
        Quat frameRotation;
        Vec3 linear;
        Vec3 angular;
    };
    struct Pair {
        Vec3 local;     // Rᵀ · 실제 속도 (단위 벡터)
        Vec3 reported;  // 보고 속도 원래 값 (world 라벨이지만 실제로는 틀어진 로컬 값, 단위 벡터)
        bool angular;
    };

    void addPairs(const Sample& prev, const Sample& next);
    void solve();
    double medianError(const Quat& b, size_t from, bool includeAngular) const;
    bool fit(bool includeAngular, Quat& fitted, double& error) const;

    std::array<std::deque<Sample>, MAX_DEVICES> m_history;
    std::deque<Pair> m_pairs;
    Quat m_prior;
    Quat m_frame;
    bool m_calibrated = false;
    bool m_angularReliable = true;
    double m_fitErrorDeg = 0;
    double m_nextSolveSec = 0;
    double m_undoLagSec = 0;
    uint64_t m_solveCount = 0;
};

}  // namespace controllerfix
