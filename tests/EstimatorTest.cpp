// VelocityEstimator 검증: 해석적으로 속도를 아는 궤적에서 추정값 비교
#include <cmath>
#include <iterator>
#include <cstdio>
#include <sstream>

#include "QuatMath.h"
#include "ReleaseBoost.h"
#include "Settings.h"
#include "ControllerTuning.h"
#include "VdFrameCalibrator.h"
#include "VdFrameFit.h"
#include "VdFrameFix.h"
#include "VdHeadset.h"
#include "VelocityEstimator.h"

using namespace controllerfix;

namespace {

constexpr double SAMPLE_INTERVAL_SEC = 1.0 / 65.0;  // 측정된 VD 포즈 갱신 주기
constexpr double RADIUS = 0.5;
constexpr double OMEGA = 6.0;  // rad/s, 원운동 + 자전
constexpr double TOLERANCE_LINEAR = 0.05;   // 상대오차
constexpr double TOLERANCE_ANGULAR = 0.02;  // rad/s
constexpr int SAMPLE_COUNT = 40;
constexpr double JUMP_DISTANCE = 1.0;  // m, 추적 튐

double length3(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }

Quat yawQuat(double a) { return {std::cos(a / 2), 0, std::sin(a / 2), 0}; }  // y축(위) 회전

int g_failures = 0;

void check(bool ok, const char* name, double got, double expected) {
    std::printf("%-34s %s  got=%.4f expected=%.4f\n", name, ok ? "OK  " : "FAIL", got, expected);
    if (!ok) ++g_failures;
}

void testCircularMotion(AngularFrame frame) {
    VelocityEstimator est;
    EstimatorConfig cfg;
    cfg.angularFrame = frame;
    est.setConfig(cfg);
    double t = 0;
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        t = i * SAMPLE_INTERVAL_SEC;
        double a = OMEGA * t;
        est.addSample({t, {RADIUS * std::cos(a), 1.0, -RADIUS * std::sin(a)}, yawQuat(a)});
    }
    VelocityEstimate e;
    est.estimate(e);
    double a = OMEGA * t;
    double vx = -RADIUS * OMEGA * std::sin(a), vz = -RADIUS * OMEGA * std::cos(a);
    double err = std::hypot(e.linear.x - vx, e.linear.z - vz) / (RADIUS * OMEGA);
    check(err < TOLERANCE_LINEAR, "linear velocity (circle)", err, 0);
    // y축 회전은 월드/로컬 동일
    check(std::fabs(e.angular.y - OMEGA) < TOLERANCE_ANGULAR, frame == AngularFrame::World ? "angular y (world)" : "angular y (local)", e.angular.y, OMEGA);
}

void testLocalFrameDiffers() {
    // 컨트롤러가 x축으로 90° 기울어진 상태에서 월드 y축 회전 → 로컬에서는 다른 축
    VelocityEstimator est;
    EstimatorConfig cfg;
    cfg.angularFrame = AngularFrame::Local;
    est.setConfig(cfg);
    const double h = std::sqrt(0.5);
    Quat tilt{h, h, 0, 0};
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        double t = i * SAMPLE_INTERVAL_SEC;
        Quat y = yawQuat(OMEGA * t);
        Quat q{y.w * tilt.w - y.y * tilt.y, y.w * tilt.x + y.y * tilt.z, y.w * tilt.y + y.y * tilt.w, y.w * tilt.z - y.y * tilt.x};
        est.addSample({t, {}, q});
    }
    VelocityEstimate e;
    est.estimate(e);
    // R_tilt^-1 * (0, w, 0) = (0, 0, -w)
    check(std::fabs(e.angular.z + OMEGA) < TOLERANCE_ANGULAR, "angular local frame axis", e.angular.z, -OMEGA);
}

void testJumpResets() {
    VelocityEstimator est;
    constexpr double DRIFT_PER_SAMPLE = 0.001;  // 정지 포즈는 중복으로 걸러지므로 약간씩 이동
    for (int i = 0; i < SAMPLE_COUNT; ++i) est.addSample({i * SAMPLE_INTERVAL_SEC, {i * DRIFT_PER_SAMPLE, 1, 0}, {}});
    est.addSample({SAMPLE_COUNT * SAMPLE_INTERVAL_SEC, {JUMP_DISTANCE, 1, 0}, {}});
    check(est.sampleCount() == 1, "tracking jump resets history", static_cast<double>(est.sampleCount()), 1);
}

void testDuplicateIgnored() {
    VelocityEstimator est;
    est.addSample({0.0, {0, 1, 0}, {}});
    est.addSample({0.001, {0, 1, 0}, {}});
    check(est.sampleCount() == 1, "duplicate pose ignored", static_cast<double>(est.sampleCount()), 1);
}

constexpr double THROW_SPEED = 4.0;  // m/s, x 방향 등속
constexpr double SPIKE_OFFSET = 0.20;  // m, 실측 VD 튐 크기 (14~20 m/s 스텝)
constexpr double FREEZE_GAP_SEC = 0.3;
constexpr double REACQUIRE_JUMP = 0.5;  // m

void addThrow(VelocityEstimator& est, int count, double t0 = 0, double x0 = 0) {
    for (int i = 0; i < count; ++i) {
        double t = t0 + i * SAMPLE_INTERVAL_SEC;
        est.addSample({t, {x0 + THROW_SPEED * (t - t0), 1, 0}, {}});
    }
}

