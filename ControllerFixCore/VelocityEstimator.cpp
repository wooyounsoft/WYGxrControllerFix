#include "VelocityEstimator.h"

#include <cmath>

#include "QuatMath.h"
#include <vector>

namespace controllerfix {

namespace {

constexpr double EPSILON = 1e-9;
constexpr size_t QUADRATIC_MIN_SAMPLES = 4;  // 이보다 적으면 1차 회귀
constexpr size_t MAX_OUTLIER_PASSES = 2;     // 70ms 구간(약 5샘플)에서 튐은 많아야 1~2개

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// 단위 쿼터니언 → 회전 벡터 (axis * angle), 최단 경로
Vec3 toRotationVector(Quat q) {
    if (q.w < 0) q = {-q.w, -q.x, -q.y, -q.z};
    double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < EPSILON) return {};
    double angle = 2.0 * std::atan2(s, q.w);
    return scale({q.x, q.y, q.z}, angle / s);
}

bool samePose(const PoseSample& a, const PoseSample& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotation.w == b.rotation.w && a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y &&
           a.rotation.z == b.rotation.z;
}

// 3x3 선형계 (Cramer). 특이하면 false.
bool solve3(const double m[3][3], const double r[3], double out[3]) {
    auto det = [](const double a[3][3]) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
               a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
               a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    double d = det(m);
    if (std::fabs(d) < EPSILON) return false;
    for (int col = 0; col < 3; ++col) {
        double t[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) t[i][j] = (j == col) ? r[i] : m[i][j];
        out[col] = det(t) / d;
    }
    return true;
}

double smoothstep(double low, double high, double x) {
    if (high <= low) return x >= high ? 1.0 : 0.0;
    double t = (x - low) / (high - low);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

}  // namespace

BlendWeights computeBlendWeights(const VelocityEstimate& estimated, const BlendConfig& config) {
    return {smoothstep(config.linearLow, config.linearHigh, length(estimated.linear)),
            smoothstep(config.angularLow, config.angularHigh, length(estimated.angular))};
}

Vec3 lerp(const Vec3& from, const Vec3& to, double weight) {
    return {from.x + (to.x - from.x) * weight, from.y + (to.y - from.y) * weight, from.z + (to.z - from.z) * weight};
}

void VelocityEstimator::addSample(const PoseSample& sample) {
    m_lastInputTimeSec = sample.timeSec;
    if (!m_samples.empty()) {
        const PoseSample& last = m_samples.back();
        double dt = sample.timeSec - last.timeSec;
        if (dt <= 0 || samePose(sample, last)) return;
        bool reacquired = dt > m_config.freezeSec;  // 고정돼 있던 기록은 실제 움직임이 아님
        if (reacquired || length(sub(sample.position, last.position)) / dt > m_config.maxStepSpeed) m_samples.clear();
    }
    m_samples.push_back(sample);
    while (m_samples.size() > MIN_SAMPLES && sample.timeSec - m_samples.front().timeSec > m_config.windowSec)
        m_samples.pop_front();
}

bool VelocityEstimator::estimate(VelocityEstimate& out) const {
    if (m_samples.size() < MIN_SAMPLES) return false;
    if (m_lastInputTimeSec - m_samples.back().timeSec > m_config.freezeSec) return false;  // 추적 끊김 중
    out.linear = scale(estimateLinear(), m_config.linearScale);
    out.angular = scale(estimateAngular(), m_config.angularScale);
    return true;
}

Vec3 VelocityEstimator::estimateLinear() const {
    const double tNewest = m_samples.back().timeSec;
    std::vector<bool> used(m_samples.size(), true);
    size_t usedCount = m_samples.size();

    // 최소자승: p(t) = c0 + c1 t (+ c2 t^2), t = 샘플시간 - 최신시간 (<= 0), 속도 = c1
    auto fit = [&](double coef[3][3]) {
        const bool quadratic = usedCount >= QUADRATIC_MIN_SAMPLES;
        double s[5] = {};     // sum t^k
        double r[3][3] = {};  // [axis][k] sum p * t^k
        for (size_t i = 0; i < m_samples.size(); ++i) {
            if (!used[i]) continue;
            const PoseSample& smp = m_samples[i];
            double t = smp.timeSec - tNewest;
            double tk = 1;
            for (double& sk : s) { sk += tk; tk *= t; }
            const double p[3] = {smp.position.x, smp.position.y, smp.position.z};
            tk = 1;
            for (int k = 0; k < 3; ++k) {
                for (int a = 0; a < 3; ++a) r[a][k] += p[a] * tk;
                tk *= t;
            }
        }
        for (int a = 0; a < 3; ++a) {
            coef[a][2] = 0;
            if (quadratic) {
                const double m[3][3] = {{s[0], s[1], s[2]}, {s[1], s[2], s[3]}, {s[2], s[3], s[4]}};
                if (solve3(m, r[a], coef[a])) continue;
            }
            double denom = s[0] * s[2] - s[1] * s[1];
            coef[a][1] = std::fabs(denom) < EPSILON ? 0 : (s[0] * r[a][1] - s[1] * r[a][0]) / denom;
            coef[a][0] = s[0] < EPSILON ? 0 : (r[a][0] - coef[a][1] * s[1]) / s[0];
        }
    };

    auto predict = [&](const double c[3][3], double t) {
        return Vec3{c[0][0] + c[0][1] * t + c[0][2] * t * t, c[1][0] + c[1][1] * t + c[1][2] * t * t,
                    c[2][0] + c[2][1] * t + c[2][2] * t * t};
    };

    double coef[3][3];
    // 순간 튐 제거 (leave-one-out): 샘플 하나를 빼고 맞춘 곡선에서 그 샘플이 가장 멀리 벗어나면 제외.
    // 일반 잔차는 2차 곡선이 끝점 튐을 따라 휘어버려서 최신 샘플 튐을 못 잡는다.
    for (size_t pass = 0; pass < MAX_OUTLIER_PASSES && usedCount > MIN_SAMPLES; ++pass) {
        size_t worst = 0;
        double worstResidual = 0;
        for (size_t i = 0; i < m_samples.size(); ++i) {
            if (!used[i]) continue;
            used[i] = false;
            --usedCount;
            fit(coef);
            used[i] = true;
            ++usedCount;
            double residual = length(sub(m_samples[i].position, predict(coef, m_samples[i].timeSec - tNewest)));
            if (residual > worstResidual) { worstResidual = residual; worst = i; }
        }
        if (worstResidual <= m_config.outlierResidual) break;
        used[worst] = false;
        --usedCount;
    }
    fit(coef);
    return {coef[0][1], coef[1][1], coef[2][1]};
}

Vec3 VelocityEstimator::estimateAngular() const {
    const PoseSample& newest = m_samples.back();
    // 샘플 k → 최신 회전(월드 기준) = omega * (t_newest - t_k)
    Vec3 num;
    double den = 0;
    for (const PoseSample& smp : m_samples) {
        double dt = newest.timeSec - smp.timeSec;
        if (dt <= 0) continue;
        Vec3 rv = toRotationVector(multiply(newest.rotation, conjugate(smp.rotation)));
        num = {num.x + rv.x * dt, num.y + rv.y * dt, num.z + rv.z * dt};
        den += dt * dt;
    }
    if (den < EPSILON) return {};
    Vec3 omegaWorld = scale(num, 1.0 / den);
    if (m_config.angularFrame == AngularFrame::Local) return rotate(conjugate(newest.rotation), omegaWorld);
    return omegaWorld;
}

}  // namespace controllerfix
