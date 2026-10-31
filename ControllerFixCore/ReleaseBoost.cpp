#include "ReleaseBoost.h"

#include <algorithm>
#include <cmath>

namespace controllerfix {

namespace {

constexpr double MIN_DIRECTION_LENGTH = 1e-9;
constexpr double HALF = 0.5;

double speed(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 scaled(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }

}  // namespace

void ReleaseBoost::addVelocity(double timeSec, const VelocityEstimate& v) {
    m_history.push_back({timeSec, v});
    while (!m_history.empty() && timeSec - m_history.front().timeSec > m_config.peakWindowSec) m_history.pop_front();
}

bool ReleaseBoost::updateGrip(double timeSec, double value, ReleaseEvent& event) {
    if (!m_gripHeld) {
        if (value >= m_config.gripPress) m_gripHeld = true;
        return false;
    }
    if (value > m_config.gripRelease) return false;
    m_gripHeld = false;
    if (!m_config.enabled || m_history.empty()) return false;

    const Sample* peak = &m_history.front();
    for (const Sample& s : m_history) {
        if (timeSec - s.timeSec > m_config.peakWindowSec) continue;
        // 같은 속도면 최근 것 (최고 속도 경과 시간을 maxPeakAgeSec 와 비교하므로)
        if (speed(s.velocity.linear) >= speed(peak->velocity.linear)) peak = &s;
    }
    event.peakSpeed = speed(peak->velocity.linear);
    event.currentSpeed = speed(m_history.back().velocity.linear);
    event.peakAgeSec = timeSec - peak->timeSec;
    if (event.peakSpeed < m_config.minSpeed || event.peakSpeed <= event.currentSpeed) return false;
    if (event.peakAgeSec > m_config.maxPeakAgeSec) return false;

    m_holdVelocity = peak->velocity;
    if (m_config.directionWindowSec > 0) {
        Vec3 sum;
        for (const Sample& s : m_history) {
            if (std::fabs(s.timeSec - peak->timeSec) > m_config.directionWindowSec * HALF) continue;
            sum = {sum.x + s.velocity.linear.x, sum.y + s.velocity.linear.y, sum.z + s.velocity.linear.z};
        }
        double length = speed(sum);
        if (length > MIN_DIRECTION_LENGTH) m_holdVelocity.linear = scaled(sum, event.peakSpeed / length);
    }
    if (m_config.maxRatio > 0) {
        double limit = std::max(event.currentSpeed * m_config.maxRatio, m_config.minSpeed);
        if (event.peakSpeed > limit) {
            m_holdVelocity.linear = scaled(m_holdVelocity.linear, limit / event.peakSpeed);
            event.peakSpeed = limit;
        }
    }
    m_holdUntilSec = timeSec + m_config.holdSec;
    return true;
}

bool ReleaseBoost::override(double timeSec, VelocityEstimate& out) const {
    if (timeSec > m_holdUntilSec) return false;
    out = m_holdVelocity;
    return true;
}

void ReleaseBoost::reset() {
    m_history.clear();
    m_holdUntilSec = 0;
}

}  // namespace controllerfix
