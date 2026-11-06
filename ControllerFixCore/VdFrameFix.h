#pragma once

#include <deque>

#include "VelocityEstimator.h"

namespace controllerfix {

// Galaxy XR 컨트롤러 속도 프레임 보정.
//   v_world = R · Qᵀ · v_reported   (R = 포즈 회전, Q = X축 -pitch 회전)
// 수치 미분이 아니라 들어온 속도를 그대로 변환하므로 지연과 떨림이 없다.
// DriverPose 단계(공개 OpenVR 인터페이스)에서만 적용한다.
// Auto: 움직임으로 보정이 필요한 기기인지 판별해서 필요할 때만 적용
enum class VdFixMode { Off, On, Auto };

struct VdFrameFixConfig {
    static constexpr double DEFAULT_PITCH_DEG = 15.0;  // 실측 X 13~16° (2026-10-24, 11-03 공통). 세션마다 다른 값은 autoCalibrate 가 맞춘다

    VdFixMode mode = VdFixMode::Auto;
    double pitchDeg = DEFAULT_PITCH_DEG;
    double angularPitchDeg = DEFAULT_PITCH_DEG;  // 각속도 전용
    bool undoLagFrame = true;   // 속도 변환에 VD 외삽 전 회전을 쓴다 (0 = v0.7.0 까지처럼 DriverPose 회전)
    bool autoCalibrate = true;  // 손 움직임으로 세션마다 3축 틀어짐을 맞춘다 (맞추기 전에는 위 pitch)
};

// 실제 속도(포즈 변화로 추정)와 비교해서 원래 속도와 보정 속도 중 어느 쪽이 맞는지 투표로 판별한다.
// 빠른 동작 몇 번이면 갈린다.
class VdFrameDetector {
public:
    enum class Verdict { Undecided, Gxr, Normal };

    static constexpr double MIN_SPEED = 1.0;          // m/s, 이보다 느린 구간은 방향 추정이 부정확해 제외
    static constexpr double AMBIGUOUS_DEG = 10.0;     // 두 오차 차이가 이보다 작으면 기권
    static constexpr size_t WINDOW_VOTES = 120;
    static constexpr size_t MIN_VOTES = 40;
    static constexpr double DECIDE_RATIO = 0.7;       // 첫 판정: 이 비율 이상이면 판정, 사이 값은 판정 보류
    static constexpr double SWITCH_RATIO = 0.9;       // 판정 뒤에 뒤집으려면 창 전체에서 이 비율 이상이어야 함

    // measured: 포즈로 추정한 실제 선속도, raw: 원래 값, fixed: 보정 값 (모두 world)
    void addVote(const Vec3& measured, const Vec3& raw, const Vec3& fixed);
    Verdict verdict() const { return m_verdict; }
    size_t votes() const { return m_votes.size(); }
    double fixRatio() const;

private:
    std::deque<bool> m_votes;  // true = 보정 쪽이 맞음
    Verdict m_verdict = Verdict::Undecided;
};

// X축 pitch 회전 (B 의 기본값)
Quat vdPitchFrame(double pitchDeg);

// v_world = R · B · v_reported
Vec3 vdLocalToWorld(const Quat& poseRotation, const Vec3& reported, const Quat& frame);

// poseRotation: 원본 포즈 회전 (DriverPose_t::qRotation)
Vec3 vdLocalToWorld(const Quat& poseRotation, const Vec3& reported, double pitchDeg);

// DriverPose::vecAngularVelocity(local) → world. q 로 world 로 돌린 뒤 보정한다.
Vec3 vdDriverAngularToWorld(const Quat& poseRotation, const Vec3& driverAngular, double pitchDeg);

}  // namespace controllerfix