void testSpikeRejected() {
    for (bool atNewest : {false, true}) {
        VelocityEstimator est;
        addThrow(est, SAMPLE_COUNT);
        double t = SAMPLE_COUNT * SAMPLE_INTERVAL_SEC;
        est.addSample({t, {THROW_SPEED * t, 1 + SPIKE_OFFSET, 0}, {}});  // 위로 튐
        if (!atNewest) est.addSample({t + SAMPLE_INTERVAL_SEC, {THROW_SPEED * (t + SAMPLE_INTERVAL_SEC), 1, 0}, {}});
        VelocityEstimate e;
        est.estimate(e);
        double err = length3(e.linear.x - THROW_SPEED, e.linear.y, e.linear.z) / THROW_SPEED;
        check(err < TOLERANCE_LINEAR, atNewest ? "spike at newest sample rejected" : "spike mid-window rejected", err, 0);
    }
}

void testFreezeAndReacquire() {
    VelocityEstimator est;
    addThrow(est, SAMPLE_COUNT);
    double tLast = (SAMPLE_COUNT - 1) * SAMPLE_INTERVAL_SEC;
    double xLast = THROW_SPEED * tLast;
    // 카메라 밖: VD가 같은 포즈를 계속 보냄
    for (double t = tLast; t < tLast + FREEZE_GAP_SEC; t += SAMPLE_INTERVAL_SEC) est.addSample({t, {xLast, 1, 0}, {}});
    VelocityEstimate e;
    check(!est.estimate(e), "frozen tracking -> no estimate", est.estimate(e), 0);
    // 재추적: 점프 후 다시 등속
    addThrow(est, 1, tLast + FREEZE_GAP_SEC, xLast + REACQUIRE_JUMP);
    check(est.sampleCount() == 1, "reacquire clears frozen history", static_cast<double>(est.sampleCount()), 1);
}

// 실측 배틀 시스터 패턴: 최고 5 m/s 이후 감속, 370ms 뒤 0.5 m/s 에서 그립 해제
constexpr double PEAK_THROW_SPEED = 5.0;
constexpr double LATE_RELEASE_SPEED = 0.5;
constexpr double GRIP_HELD = 1.0;
constexpr double GRIP_OPEN = 0.0;

VelocityEstimate linearVelocity(double speed) {
    VelocityEstimate v;
    v.linear = {speed, 0, 0};
    return v;
}

void testReleaseBoostUsesPeak() {
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    double t = 0;
    for (; t < 0.10; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(PEAK_THROW_SPEED));
    for (; t < 0.10 + cfg.maxPeakAgeSec / 2; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(LATE_RELEASE_SPEED));
    bool fired = boost.updateGrip(t, GRIP_OPEN, ev);
    VelocityEstimate out;
    bool held = boost.override(t + cfg.holdSec / 2, out);
    check(fired && held && std::fabs(out.linear.x - PEAK_THROW_SPEED) < 1e-9, "release boost uses recent peak", out.linear.x, PEAK_THROW_SPEED);
    check(!boost.override(t + cfg.holdSec * 2, out), "release boost expires after hold", 0, 0);
}

void testReleaseBoostIgnoresOldPeakAndSlowMoves() {
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    double t = 0;
    for (; t < 0.10; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(PEAK_THROW_SPEED));
    for (; t < 0.10 + cfg.peakWindowSec * 2; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(LATE_RELEASE_SPEED));
    check(!boost.updateGrip(t, GRIP_OPEN, ev), "peak older than window ignored (just placing)", ev.peakSpeed, LATE_RELEASE_SPEED);
}

// 휘두른 뒤 멈췄다가 내려놓기: 최고 속도가 창 안에 있어도 오래됐으면 보정 안 함
void testReleaseBoostIgnoresStalePeak() {
    constexpr double STALE_GAP_SEC = 0.120;
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    double t = 0;
    for (; t < 0.03; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(PEAK_THROW_SPEED));
    for (; t < 0.03 + STALE_GAP_SEC; t += SAMPLE_INTERVAL_SEC) boost.addVelocity(t, linearVelocity(LATE_RELEASE_SPEED));
    bool fired = boost.updateGrip(t, GRIP_OPEN, ev);
    check(!fired && ev.peakAgeSec > cfg.maxPeakAgeSec, "stale peak ignored (stop then place)", ev.peakAgeSec, cfg.maxPeakAgeSec);
}

// v_rep = Q·Rᵀ·v (Q = X축 -13°) → vdLocalToWorld 로 world 속도 복원
void testVdFrameFixInverts() {
    constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
    const double pitch = VdFrameFixConfig::DEFAULT_PITCH_DEG;
    const double h = std::sqrt(0.5);
    Quat poseRotation{h, 0.3, h * 0.8, -0.2};
    double n = std::sqrt(poseRotation.w * poseRotation.w + poseRotation.x * poseRotation.x + poseRotation.y * poseRotation.y +
                         poseRotation.z * poseRotation.z);
    poseRotation = {poseRotation.w / n, poseRotation.x / n, poseRotation.y / n, poseRotation.z / n};
    Vec3 world{THROW_SPEED, 1.0, -2.0};
    Quat q{std::cos(-pitch * DEG_TO_RAD / 2), std::sin(-pitch * DEG_TO_RAD / 2), 0, 0};
    Vec3 reported = rotate(q, rotate(conjugate(poseRotation), world));
    Vec3 fixed = vdLocalToWorld(poseRotation, reported, pitch);
    double err = length3(fixed.x - world.x, fixed.y - world.y, fixed.z - world.z);
    check(err < 1e-9, "vd frame fix inverts VD local frame", err, 0);

    // 각속도: DriverPose 에는 conj(R)·ω 로 들어온다
    Vec3 angularWorld{OMEGA, -1.0, 2.0};
    Vec3 angularShm = rotate(q, rotate(conjugate(poseRotation), angularWorld));
    Vec3 angularFixed = vdDriverAngularToWorld(poseRotation, rotate(conjugate(poseRotation), angularShm), pitch);
    double angErr = length3(angularFixed.x - angularWorld.x, angularFixed.y - angularWorld.y, angularFixed.z - angularWorld.z);
    check(angErr < 1e-9, "vd frame fix: driverpose angular (world)", angErr, 0);
}

