#include "VdFrameCalibrator.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "QuatMath.h"
#include "VdFrameFit.h"

namespace controllerfix {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double MIN_NORM = 1e-9;
constexpr double HALF = 0.5;
constexpr size_t HISTORY_SIZE = 3;  // 앞, 가운데, 뒤

double norm(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 unit(const Vec3& v) {
    double n = norm(v);
    return n < MIN_NORM ? Vec3{} : Vec3{v.x / n, v.y / n, v.z / n};
}
double angleDeg(const Vec3& a, const Vec3& b) {
    Vec3 ua = unit(a), ub = unit(b);
    double c = ua.x * ub.x + ua.y * ub.y + ua.z * ub.z;
    return std::acos(std::fmax(-1.0, std::fmin(1.0, c))) * RAD_TO_DEG;
}

// 단위 쿼터니언 → 회전 벡터, 최단 경로
Vec3 rotationVector(Quat q) {
    if (q.w < 0) q = {-q.w, -q.x, -q.y, -q.z};
    double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < MIN_NORM) return {};
    double angle = 2.0 * std::atan2(s, q.w);
    return {q.x * angle / s, q.y * angle / s, q.z * angle / s};
}

Quat fromRotationVector(const Vec3& r) {
    double angle = norm(r);
    if (angle < MIN_NORM) return {};
    double s = std::sin(angle * HALF) / angle;
    return {std::cos(angle * HALF), r.x * s, r.y * s, r.z * s};
}

// a → b 로 t 만큼 (작은 차이라 정규화 선형 보간으로 충분)
Quat blend(const Quat& a, Quat b, double t) {
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) b = {-b.w, -b.x, -b.y, -b.z};
    Quat q{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

bool samePose(const Quat& qa, const Vec3& pa, const Quat& qb, const Vec3& pb) {
    return qa.w == qb.w && qa.x == qb.x && qa.y == qb.y && qa.z == qb.z && pa.x == pb.x && pa.y == pb.y && pa.z == pb.z;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0;
    auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

}  // namespace

void VdFrameCalibrator::setPrior(const Quat& prior) {
    m_prior = prior;
    if (!m_calibrated) m_frame = prior;
}

void VdFrameCalibrator::reset() {
    for (auto& h : m_history) h.clear();
    m_pairs.clear();
    m_frame = m_prior;
    m_calibrated = false;
    m_angularReliable = true;
    m_fitErrorDeg = 0;
    m_nextSolveSec = 0;
}

void VdFrameCalibrator::addSample(int device, double timeSec, const Quat& rotation, const Vec3& position, const Vec3& reportedLinear,
                                  const Vec3& reportedAngular) {
    if (device < 0 || device >= MAX_DEVICES) return;
    // VD 의 뒤로 외삽(p - lag·v, exp(-lag·ω)·R)을 되돌려 실제 포즈 흐름을 얻는다
    const double lag = m_undoLagSec;
    Vec3 truthPosition{position.x + lag * reportedLinear.x, position.y + lag * reportedLinear.y, position.z + lag * reportedLinear.z};
    Quat truthRotation = multiply(fromRotationVector({lag * reportedAngular.x, lag * reportedAngular.y, lag * reportedAngular.z}), rotation);
    const Quat& frameRotation = truthRotation;  // 보고 속도는 외삽 전 회전 기준
    std::deque<Sample>& history = m_history[device];
    // SteamVR 은 같은 포즈를 여러 번 넘긴다 → 새 포즈만 쓴다
    if (!history.empty() && (timeSec <= history.back().timeSec ||
                             samePose(truthRotation, truthPosition, history.back().truthRotation, history.back().truthPosition)))
        return;
    history.push_back({timeSec, truthRotation, truthPosition, frameRotation, reportedLinear, reportedAngular});
    if (history.size() > HISTORY_SIZE) history.pop_front();
    if (history.size() < HISTORY_SIZE) return;
    addPairs(history[0], history[2]);
    if (m_pairs.size() >= MIN_PAIRS && timeSec >= m_nextSolveSec) {
        m_nextSolveSec = timeSec + SOLVE_INTERVAL_SEC;
        solve();
    }
}

// 가운데 시점의 실제 속도(앞뒤 차분)와 다음 샘플의 보고 속도를 짝짓는다 (보고 값이 한 프레임쯤 늦음, 실측 -20 ms)
void VdFrameCalibrator::addPairs(const Sample& prev, const Sample& next) {
    double span = next.timeSec - prev.timeSec;
    if (span < MIN_SPAN_SEC || span > MAX_SPAN_SEC) return;
    Quat toLocal = conjugate(next.frameRotation);
    Vec3 dr = rotationVector(multiply(next.truthRotation, conjugate(prev.truthRotation)));
    Vec3 angular{dr.x / span, dr.y / span, dr.z / span};
    if (norm(angular) >= MIN_ANGULAR_SPEED && norm(next.angular) > MIN_NORM)
        m_pairs.push_back({unit(rotate(toLocal, angular)), unit(next.angular), true});
    Vec3 linear{(next.truthPosition.x - prev.truthPosition.x) / span, (next.truthPosition.y - prev.truthPosition.y) / span,
                (next.truthPosition.z - prev.truthPosition.z) / span};
    double speed = norm(linear);
    if (speed >= MIN_LINEAR_SPEED && speed <= MAX_LINEAR_SPEED && norm(next.linear) > MIN_NORM)
        m_pairs.push_back({unit(rotate(toLocal, linear)), unit(next.linear), false});
    while (m_pairs.size() > MAX_PAIRS) m_pairs.pop_front();
}

double VdFrameCalibrator::medianError(const Quat& b, size_t from, bool includeAngular) const {
    std::vector<double> errors;
    errors.reserve(m_pairs.size() - from);
    for (size_t i = from; i < m_pairs.size(); ++i)
        if (includeAngular || !m_pairs[i].angular) errors.push_back(angleDeg(rotate(b, m_pairs[i].reported), m_pairs[i].local));
    return median(errors);
}

bool VdFrameCalibrator::fit(bool includeAngular, Quat& fitted, double& error) const {
    std::vector<Vec3> source, target;
    source.reserve(m_pairs.size());
    target.reserve(m_pairs.size());
    for (const Pair& p : m_pairs) {
        if (!includeAngular && p.angular) continue;
        source.push_back(p.reported);
        target.push_back(p.local);
    }
    if (source.size() < (includeAngular ? MIN_PAIRS : MIN_LINEAR_PAIRS)) return false;
    bestRotation(source, target, fitted);
    error = medianError(fitted, 0, includeAngular);
    return error <= (includeAngular ? MAX_COMBINED_ERROR_DEG : MAX_FIT_ERROR_DEG);
}

void VdFrameCalibrator::solve() {
    // 접속 중 틀어짐이 크게 바뀌면 (예: 기기 전환) 옛 데이터를 버리고 최근 것만으로 다시 맞춘다
    if (m_calibrated && m_pairs.size() > RECENT_PAIRS &&
        medianError(m_frame, m_pairs.size() - RECENT_PAIRS, m_angularReliable) > RESET_ERROR_DEG) {
        m_pairs.erase(m_pairs.begin(), m_pairs.end() - static_cast<std::ptrdiff_t>(RECENT_PAIRS));
        m_calibrated = false;
    }
    // 회전 + 이동이 한 B 로 맞으면 둘 다, 아니면 이동만 (각속도 규칙이 다른 공간)
    Quat fitted;
    double error = 0;
    bool angularOk = fit(true, fitted, error);
    if (!angularOk && !fit(false, fitted, error)) return;  // 맞는 회전이 없다 → 기존 값 유지
    if (m_calibrated && angularOk != m_angularReliable) m_calibrated = false;  // 방식이 바뀌면 섞지 않는다
    m_angularReliable = angularOk;
    m_frame = m_calibrated ? blend(m_frame, fitted, SMOOTHING) : fitted;
    m_calibrated = true;
    m_fitErrorDeg = error;
    ++m_solveCount;
}

}  // namespace controllerfix
