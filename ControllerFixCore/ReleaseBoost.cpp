#include "ReleaseBoost.h"

#include <cmath>

namespace controllerfix {

namespace {

double speed(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

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