// 자동 판별: 여러 자세로 휘두를 때 보정이 필요한 기기(GXR)와 아닌 기기를 구분
VdFrameDetector::Verdict runDetector(bool gxr) {
    constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
    constexpr int SWINGS = 200;
    constexpr double YAW_STEP = 0.37;   // rad, 매 샘플 자세 변화
    constexpr double PITCH_STEP = 0.23;
    const double pitch = VdFrameFixConfig::DEFAULT_PITCH_DEG;
    Quat frame{std::cos(-pitch * DEG_TO_RAD / 2), std::sin(-pitch * DEG_TO_RAD / 2), 0, 0};
    VdFrameDetector detector;
    for (int i = 0; i < SWINGS; ++i) {
        Quat yaw = yawQuat(i * YAW_STEP);
        Quat tilt{std::cos(i * PITCH_STEP / 2), std::sin(i * PITCH_STEP / 2), 0, 0};
        Quat poseRotation = multiply(yaw, tilt);
        Vec3 world{THROW_SPEED * std::cos(i * YAW_STEP), 1.0, THROW_SPEED * std::sin(i * PITCH_STEP)};
        Vec3 raw = gxr ? rotate(frame, rotate(conjugate(poseRotation), world)) : world;
        detector.addVote(world, raw, vdLocalToWorld(poseRotation, raw, pitch));
    }
    return detector.verdict();
}

// 판정 뒤에는 애매한 구간(70% 정도)이 와도 뒤집히지 않는다
void testVdFrameDetectorSticky() {
    constexpr int NORMAL_VOTES = 120;
    constexpr int MIXED_VOTES = 400;
    constexpr int MIXED_FIX_EVERY = 10;  // 10번 중 7번 보정 쪽
    constexpr int MIXED_FIX_COUNT = 7;
    VdFrameDetector detector;
    Vec3 measured{THROW_SPEED, 0, 0};
    Vec3 good = measured, bad{0, THROW_SPEED, 0};
    for (int i = 0; i < NORMAL_VOTES; ++i) detector.addVote(measured, good, bad);
    bool normal = detector.verdict() == VdFrameDetector::Verdict::Normal;
    for (int i = 0; i < MIXED_VOTES; ++i) {
        bool fixWins = i % MIXED_FIX_EVERY < MIXED_FIX_COUNT;
        detector.addVote(measured, fixWins ? bad : good, fixWins ? good : bad);
    }
    check(normal && detector.verdict() == VdFrameDetector::Verdict::Normal, "vd frame detector: sticky verdict", detector.fixRatio(), 0.7);
}

void testVdFrameDetector() {
    check(runDetector(true) == VdFrameDetector::Verdict::Gxr, "vd frame detector: GXR -> fix", 0, 0);
    check(runDetector(false) == VdFrameDetector::Verdict::Normal, "vd frame detector: normal -> no fix", 0, 0);
}

// 프레임 맞추기: 기본 공식(13°)과 다른 보정 회전도 움직임으로 찾아낸다
constexpr double FIT_DEG_TO_RAD = 3.14159265358979323846 / 180.0;
constexpr double FIT_PRIOR_PITCH_DEG = VdFrameFixConfig::DEFAULT_PITCH_DEG;

Quat fitTestRotation() {
    constexpr double TRUE_PITCH_DEG = 40;  // 포즈 오프셋이 섞인 실제 보정 회전
    constexpr double TRUE_YAW_DEG = 20;
    Quat b{std::cos(TRUE_PITCH_DEG * FIT_DEG_TO_RAD / 2), std::sin(TRUE_PITCH_DEG * FIT_DEG_TO_RAD / 2), 0, 0};
    return multiply(yawQuat(TRUE_YAW_DEG * FIT_DEG_TO_RAD), b);
}

void runFrameFit(bool needsFix, VdFrameFit& fit) {
    constexpr int SAMPLES = 400;
    constexpr double YAW_STEP = 0.37;
    constexpr double PITCH_STEP = 0.23;
    constexpr double ROLL_STEP = 0.11;
    constexpr double NOISE = 0.15;  // m/s, 측정 잡음
    Quat fix = fitTestRotation();
    for (int i = 0; i < SAMPLES; ++i) {
        Quat roll{std::cos(i * ROLL_STEP / 2), 0, 0, std::sin(i * ROLL_STEP / 2)};
        Quat tilt{std::cos(i * PITCH_STEP / 2), std::sin(i * PITCH_STEP / 2), 0, 0};
        Quat poseRotation = multiply(multiply(yawQuat(i * YAW_STEP), tilt), roll);
        Vec3 world{THROW_SPEED * std::cos(i * YAW_STEP), 1.0 + std::sin(i * ROLL_STEP), THROW_SPEED * std::sin(i * PITCH_STEP)};
        Vec3 reported = needsFix ? rotate(conjugate(fix), rotate(conjugate(poseRotation), world)) : world;
        Vec3 measured{world.x + NOISE * std::sin(i * 1.7), world.y + NOISE * std::cos(i * 2.3), world.z - NOISE * std::sin(i * 0.9)};
        fit.addSample(i * SAMPLE_INTERVAL_SEC * 10, poseRotation, measured, reported);
    }
    if (!needsFix) return;
    Quat poseRotation = multiply(yawQuat(1.0), Quat{std::cos(0.4), std::sin(0.4), 0, 0});
    Vec3 world{THROW_SPEED, -1.0, 2.0};
    Vec3 fixed = fit.apply(poseRotation, rotate(conjugate(fix), rotate(conjugate(poseRotation), world)));
    double err = length3(fixed.x - world.x, fixed.y - world.y, fixed.z - world.z) / length3(world.x, world.y, world.z);
    check(err < TOLERANCE_LINEAR, "frame fit recovers rotation", err, 0);
}

