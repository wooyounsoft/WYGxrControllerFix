#include "ControllerTuning.h"

#include <algorithm>
#include <cmath>

#include "QuatMath.h"
#include "VdFrameFix.h"

namespace controllerfix {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double HALF = 0.5;
constexpr double MAX_AMPLITUDE = 1.0;
constexpr double MIN_ANGLE = 1e-12;

// 회전 벡터(축 · 각도) → 쿼터니언
Quat fromRotationVector(const Vec3& r) {
    double angle = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (angle < MIN_ANGLE) return {};
    double s = std::sin(angle * HALF) / angle;
    return {std::cos(angle * HALF), r.x * s, r.y * s, r.z * s};
}

Quat normalized(const Quat& q) {
    double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

Vec3 scaled(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }

}  // namespace

bool SpikeGuard::isSpike(const SpikeGuardConfig& config, double timeSec, double speed) {
    if (!config.enabled || speed <= config.maxSpeed) {
        m_startSec = NOT_STARTED;
        return false;
    }
    if (m_startSec == NOT_STARTED) m_startSec = timeSec;
    return timeSec - m_startSec <= config.maxHoldSec;
}

DpadState dpadFromStick(const DpadConfig& config, const Vec2& stick) {
    return {stick.y >= config.threshold, stick.y <= -config.threshold, stick.x <= -config.threshold, stick.x >= config.threshold};
}

bool dpadGestureHeld(const DpadConfig& config, bool held, double distance) {
    return distance <= config.gestureDistance * (held ? DpadConfig::GESTURE_RELEASE_RATIO : 1.0);
}

void tuneHaptic(const HapticConfig& config, double& amplitude, double& durationSec) {
    if (!config.enabled) return;
    amplitude = std::min(MAX_AMPLITUDE, amplitude * config.gain);
    if (durationSec > 0) durationSec = std::max(durationSec, config.minDurationSec);
}

Quat angleOffsetRotation(const AngleOffsetConfig& config) {
    if (!config.enabled) return {};
    double a = config.pitchDeg * DEG_TO_RAD;
    return {std::cos(a * HALF), std::sin(a * HALF), 0, 0};
}

Vec2 shapeStick(const StickConfig& config, const Vec2& value) {
    if (!config.enabled) return value;
    double r = std::sqrt(value.x * value.x + value.y * value.y);
    if (r <= config.deadzone || r <= 0) return {};
    double t = std::min(1.0, (r - config.deadzone) / (1.0 - config.deadzone));
    double shaped = std::pow(t, config.curve);
    return {value.x / r * shaped, value.y / r * shaped};
}

void correctPoseLag(double lagSec, const Vec3& reportedLinear, const Vec3& fixedLinear, const Vec3& reportedAngular,
                    const Vec3& fixedAngular, Vec3& position, Quat& rotation) {
    position.x -= lagSec * (fixedLinear.x - reportedLinear.x);
    position.y -= lagSec * (fixedLinear.y - reportedLinear.y);
    position.z -= lagSec * (fixedLinear.z - reportedLinear.z);
    Quat undo = fromRotationVector({lagSec * reportedAngular.x, lagSec * reportedAngular.y, lagSec * reportedAngular.z});
    Quat redo = fromRotationVector({-lagSec * fixedAngular.x, -lagSec * fixedAngular.y, -lagSec * fixedAngular.z});
    rotation = normalized(multiply(redo, multiply(undo, rotation)));
}

void leadPose(double leadSec, const Vec3& linear, const Vec3& angular, Vec3& position, Quat& rotation) {
    position.x += leadSec * linear.x;
    position.y += leadSec * linear.y;
    position.z += leadSec * linear.z;
    Quat step = fromRotationVector({leadSec * angular.x, leadSec * angular.y, leadSec * angular.z});
    rotation = normalized(multiply(step, rotation));
}

VdPose fixVdPose(const VdPose& reported, const VdPoseConfig& config) {
    VdPose out = reported;
    out.linear = vdLocalToWorld(reported.rotation, reported.linear, config.pitchDeg);
    out.acceleration = vdLocalToWorld(reported.rotation, reported.acceleration, config.pitchDeg);
    Vec3 angularWorld = vdDriverAngularToWorld(reported.rotation, reported.angular, config.angularPitchDeg);
    if (config.poseLagSec != 0)
        correctPoseLag(config.poseLagSec, reported.linear, out.linear, rotate(reported.rotation, reported.angular), angularWorld,
                       out.position, out.rotation);
    if (config.poseLeadSec != 0) leadPose(config.poseLeadSec, out.linear, angularWorld, out.position, out.rotation);
    out.linear = scaled(out.linear, config.linearScale);
    out.acceleration = scaled(out.acceleration, config.linearScale);
    out.angular = scaled(rotate(conjugate(out.rotation), angularWorld), config.angularScale);
    return out;
}

}  // namespace controllerfix
