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

// 추적이 순간적으로 튈 때 (실측 150 m/s, 손 위치 약 10 m 이동) 직전 값을 잠깐 유지한다.
struct SpikeGuardConfig {
    static constexpr double DEFAULT_MAX_SPEED = 25.0;      // m/s, 실제 던지기는 이보다 훨씬 느림
    static constexpr double DEFAULT_MAX_HOLD_SEC = 0.150;  // 이보다 길면 튐이 아니라고 보고 그대로 내보냄

    bool enabled = true;
    double maxSpeed = DEFAULT_MAX_SPEED;
    double maxHoldSec = DEFAULT_MAX_HOLD_SEC;
};

class SpikeGuard {
public:
    // 이 프레임을 버리고 직전 값을 유지해야 하면 true
    bool isSpike(const SpikeGuardConfig& config, double timeSec, double speed);

private:
    static constexpr double NOT_STARTED = -1;
    double m_startSec = NOT_STARTED;
};

struct Vec2 {
    double x = 0, y = 0;
};

// 왼손을 얼굴 가까이 대거나(gesture) 오른쪽 스틱을 누른 채 왼쪽 스틱을 밀면 D-pad 로 보낸다
struct DpadConfig {
    static constexpr double DEFAULT_THRESHOLD = 0.5;
    static constexpr double DEFAULT_GESTURE_DISTANCE = 0.22;  // m, 손-머리 거리
    static constexpr double GESTURE_RELEASE_RATIO = 1.25;     // 경계에서 깜빡이지 않게 놓을 때는 더 멀어야 함
    // D-pad 에 들어갈 때 왼손 진동
    static constexpr double HAPTIC_DURATION_SEC = 0.040;
    static constexpr double HAPTIC_FREQUENCY_HZ = 160.0;
    static constexpr double HAPTIC_AMPLITUDE = 0.6;

    bool enabled = true;
    bool gesture = true;  // false = 오른쪽 스틱 누름
    bool haptic = true;
    double threshold = DEFAULT_THRESHOLD;
    double gestureDistance = DEFAULT_GESTURE_DISTANCE;
};

struct DpadState {
    bool up = false, down = false, left = false, right = false;
};

DpadState dpadFromStick(const DpadConfig& config, const Vec2& stick);
bool dpadGestureHeld(const DpadConfig& config, bool held, double distance);

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

// SteamVR 드라이버 포즈 한 프레임. 각속도는 rotation 기준 로컬, 나머지는 world.
struct VdPose {
    Vec3 position;
    Quat rotation;
    Vec3 linear;
    Vec3 angular;
    Vec3 acceleration;
};

struct VdPoseConfig {
    double pitchDeg = 0;
    double angularPitchDeg = 0;
    double poseLagSec = 0;   // 0 = 끔
    double poseLeadSec = 0;  // 0 = 끔
    double linearScale = 1;
    double angularScale = 1;
};

// 속도 프레임 보정 → 손 위치 보정 → 세기 배율 순서.
// 배율은 게임 쪽 세기 조절이라 손 위치 계산에 들어가면 안 된다 (맨 마지막에 적용).
VdPose fixVdPose(const VdPose& reported, const VdPoseConfig& config);

}  // namespace controllerfix