// 기본 공식과 많이 다른 공간(앱마다 다른 포즈 오프셋)도 맞추기 전에 '필요 없음'으로 꺼지지 않는다
void testVdFrameFitLargeOffsetNeverNormal() {
    constexpr int SAMPLES = 400;
    constexpr double OFFSET_PITCH_DEG = 70;
    constexpr double YAW_STEP = 0.37;
    constexpr double PITCH_STEP = 0.23;
    constexpr double ROLL_STEP = 0.11;
    Quat fix{std::cos(OFFSET_PITCH_DEG * FIT_DEG_TO_RAD / 2), std::sin(OFFSET_PITCH_DEG * FIT_DEG_TO_RAD / 2), 0, 0};
    VdFrameFit fit(FIT_PRIOR_PITCH_DEG);
    bool everNormal = false;
    for (int i = 0; i < SAMPLES; ++i) {
        Quat roll{std::cos(i * ROLL_STEP / 2), 0, 0, std::sin(i * ROLL_STEP / 2)};
        Quat tilt{std::cos(i * PITCH_STEP / 2), std::sin(i * PITCH_STEP / 2), 0, 0};
        Quat poseRotation = multiply(multiply(yawQuat(i * YAW_STEP), tilt), roll);
        Vec3 world{THROW_SPEED * std::cos(i * YAW_STEP), 1.0 + std::sin(i * ROLL_STEP), THROW_SPEED * std::sin(i * PITCH_STEP)};
        fit.addSample(i * SAMPLE_INTERVAL_SEC * 10, poseRotation, world, rotate(conjugate(fix), rotate(conjugate(poseRotation), world)));
        everNormal = everNormal || fit.verdict() == VdFrameFit::Verdict::Normal;
    }
    check(!everNormal && fit.verdict() == VdFrameFit::Verdict::Fix, "frame fit: large offset never 'not needed'", fit.fitErrorDeg(), fit.rawErrorDeg());
}

double dist3(const Vec3& a, const Vec3& b) { return length3(a.x - b.x, a.y - b.y, a.z - b.z); }

void testStickAndHaptic() {
    StickConfig stick;
    stick.enabled = true;
    stick.deadzone = 0.2;
    stick.curve = 2.0;
    Vec2 small = shapeStick(stick, {0.1, 0.1});
    Vec2 full = shapeStick(stick, {0, 1});
    Vec2 half = shapeStick(stick, {0.6, 0});  // (0.6-0.2)/0.8 = 0.5 → 0.25
    check(small.x == 0 && small.y == 0 && std::fabs(full.y - 1) < 1e-9 && std::fabs(half.x - 0.25) < 1e-9, "stick: deadzone + curve", half.x, 0.25);
    stick.enabled = false;
    Vec2 same = shapeStick(stick, {0.1, 0.1});
    check(same.x == 0.1 && same.y == 0.1, "stick: off = unchanged", same.x, 0.1);

    HapticConfig haptic;
    haptic.enabled = true;
    double amplitude = 0.8, duration = 0.005, minimal = -1;
    tuneHaptic(haptic, amplitude, duration);
    tuneHaptic(haptic, amplitude, minimal);
    check(amplitude == 1.0 && std::fabs(duration - HapticConfig::DEFAULT_MIN_DURATION_SEC) < 1e-12 && minimal == -1,
          "haptic: gain clamp + min duration", amplitude, 1.0);
}

// 손 위치: 원래 속도로 되돌린 포즈 → 보정 속도로 되돌린 포즈
void testPoseLag() {
    constexpr double LAG = 0.037;
    Vec3 shm{0.1, 1.2, -0.3};
    Quat shmRot = multiply(yawQuat(0.4), Quat{std::cos(0.2), std::sin(0.2), 0, 0});
    Vec3 wrongV{1.0, -2.0, 0.5}, trueV{2.5, 0.3, -1.0};
    Vec3 wrongW{3.0, 1.0, -2.0}, trueW{-1.0, 4.0, 0.5};
    auto expRot = [](const Vec3& r) {
        double a = length3(r.x, r.y, r.z);
        double s = std::sin(a / 2) / a;
        return Quat{std::cos(a / 2), r.x * s, r.y * s, r.z * s};
    };
    // VD 쪽: p = shm - lag·v_wrong, R = exp(-lag·ω_wrong)·R_shm
    Vec3 p{shm.x - LAG * wrongV.x, shm.y - LAG * wrongV.y, shm.z - LAG * wrongV.z};
    Quat r = multiply(expRot({-LAG * wrongW.x, -LAG * wrongW.y, -LAG * wrongW.z}), shmRot);
    correctPoseLag(LAG, wrongV, trueV, wrongW, trueW, p, r);
    Vec3 expectedP{shm.x - LAG * trueV.x, shm.y - LAG * trueV.y, shm.z - LAG * trueV.z};
    Quat expectedR = multiply(expRot({-LAG * trueW.x, -LAG * trueW.y, -LAG * trueW.z}), shmRot);
    double rotErr = std::fabs(std::fabs(r.w * expectedR.w + r.x * expectedR.x + r.y * expectedR.y + r.z * expectedR.z) - 1);
    check(dist3(p, expectedP) < 1e-12 && rotErr < 1e-12, "pose lag: corrected to true velocities", dist3(p, expectedP), 0);
}

