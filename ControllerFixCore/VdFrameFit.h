#pragma once

#include <deque>

#include "VelocityEstimator.h"

namespace controllerfix {

// 속도 프레임 보정을 실제 움직임(포즈 변화)으로 다듬는다.
//   v_world = R · B · v_reported   (R = 공간 포즈 회전, B = 고정 회전)
// 처음에는 SteamVR 쪽과 같은 공식(B = X축 pitch 회전)으로 시작하고,
// 샘플이 충분히 쌓이면 직접 맞춘 B 가 더 나을 때 그것으로 바꾼다.
// 원래 값이 더 맞으면 Normal 로 판정한다 (보정이 필요 없는 기기).
class VdFrameFit {
public:
    enum class Verdict { Undecided, Fix, Normal };

    static constexpr double MIN_SPEED = 1.0;            // m/s, 이보다 느린 구간은 방향 추정이 부정확해 제외
    static constexpr size_t MAX_SAMPLES = 400;
    static constexpr size_t MIN_SAMPLES = 60;           // 판정에 필요한 샘플
    static constexpr size_t REFINE_MIN_SAMPLES = 150;   // 직접 맞춘 B 를 쓰기 시작하는 샘플 수
    static constexpr double SOLVE_INTERVAL_SEC = 1.0;
    static constexpr double MARGIN_DEG = 10.0;          // 원래 값과 보정 값의 오차 차이가 이보다 커야 판정
    static constexpr double REFINE_MARGIN_DEG = 2.0;    // 맞춘 B 가 기본 공식보다 이만큼 나아야 교체
    static constexpr double MAX_FIT_ERROR_DEG = 25.0;   // 원래 값 오차가 이보다 작아야 Normal

    explicit VdFrameFit(double priorPitchDeg = 0);
    void setPriorPitch(double pitchDeg);

    // measured: 포즈로 추정한 실제 선속도(world), reported: 런타임이 준 값
    void addSample(double timeSec, const Quat& spaceRotation, const Vec3& measured, const Vec3& reported);
    Vec3 apply(const Quat& spaceRotation, const Vec3& reported) const;
    void reset();

    Verdict verdict() const { return m_verdict; }
    double fitErrorDeg() const { return m_fitErrorDeg; }
    double rawErrorDeg() const { return m_rawErrorDeg; }
    bool refined() const { return m_refined; }  // false = 기본 공식 사용 중
    double fixAngleDeg() const;  // B 의 회전각
    Vec3 fixAxis() const;        // B 의 회전축
    size_t sampleCount() const { return m_samples.size(); }

private:
    struct Sample {
        Vec3 localMeasured;  // Rᵀ · measured (단위 벡터)
        Vec3 reported;       // 단위 벡터
        double rawErrorDeg;
    };

    void solve();

    std::deque<Sample> m_samples;
    double m_nextSolveSec = 0;
    Verdict m_verdict = Verdict::Undecided;
    Quat m_prior;  // 기본 공식
    Quat m_fix;    // 현재 쓰는 B
    bool m_refined = false;
    double m_fitErrorDeg = 0;
    double m_rawErrorDeg = 0;
};

}  // namespace controllerfix
