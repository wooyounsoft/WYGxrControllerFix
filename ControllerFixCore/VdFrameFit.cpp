#include "VdFrameFit.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "QuatMath.h"

namespace controllerfix {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double HALF = 0.5;
constexpr double MIN_NORM = 1e-9;
constexpr int QUAT_DIM = 4;
constexpr int JACOBI_MAX_SWEEPS = 50;
constexpr double JACOBI_EPSILON = 1e-12;

double norm(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 unit(const Vec3& v) {
    double n = norm(v);
    return n < MIN_NORM ? Vec3{} : Vec3{v.x / n, v.y / n, v.z / n};
}
double angleDeg(const Vec3& a, const Vec3& b) {
    double c = dot(unit(a), unit(b));
    return std::acos(std::fmax(-1.0, std::fmin(1.0, c))) * RAD_TO_DEG;
}
Quat rotationAboutX(double angleRad) { return {std::cos(angleRad * HALF), std::sin(angleRad * HALF), 0, 0}; }

double median(std::vector<double> values) {
    if (values.empty()) return 0;
    auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

// 대칭 4x4 행렬의 최대 고유값과 고유벡터 (Jacobi)
double largestEigen(double a[QUAT_DIM][QUAT_DIM], double vec[QUAT_DIM]) {
    double v[QUAT_DIM][QUAT_DIM] = {};
    for (int i = 0; i < QUAT_DIM; ++i) v[i][i] = 1;
    for (int sweep = 0; sweep < JACOBI_MAX_SWEEPS; ++sweep) {
        double off = 0;
        for (int p = 0; p < QUAT_DIM; ++p)
            for (int q = p + 1; q < QUAT_DIM; ++q) off += a[p][q] * a[p][q];
        if (off < JACOBI_EPSILON) break;
        for (int p = 0; p < QUAT_DIM; ++p) {
            for (int q = p + 1; q < QUAT_DIM; ++q) {
                if (std::fabs(a[p][q]) < MIN_NORM) continue;
                double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < QUAT_DIM; ++k) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < QUAT_DIM; ++k) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < QUAT_DIM; ++k) {
                    double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    int best = 0;
    for (int i = 1; i < QUAT_DIM; ++i)
        if (a[i][i] > a[best][best]) best = i;
    for (int i = 0; i < QUAT_DIM; ++i) vec[i] = v[i][best];
    return a[best][best];
}

// Horn 방법: Σ target·(q·source) 를 최대로 하는 회전 q. 반환값 = 그 합.
double bestRotation(const std::vector<Vec3>& source, const std::vector<Vec3>& target, Quat& q) {
    double s[3][3] = {};
    for (size_t i = 0; i < source.size(); ++i) {
        const double a[3] = {source[i].x, source[i].y, source[i].z};
        const double b[3] = {target[i].x, target[i].y, target[i].z};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) s[r][c] += a[r] * b[c];
    }
    double n[QUAT_DIM][QUAT_DIM] = {
        {s[0][0] + s[1][1] + s[2][2], s[1][2] - s[2][1], s[2][0] - s[0][2], s[0][1] - s[1][0]},
        {s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2], s[0][1] + s[1][0], s[2][0] + s[0][2]},
        {s[2][0] - s[0][2], s[0][1] + s[1][0], -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1]},
        {s[0][1] - s[1][0], s[2][0] + s[0][2], s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2]},
    };
    double e[QUAT_DIM];
    double score = largestEigen(n, e);
    q = {e[0], e[1], e[2], e[3]};
    return score;
}

}  // namespace

VdFrameFit::VdFrameFit(double priorPitchDeg) { setPriorPitch(priorPitchDeg); }

void VdFrameFit::setPriorPitch(double pitchDeg) {
    m_prior = rotationAboutX(pitchDeg * DEG_TO_RAD);
    if (!m_refined) m_fix = m_prior;
}

void VdFrameFit::addSample(double timeSec, const Quat& spaceRotation, const Vec3& measured, const Vec3& reported) {
    if (norm(measured) < MIN_SPEED || norm(reported) < MIN_NORM) return;
    m_samples.push_back({unit(rotate(conjugate(spaceRotation), measured)), unit(reported), angleDeg(reported, measured)});
    if (m_samples.size() > MAX_SAMPLES) m_samples.pop_front();
    if (m_samples.size() < MIN_SAMPLES || timeSec < m_nextSolveSec) return;
    m_nextSolveSec = timeSec + SOLVE_INTERVAL_SEC;
    solve();
}

void VdFrameFit::solve() {
    std::vector<Vec3> target, source;
    target.reserve(m_samples.size());
    source.reserve(m_samples.size());
    for (const Sample& s : m_samples) {
        target.push_back(s.localMeasured);
        source.push_back(s.reported);
    }
    Quat fitted;
    bestRotation(source, target, fitted);

    auto medianError = [&](const Quat& b) {
        std::vector<double> errors;
        errors.reserve(m_samples.size());
        for (const Sample& s : m_samples) errors.push_back(angleDeg(rotate(b, s.reported), s.localMeasured));
        return median(errors);
    };
    std::vector<double> rawErrors;
    for (const Sample& s : m_samples) rawErrors.push_back(s.rawErrorDeg);
    double priorError = medianError(m_prior);
    double fittedError = medianError(fitted);
    m_refined = m_samples.size() >= REFINE_MIN_SAMPLES && fittedError + REFINE_MARGIN_DEG < priorError;
    m_fix = m_refined ? fitted : m_prior;
    m_fitErrorDeg = m_refined ? fittedError : priorError;
    m_rawErrorDeg = median(rawErrors);
    // 필요 없는 기기 판정은 기본 공식과 맞춘 값 둘 다보다 원래 값이 확실히 나을 때만.
    // 기본 공식이 안 맞는 공간(포즈 오프셋이 다른 앱)을 아직 맞추기 전에 꺼 버리지 않게 한다.
    double bestFixError = std::fmin(priorError, fittedError);
    if (m_fitErrorDeg + MARGIN_DEG < m_rawErrorDeg) m_verdict = Verdict::Fix;
    else if (m_rawErrorDeg < MAX_FIT_ERROR_DEG && m_rawErrorDeg + MARGIN_DEG < bestFixError) m_verdict = Verdict::Normal;
}

Vec3 VdFrameFit::apply(const Quat& spaceRotation, const Vec3& reported) const {
    return rotate(spaceRotation, rotate(m_fix, reported));
}

void VdFrameFit::reset() {
    m_samples.clear();
    m_nextSolveSec = 0;
    m_verdict = Verdict::Undecided;
    m_refined = false;
    m_fix = m_prior;
}

double VdFrameFit::fixAngleDeg() const {
    return 2 * std::acos(std::fmin(1.0, std::fabs(m_fix.w))) * RAD_TO_DEG;
}

Vec3 VdFrameFit::fixAxis() const {
    Vec3 axis = unit({m_fix.x, m_fix.y, m_fix.z});
    return m_fix.w < 0 ? Vec3{-axis.x, -axis.y, -axis.z} : axis;
}

}  // namespace controllerfix