// 앞당기기 = 같은 lag 로 되돌린 포즈를 정확히 원래대로 복원
void testPoseLead() {
    constexpr double LEAD = 0.025;
    Vec3 p0{0.1, 1.2, -0.3};
    Quat r0 = multiply(yawQuat(0.4), Quat{std::cos(0.2), std::sin(0.2), 0, 0});
    Vec3 v{2.5, 0.3, -1.0}, w{-1.0, 4.0, 0.5};
    Vec3 p = p0;
    Quat r = r0;
    correctPoseLag(LEAD, {}, v, {}, w, p, r);  // lag 만큼 되돌림
    leadPose(LEAD, v, w, p, r);
    double rotErr = std::fabs(std::fabs(r.w * r0.w + r.x * r0.x + r.y * r0.y + r.z * r0.z) - 1);
    check(dist3(p, p0) < 1e-12 && rotErr < 1e-12, "pose lead: undoes same lag", dist3(p, p0), 0);
}

// 세기 배율은 속도에만 곱해지고 손 위치/회전은 배율 1 과 같아야 한다
void testScaleDoesNotMoveHand() {
    constexpr double STRONG = 1.5;
    constexpr double LAG = 0.037;
    VdPose reported{{0.1, 1.2, -0.3}, yawQuat(0.4), {1.0, -2.0, 0.5}, {3.0, 1.0, -2.0}, {}};
    VdPoseConfig cfg;
    cfg.linearFrame = cfg.angularFrame = vdPitchFrame(VdFrameFixConfig::DEFAULT_PITCH_DEG);
    cfg.poseLagSec = LAG;
    VdPose base = fixVdPose(reported, cfg);
    cfg.linearScale = cfg.angularScale = STRONG;
    VdPose strong = fixVdPose(reported, cfg);
    double rotErr = std::fabs(std::fabs(base.rotation.w * strong.rotation.w + base.rotation.x * strong.rotation.x +
                                        base.rotation.y * strong.rotation.y + base.rotation.z * strong.rotation.z) - 1);
    check(dist3(base.position, strong.position) < 1e-12 && rotErr < 1e-12, "scale keeps hand pose", dist3(base.position, strong.position), 0);
    check(std::fabs(strong.linear.x - base.linear.x * STRONG) < 1e-12 && std::fabs(strong.angular.y - base.angular.y * STRONG) < 1e-12,
          "scale applies to velocity", strong.linear.x, base.linear.x * STRONG);
}

void testSpikeGuard() {
    constexpr double FAST = 150.0, NORMAL = 5.0;
    SpikeGuardConfig cfg;
    SpikeGuard guard;
    check(!guard.isSpike(cfg, 0.0, NORMAL), "spike guard: normal speed passes", 0, 0);
    check(guard.isSpike(cfg, 0.01, FAST), "spike guard: fast frame held", 0, 0);
    check(!guard.isSpike(cfg, 0.01 + cfg.maxHoldSec * 2, FAST), "spike guard: gives up after hold", 0, 0);
    check(!guard.isSpike(cfg, 1.0, NORMAL) && guard.isSpike(cfg, 1.01, FAST), "spike guard: rearms after normal frame", 0, 0);
    cfg.enabled = false;
    SpikeGuard off;
    check(!off.isSpike(cfg, 0.0, FAST), "spike guard: disabled", 0, 0);
}

// 최고 속도 한 프레임만 방향이 튀어도 앞뒤 평균 방향, 크기는 최고 속도
void testReleaseDirectionAveraged() {
    constexpr double STEP = 0.011;
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    boost.addVelocity(STEP, linearVelocity(4.0));
    boost.addVelocity(STEP * 2, {{5.0, 5.0, 0}, {}});  // 방향 튐, 약 7 m/s
    boost.addVelocity(STEP * 3, linearVelocity(4.0));
    boost.addVelocity(STEP * 4, linearVelocity(2.0));
    bool fired = boost.updateGrip(STEP * 4, GRIP_OPEN, ev);
    VelocityEstimate out;
    boost.override(STEP * 4, out);
    double speed = length3(out.linear.x, out.linear.y, out.linear.z);
    double spikeAngle = 45.0, angle = std::atan2(out.linear.y, out.linear.x) * 180.0 / 3.14159265358979323846;
    check(fired && angle < spikeAngle * 0.8, "release direction averaged", angle, 0);  // 빠를수록 구간이 짧아 덜 섞임
    check(std::fabs(speed - ev.peakSpeed) < 1e-9, "release keeps peak speed", speed, ev.peakSpeed);
}


