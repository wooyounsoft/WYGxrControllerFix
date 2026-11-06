#include "ReleaseBoost.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace controllerfix {

namespace {

constexpr double MIN_DIRECTION_LENGTH = 1e-9;
constexpr double HALF = 0.5;
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
constexpr double GAUSS_CUTOFF_SIGMAS = 3.0;

double speed(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 scaled(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }

}  // namespace

void ReleaseBoost::addVelocity(double timeSec, const VelocityEstimate& v) {
    // 시간순만 기록한다. 게임이 한 프레임에 여러 예측 시각으로 조회하면 (Titanfall) 시간이 뒤섞이는데,
    // 해제 시점부터 거꾸로 찾는 로직이 엉뚱한 샘플을 고르거나 너무 오래됐다고 포기했다 (2026-11-03).
    if (!m_history.empty() && timeSec <= m_history.back().timeSec) {
        ++m_outOfOrder;
        return;
    }
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
    event.outOfOrder = m_outOfOrder;
    m_outOfOrder = 0;
    if (!m_config.enabled || m_history.empty()) return false;

    const Sample* peak = &m_history.front();
    for (const Sample& s : m_history) {
        if (timeSec - s.timeSec > m_config.peakWindowSec) continue;
        // 같은 속도면 최근 것 (최고 속도 경과 시간을 maxPeakAgeSec 와 비교하므로)
        if (speed(s.velocity.linear) >= speed(peak->velocity.linear)) peak = &s;
    }
    const double peakSpeed = speed(peak->velocity.linear);
    // 기준 시점: 해제보다 minLookbackSec 앞 (Steam Link 처럼 조금 늦은 속도를 쓴다).
    // 그 시점부터 거꾸로, 아직 빨랐던 마지막 순간 (최고 속도 이후 감속이 시작되기 직전)을 찾는다.
    const double referenceSec = timeSec - m_config.minLookbackSec;
    const Sample* fast = nullptr;
    for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
        if (timeSec - it->timeSec > m_config.peakWindowSec) break;
        // 기준 시점보다 새 샘플은 건너뛴다 (가장 가까운 샘플은 반 간격까지 허용)
        auto older = std::next(it);
        if (it->timeSec > referenceSec && older != m_history.rend() &&
            std::fabs(older->timeSec - referenceSec) <= std::fabs(it->timeSec - referenceSec))
            continue;
        if (speed(it->velocity.linear) >= peakSpeed * m_config.fastRatio) {
            fast = &*it;
            break;
        }
    }
    if (fast == nullptr) fast = peak;
    event.peakSpeed = speed(fast->velocity.linear);
    event.currentSpeed = speed(m_history.back().velocity.linear);
    event.peakAgeSec = timeSec - fast->timeSec;
    event.maxSpeed = peakSpeed;
    event.maxAgeSec = timeSec - peak->timeSec;
    for (const Sample& s : m_history) event.samples += timeSec - s.timeSec <= m_config.peakWindowSec ? 1 : 0;
    using Result = ReleaseEvent::Result;
    if (peakSpeed < m_config.minSpeed) { event.result = Result::NotThrow; return false; }
    if (fast == &m_history.back()) { event.result = Result::OnTime; return false; }  // 제때 놓음 (minLookbackSec = 0 일 때)
    if (event.peakAgeSec > m_config.maxPeakAgeSec) { event.result = Result::TooOld; return false; }
    event.result = Result::Fired;

    m_holdVelocity = fast->velocity;
    double window = m_config.directionWindowSec;
    if (m_config.directionArcDeg > 0 && event.peakSpeed > 0)
        window = std::min(window, 2 * ReleaseBoostConfig::THROW_ARC_RADIUS_M * m_config.directionArcDeg * DEG_TO_RAD / event.peakSpeed);
    if (window > 0) {
        // 가우시안 가중 평균 (σ = 구간/2). 샘플 간격(약 15 ms)보다 짧은 구간도 속도에 따라 부드럽게 변한다.
        const double sigma = window * HALF;
        Vec3 sum;
        for (const Sample& s : m_history) {
            double dt = s.timeSec - fast->timeSec;
            if (std::fabs(dt) > sigma * GAUSS_CUTOFF_SIGMAS) continue;
            double w = std::exp(-HALF * (dt / sigma) * (dt / sigma));
            sum = {sum.x + w * s.velocity.linear.x, sum.y + w * s.velocity.linear.y, sum.z + w * s.velocity.linear.z};
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

const char* releaseResultName(ReleaseEvent::Result r) {
    switch (r) {
    case ReleaseEvent::Result::Fired: return "boosted";
    case ReleaseEvent::Result::NotThrow: return "not a throw";
    case ReleaseEvent::Result::OnTime: return "on time";
    case ReleaseEvent::Result::TooOld: return "too old";
    default: return "none";
    }
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
