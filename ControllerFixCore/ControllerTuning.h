#pragma once

#include "VelocityEstimator.h"

namespace controllerfix {

// 사용자 취향 조정: 진동 세기, 컨트롤러 각도, 스틱 데드존/감도. 기본은 모두 꺼짐.
struct HapticConfig {
    static constexpr double DEFAULT_GAIN = 1.5;
    static constexpr double DEFAULT_MIN_DURATION_SEC = 0.020;  // 너무 짧은 진동은 잘 느껴지지 않음

    bool enabled = false;
    double gain = DEFAULT_GAIN;
    double minDurationSec = DEFAULT_MIN_DURATION_SEC;
};

struct AngleOffsetConfig {
    bool enabled = false;
    double pitchDeg = 0;  // + = 컨트롤러 앞쪽을 위로
};

struct StickConfig {
    static constexpr double DEFAULT_DEADZONE = 0.10;
    static constexpr double DEFAULT_CURVE = 1.0;  // 1 = 그대로, 1보다 크면 가운데 근처가 덜 민감

    bool enabled = false;
    double deadzone = DEFAULT_DEADZONE;
    double curve = DEFAULT_CURVE;
};

struct Vec2 {
    double x = 0, y = 0;
};

// 진동 세기(0~1)와 길이(초)를 조정. 길이가 0 이하면 런타임 최소 진동이라 그대로 둔다.
void tuneHaptic(const HapticConfig& config, double& amplitude, double& durationSec);

// 컨트롤러 기준 X축 회전 (각도 조정). 포즈 회전 오른쪽에 곱한다.
Quat angleOffsetRotation(const AngleOffsetConfig& config);

// 원형 데드존 + 감도 곡선
Vec2 shapeStick(const StickConfig& config, const Vec2& value);

// SteamVR 손 위치: VD 쪽에서 원래 속도로 lagSec 만큼 되돌린 포즈를 보정 속도로 되돌린 포즈로 바꾼다.
//   p' = p - lag · (v_fixed - v_reported)
//   R' = exp(-lag · ω_fixed) · exp(lag · ω_reported) · R      (각속도는 world)
void correctPoseLag(double lagSec, const Vec3& reportedLinear, const Vec3& fixedLinear, const Vec3& reportedAngular,
                    const Vec3& fixedAngular, Vec3& position, Quat& rotation);

// 손 포즈를 leadSec 만큼 앞당긴다 (실험용, 기본 꺼짐).
//   p' = p + lead · v,  R' = exp(lead · ω) · R      (각속도는 world)
void leadPose(double leadSec, const Vec3& linear, const Vec3& angular, Vec3& position, Quat& rotation);

}  // namespace controllerfix