// 그립 판정이 늦은 경우: 최고 속도 지점이 아니라 감속 직전 "아직 빨랐던 마지막 순간"의 세기와 방향을 쓴다
void testReleaseUsesLastFastMoment() {
    constexpr double STEP = 0.0154;
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    double t = STEP;
    boost.addVelocity(t, {{0, 8.0, 0}, {}});  // 최고 속도, 아직 위로 휘두르는 중
    t += STEP;
    boost.addVelocity(t, {{5.0, 5.0, 0}, {}});  // 약 7.1 m/s (0.88), 앞으로 꺾임
    t += STEP;
    boost.addVelocity(t, {{7.0, 0, 0}, {}});  // 7.0 m/s (0.875), 앞으로 = 실제로 놓은 순간
    t += STEP;
    boost.addVelocity(t, {{3.0, -2.0, 0}, {}});  // 감속하며 아래로 (그립 판정이 늦게 온 시점)
    bool fired = boost.updateGrip(t, GRIP_OPEN, ev);
    VelocityEstimate out;
    boost.override(t, out);
    double angle = std::atan2(out.linear.y, out.linear.x) * 180.0 / 3.14159265358979323846;
    constexpr double FORWARD_TOLERANCE_DEG = 25.0;  // 방향 평균에 앞 샘플이 조금 섞임
    check(fired && std::fabs(angle) < FORWARD_TOLERANCE_DEG, "release uses last fast moment direction", angle, 0);
    check(fired && std::fabs(ev.peakAgeSec - STEP) < 1e-9, "release looks back only to the last fast moment", ev.peakAgeSec, STEP);

    // 제때 놓은 경우: lookback 0 이면 그대로, 기본값이면 약 20 ms 앞 속도 (Steam Link 처럼)
    ReleaseBoostConfig noLookback = cfg;
    noLookback.minLookbackSec = 0;
    ReleaseBoost onTime;
    onTime.setConfig(noLookback);
    onTime.updateGrip(0.0, GRIP_HELD, ev);
    onTime.addVelocity(STEP, linearVelocity(8.0));
    onTime.addVelocity(STEP * 2, linearVelocity(7.5));
    check(!onTime.updateGrip(STEP * 2, GRIP_OPEN, ev), "release on time (no lookback) -> no override", ev.currentSpeed, 7.5);

    ReleaseBoost lookback;
    lookback.setConfig(cfg);
    lookback.updateGrip(0.0, GRIP_HELD, ev);
    lookback.addVelocity(STEP, {{8.0, 1.0, 0}, {}});      // 약 20 ms 전: 아직 조금 위로
    lookback.addVelocity(STEP * 2, {{7.5, -1.5, 0}, {}});  // 놓는 순간: 벌써 아래로 꺾임
    bool fired2 = lookback.updateGrip(STEP * 2, GRIP_OPEN, ev);
    lookback.override(STEP * 2, out);
    check(fired2 && out.linear.y > 0 && std::fabs(ev.peakAgeSec - STEP) < 1e-9, "release on time uses ~20 ms earlier velocity", ev.peakAgeSec, STEP);
}


// 방향 평균 구간은 속도에 반비례 (빠른 던지기는 곡선이 빨리 돌아서 짧게)
void testReleaseDirectionWindowShrinksWhenFast() {
    constexpr double STEP = 0.0154;
    ReleaseBoostConfig cfg;
    ReleaseEvent ev;
    VelocityEstimate out;
    // 9 m/s: 구간 ≈ 14 ms → 앞 샘플(15 ms 전, 45° 위)은 섞이지 않는다
    ReleaseBoost fast;
    fast.setConfig(cfg);
    fast.updateGrip(0.0, GRIP_HELD, ev);
    fast.addVelocity(STEP, {{6.4, 6.4, 0}, {}});
    fast.addVelocity(STEP * 2, linearVelocity(9.0));
    fast.addVelocity(STEP * 3, linearVelocity(3.0));
    fast.updateGrip(STEP * 3, GRIP_OPEN, ev);
    fast.override(STEP * 3, out);
    double fastAngle = std::atan2(out.linear.y, out.linear.x) * 180.0 / 3.14159265358979323846;
    constexpr double FAST_MAX_MIX_DEG = 5.0;  // 9 m/s: 45° 앞 샘플이 거의 안 섞임
    check(std::fabs(fastAngle) < FAST_MAX_MIX_DEG, "release dir window: fast throw mostly own direction", fastAngle, 0);
    // 3 m/s: 구간 30 ms → 앞 샘플도 평균에 들어간다
    ReleaseBoost slow;
    slow.setConfig(cfg);
    slow.updateGrip(0.0, GRIP_HELD, ev);
    slow.addVelocity(STEP, {{2.1, 2.1, 0}, {}});
    slow.addVelocity(STEP * 2, linearVelocity(3.0));
    slow.addVelocity(STEP * 3, linearVelocity(1.0));
    slow.updateGrip(STEP * 3, GRIP_OPEN, ev);
    slow.override(STEP * 3, out);
    double slowAngle = std::atan2(out.linear.y, out.linear.x) * 180.0 / 3.14159265358979323846;
    check(slowAngle > 2 * FAST_MAX_MIX_DEG, "release dir window: slow throw averages neighbors", slowAngle, 0);
}


// 한 프레임에 여러 예측 시각으로 조회 (시간이 뒤섞임) 해도 시간순 기록으로 판단한다
void testReleaseIgnoresOutOfOrderSamples() {
    constexpr double FRAME = 0.011;
    constexpr double AHEAD = 0.030;  // 미래 예측 조회
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    // 손 속도: 앞선 시각 조회와 현재 시각 조회는 같은 움직임 (현재 시각 쪽은 그만큼 과거 값)
    const double speeds[] = {2.0, 5.0, 8.0, 8.0, 7.5, 7.0, 4.0, 2.0, 1.0, 1.0, 1.0};
    constexpr int AHEAD_FRAMES = 3;  // 33 ms
    double t = 0;
    for (int i = 0; i < static_cast<int>(std::size(speeds)); ++i) {
        t += FRAME;
        boost.addVelocity(t + AHEAD_FRAMES * FRAME, linearVelocity(speeds[i]));
        int past = i - AHEAD_FRAMES;
        boost.addVelocity(t, linearVelocity(past >= 0 ? speeds[past] : speeds[0]));  // 뒤로 감
    }
    bool fired = boost.updateGrip(t + AHEAD_FRAMES * FRAME, GRIP_OPEN, ev);
    check(fired && ev.peakAgeSec <= cfg.maxPeakAgeSec, "release: out-of-order samples ignored", ev.peakAgeSec, cfg.maxPeakAgeSec);
}

