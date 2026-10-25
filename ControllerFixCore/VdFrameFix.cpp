#include "VdFrameFix.h"

#include <cmath>

#include "QuatMath.h"

namespace controllerfix {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double HALF = 0.5;

Quat rotationAboutX(double angleRad) { return {std::cos(angleRad * HALF), std::sin(angleRad * HALF), 0, 0}; }

}  // namespace

Vec3 vdLocalToWorld(const Quat& poseRotation, const Vec3& reported, double pitchDeg) {
    // Qᵀ = X축 +pitch 회전
    Vec3 local = rotate(rotationAboutX(pitchDeg * DEG_TO_RAD), reported);
    return rotate(poseRotation, local);
}

namespace {

constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double MIN_NORM = 1e-9;

double norm(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

double angleDeg(const Vec3& a, const Vec3& b) {
    double n = norm(a) * norm(b);
    if (n < MIN_NORM) return 0;
    double c = (a.x * b.x + a.y * b.y + a.z * b.z) / n;
    return std::acos(std::fmax(-1.0, std::fmin(1.0, c))) * RAD_TO_DEG;
}

}  // namespace

void VdFrameDetector::addVote(const Vec3& measured, const Vec3& raw, const Vec3& fixed) {
    if (norm(measured) < MIN_SPEED) return;
    double rawErr = angleDeg(raw, measured);
    double fixedErr = angleDeg(fixed, measured);
    if (std::fabs(rawErr - fixedErr) < AMBIGUOUS_DEG) return;
    m_votes.push_back(fixedErr < rawErr);
    if (m_votes.size() > WINDOW_VOTES) m_votes.pop_front();
    if (m_votes.size() < MIN_VOTES) return;
    double ratio = fixRatio();
    if (ratio >= DECIDE_RATIO) m_verdict = Verdict::Gxr;
    else if (ratio <= 1.0 - DECIDE_RATIO) m_verdict = Verdict::Normal;
}

double VdFrameDetector::fixRatio() const {
    if (m_votes.empty()) return 0;
    size_t fix = 0;
    for (bool v : m_votes) fix += v ? 1 : 0;
    return static_cast<double>(fix) / static_cast<double>(m_votes.size());
}

Vec3 vdDriverAngularToWorld(const Quat& poseRotation, const Vec3& driverAngular, double pitchDeg) {
    return vdLocalToWorld(poseRotation, rotate(poseRotation, driverAngular), pitchDeg);
}

}  // namespace controllerfix