void testReleaseMaxRatio() {
    constexpr double RATIO = 1.5;
    ReleaseBoost boost;
    ReleaseBoostConfig cfg;
    cfg.maxRatio = RATIO;
    boost.setConfig(cfg);
    ReleaseEvent ev;
    boost.updateGrip(0.0, GRIP_HELD, ev);
    boost.addVelocity(0.01, linearVelocity(PEAK_THROW_SPEED));
    boost.addVelocity(0.02, linearVelocity(LATE_RELEASE_SPEED));
    boost.updateGrip(0.02, GRIP_OPEN, ev);
    VelocityEstimate out;
    boost.override(0.02, out);
    double expected = std::fmax(LATE_RELEASE_SPEED * RATIO, cfg.minSpeed);
    check(std::fabs(length3(out.linear.x, out.linear.y, out.linear.z) - expected) < 1e-9, "release ratio limit", out.linear.x, expected);
}

void testDpad() {
    DpadConfig cfg;
    DpadState up = dpadFromStick(cfg, {0.1, 0.8});
    DpadState left = dpadFromStick(cfg, {-0.9, 0});
    DpadState idle = dpadFromStick(cfg, {0.2, -0.2});
    check(up.up && !up.down && !up.left && !up.right, "dpad up", 0, 0);
    check(left.left && !left.up && !left.down && !left.right, "dpad left", 0, 0);
    check(!idle.up && !idle.down && !idle.left && !idle.right, "dpad idle below threshold", 0, 0);
}

void testDpadGesture() {
    DpadConfig cfg;
    check(cfg.enabled && cfg.gesture, "dpad gesture on by default", 0, 0);
    double between = cfg.gestureDistance * (1 + DpadConfig::GESTURE_RELEASE_RATIO) / 2;
    check(dpadGestureHeld(cfg, false, cfg.gestureDistance - 0.01), "dpad gesture: near starts", 0, 0);
    check(!dpadGestureHeld(cfg, false, between), "dpad gesture: not started between", between, 0);
    check(dpadGestureHeld(cfg, true, between), "dpad gesture: stays held between", between, 0);
    check(!dpadGestureHeld(cfg, true, cfg.gestureDistance * 2), "dpad gesture: far releases", 0, 0);
}

void testVdHeadset() {
    const std::string gxr = VdHeadset::parseDeviceName(R"({ "CodecName": "HEVC", "DeviceName": "Samsung Galaxy XR" })");
    const std::string quest = VdHeadset::parseDeviceName("{\"DeviceName\":\"Meta Quest 3\"}");
    check(gxr == "Samsung Galaxy XR" && !VdHeadset::isQuestName(gxr) && VdHeadset::isGxrName(gxr), "vd headset: GXR", 0, 0);
    check(quest == "Meta Quest 3" && VdHeadset::isQuestName(quest) && !VdHeadset::isGxrName(quest), "vd headset: Quest", 0, 0);
    check(VdHeadset::parseDeviceName("{}").empty() && !VdHeadset::isQuestName(""), "vd headset: unknown keeps auto", 0, 0);
}

void testSettingsParse() {
    std::istringstream in(
        "; comment\nenabled=0\nlinearScale = 1.25\nvdFrameFix=auto\nvdAngularPitchDeg=16\nreleaseButton=trigger\n"
        "releaseDirWindowMs=30\nspikeMaxSpeed=abc\nspikeHoldMs=100\nunknownKey=1\ndpadTrigger=stick\ndpadGestureDist=0.3\n");
    Settings s;
    SettingsFile::parse(in, s);
    check(!s.enabled && s.estimator.linearScale == 1.25 && s.vdFix.mode == VdFixMode::Auto, "settings: basic keys", s.estimator.linearScale, 1.25);
    check(!s.dpad.gesture && s.dpad.gestureDistance == 0.3, "settings: dpad trigger", s.dpad.gestureDistance, 0.3);
    check(s.vdFix.angularPitchDeg == 16 && s.vdFix.pitchDeg == VdFrameFixConfig::DEFAULT_PITCH_DEG, "settings: angular pitch separate", s.vdFix.angularPitchDeg, 16);
    check(s.releaseBoost.button == ReleaseButton::Trigger && std::fabs(s.releaseBoost.directionWindowSec - 0.03) < 1e-12, "settings: release keys", s.releaseBoost.directionWindowSec, 0.03);
    check(s.spikeGuard.maxSpeed == SpikeGuardConfig::DEFAULT_MAX_SPEED && std::fabs(s.spikeGuard.maxHoldSec - 0.1) < 1e-12, "settings: bad value keeps default", s.spikeGuard.maxSpeed, SpikeGuardConfig::DEFAULT_MAX_SPEED);
}

void testVdFrameFit() {
    testVdFrameFitLargeOffsetNeverNormal();
    VdFrameFit gxr(FIT_PRIOR_PITCH_DEG), normal(FIT_PRIOR_PITCH_DEG);
    // 샘플 전에는 기본 공식 = SteamVR 쪽 보정과 같다
    Quat poseRotation = multiply(yawQuat(0.7), Quat{std::cos(0.3), std::sin(0.3), 0, 0});
    Vec3 reported{1.0, -2.0, 0.5};
    Vec3 a = gxr.apply(poseRotation, reported);
    Vec3 b = vdLocalToWorld(poseRotation, reported, FIT_PRIOR_PITCH_DEG);
    check(length3(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-9, "frame fit: default = steamvr formula", 0, 0);

    runFrameFit(true, gxr);
    check(gxr.verdict() == VdFrameFit::Verdict::Fix && gxr.refined(), "frame fit: GXR -> fix (fitted)", gxr.fitErrorDeg(), gxr.rawErrorDeg());
    runFrameFit(false, normal);
    check(normal.verdict() == VdFrameFit::Verdict::Normal, "frame fit: normal -> no fix", normal.fitErrorDeg(), normal.rawErrorDeg());
}


// 세션 틀어짐(X 15° + Y 6°)을 손 움직임만으로 찾는지. VD 처럼 틀린 속도로 포즈를 뒤로 외삽한 입력.
Quat eulerXY(double xDeg, double yDeg) {
    constexpr double D = 3.14159265358979323846 / 180.0;
    Quat qx{std::cos(xDeg * D / 2), std::sin(xDeg * D / 2), 0, 0};
    Quat qy{std::cos(yDeg * D / 2), 0, std::sin(yDeg * D / 2), 0};
    return multiply(qx, qy);
}

void testVdFrameCalibrator() {
    constexpr double DT = 0.0154;            // VD 공유 메모리 주기
    constexpr double LAG = 0.037;            // poseLagMs
    constexpr int STEPS = 400;               // 약 6초
    constexpr double TOLERANCE_DEG = 1.0;
    const Quat trueFrame = eulerXY(15.0, -6.0);  // B: local = B · reported
    VdFrameCalibrator cal;
    cal.setPrior(vdPitchFrame(VdFrameFixConfig::DEFAULT_PITCH_DEG));
    cal.setUndoLag(LAG);
    check(!cal.calibrated(), "calibrator: default before motion", 0, 0);
    auto expq = [](const Vec3& rv) {
        double n = std::sqrt(rv.x * rv.x + rv.y * rv.y + rv.z * rv.z);
        if (n < 1e-12) return Quat{};
        double s = std::sin(n / 2) / n;
        return Quat{std::cos(n / 2), rv.x * s, rv.y * s, rv.z * s};
    };
    auto logq = [](Quat q) {
        if (q.w < 0) q = {-q.w, -q.x, -q.y, -q.z};
        double s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
        if (s < 1e-12) return Vec3{};
        double k = 2 * std::atan2(s, q.w) / s;
        return Vec3{q.x * k, q.y * k, q.z * k};
    };
    // 팔을 흔들고 손목을 돌리는 움직임 (회전축과 이동 방향이 계속 바뀜)
    auto rotAt = [&](double t) { return expq({0.9 * std::sin(2.1 * t), 1.3 * std::sin(1.3 * t + 1), 0.6 * std::sin(3.1 * t)}); };
    auto posAt = [](double t) { return Vec3{0.4 * std::sin(1.7 * t), 0.3 * std::sin(2.3 * t), 0.35 * std::cos(1.1 * t)}; };
    constexpr double H = 1e-4;
    Vec3 prevV, prevW;  // 보고 값은 한 프레임 늦다
    for (int i = 0; i < STEPS; ++i) {
        double t = i * DT;
        Quat r = rotAt(t);
        Vec3 w = logq(multiply(rotAt(t + H), conjugate(rotAt(t - H))));
        w = {w.x / (2 * H), w.y / (2 * H), w.z / (2 * H)};
        Vec3 p0 = posAt(t - H), p1 = posAt(t + H), p = posAt(t);
        Vec3 v{(p1.x - p0.x) / (2 * H), (p1.y - p0.y) / (2 * H), (p1.z - p0.z) / (2 * H)};
        // 보고 값: reported = Bᵀ · Rᵀ · true (world 라벨 = R · reported)
        Quat toRep = conjugate(trueFrame);
        Vec3 vNow = rotate(toRep, rotate(conjugate(r), v));
        Vec3 wNow = rotate(toRep, rotate(conjugate(r), w));
        Vec3 vRep = i == 0 ? vNow : prevV, wRepLocal = i == 0 ? wNow : prevW;
        prevV = vNow;
        prevW = wNow;
        Vec3 wRepWorld = wRepLocal;  // VD 는 로컬 값을 world 라벨로 보낸다
        // VD 외삽: p - lag·v_rep, exp(-lag·ω_rep)·R
        Vec3 pd{p.x - LAG * vRep.x, p.y - LAG * vRep.y, p.z - LAG * vRep.z};
        Quat rd = multiply(expq({-LAG * wRepWorld.x, -LAG * wRepWorld.y, -LAG * wRepWorld.z}), r);
        cal.addSample(0, t, rd, pd, vRep, wRepWorld);
    }
    Quat f = cal.frame(), d = multiply(f, conjugate(trueFrame));
    double errDeg = 2 * std::acos(std::fmin(1.0, std::fabs(d.w))) * 180.0 / 3.14159265358979323846;
    check(cal.calibrated() && errDeg < TOLERANCE_DEG, "calibrator: finds X15/Y6 session frame", errDeg, 0);
    cal.reset();
    check(!cal.calibrated(), "calibrator: reset -> default", 0, 0);
}

}  // namespace

int main() {
    testVdFrameFixInverts();
    testVdFrameDetector();
    testVdFrameDetectorSticky();
    testVdFrameFit();
    testVdFrameCalibrator();
    testStickAndHaptic();
    testPoseLag();
    testPoseLead();
    testScaleDoesNotMoveHand();
    testSpikeGuard();
    testReleaseDirectionAveraged();
    testReleaseMaxRatio();
    testReleaseUsesLastFastMoment();
    testReleaseIgnoresOutOfOrderSamples();
    testReleaseDirectionWindowShrinksWhenFast();
    testDpad();
    testDpadGesture();
    testSettingsParse();
    testVdHeadset();
    testReleaseBoostUsesPeak();
    testReleaseBoostIgnoresOldPeakAndSlowMoves();
    testReleaseBoostIgnoresStalePeak();
    testSpikeRejected();
    testFreezeAndReacquire();
    testCircularMotion(AngularFrame::World);
    testCircularMotion(AngularFrame::Local);
    testLocalFrameDiffers();
    testJumpResets();
    testDuplicateIgnored();
    std::printf(g_failures ? "\n%d FAILED\n" : "\nALL PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
