#include "SpaceVelocityFixer.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>

#include "LayerLog.h"
#include "QuatMath.h"
#include "VdFrameFix.h"

namespace {

constexpr const char* DATA_RELATIVE_DIR = "WYGxrControllerFix";
constexpr const char* SETTINGS_FILE = "settings.ini";
constexpr const char* CAPTURE_DIR = "captures";
constexpr double NANOSECONDS_PER_SECOND = 1e9;
constexpr double MS_PER_SEC = 1000.0;
constexpr size_t NAME_BUFFER_SIZE = 64;
constexpr XrSpaceLocationFlags POSE_VALID = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
constexpr XrSpaceVelocityFlags VELOCITY_VALID = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;

constexpr const char* LEFT_HAND_PATH = "/user/hand/left";
constexpr const char* RIGHT_HAND_PATH = "/user/hand/right";
constexpr const char* SQUEEZE_PATH = "/input/squeeze";

// analyze_capture.py 와 같은 형식
constexpr const char* CAPTURE_HEADER =
    "t,device,role,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,vx,vy,vz,wx,wy,wz,grip,trigger\n";

std::filesystem::path dataDir() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    return std::filesystem::path(localAppData ? localAppData : ".") / DATA_RELATIVE_DIR;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

controllerfix::Vec3 toVec3(const XrVector3f& v) { return {v.x, v.y, v.z}; }

controllerfix::Vec3 scaled(const controllerfix::Vec3& v, double factor) { return {v.x * factor, v.y * factor, v.z * factor}; }

void assign(XrVector3f& out, const controllerfix::Vec3& v) {
    out.x = static_cast<float>(v.x);
    out.y = static_cast<float>(v.y);
    out.z = static_cast<float>(v.z);
}

// 쿼터니언 → 3x3 회전행렬 (행 우선)
void toMatrix(const XrQuaternionf& q, double m[3][3]) {
    double x = q.x, y = q.y, z = q.z, w = q.w;
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - z * w);     m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);     m[2][1] = 2 * (y * z + x * w);     m[2][2] = 1 - 2 * (x * x + y * y);
}

const char* vdFixModeName(controllerfix::VdFixMode mode) {
    switch (mode) {
    case controllerfix::VdFixMode::Off: return "off";
    case controllerfix::VdFixMode::On: return "on";
    default: return "auto";
    }
}

const char* verdictName(controllerfix::VdFrameFit::Verdict v) {
    switch (v) {
    case controllerfix::VdFrameFit::Verdict::Fix: return "GXR detected -> fix ON";
    case controllerfix::VdFrameFit::Verdict::Normal: return "not needed -> fix OFF";
    default: return "undecided";
    }
}

const char* handName(SpaceVelocityFixer::Hand hand) {
    switch (hand) {
    case SpaceVelocityFixer::Hand::Left: return "left";
    case SpaceVelocityFixer::Hand::Right: return "right";
    default: return "?";
    }
}

}  // namespace

SpaceVelocityFixer& SpaceVelocityFixer::I() {
    static SpaceVelocityFixer instance;
    return instance;
}

SpaceVelocityFixer::SpaceVelocityFixer() : m_settingsFile(dataDir() / SETTINGS_FILE) {}

SpaceVelocityFixer::Hand SpaceVelocityFixer::handOf(const std::string& path) {
    if (startsWith(path, LEFT_HAND_PATH)) return Hand::Left;
    if (startsWith(path, RIGHT_HAND_PATH)) return Hand::Right;
    return Hand::Unknown;
}

SpaceVelocityFixer::BindingKind SpaceVelocityFixer::kindOf(const std::string& bindingPath) {
    if (bindingPath.find(SQUEEZE_PATH) != std::string::npos) return BindingKind::Grip;
    return BindingKind::Other;
}

bool SpaceVelocityFixer::onInstanceCreated(const std::string& appName, const std::string& runtimeName) {
    std::lock_guard lock(m_mutex);
    m_settingsFile.ensureExists();
    m_settingsFile.reloadIfChanged(m_settings);
    applySettings();
    bool active = m_settings.runtimeMatch.empty() || runtimeName.find(m_settings.runtimeMatch) != std::string::npos;
    if (active) m_appName = appName;
    LayerLog("app='%s' runtime='%s' -> %s (openxrEnabled=%d enabled=%d vdFrameFix=%s capture=%d)", appName.c_str(),
             runtimeName.c_str(), active ? "ACTIVE" : "inactive (runtime mismatch)", m_settings.openxrEnabled,
             m_settings.enabled, vdFixModeName(m_settings.vdFix.mode), m_settings.openxrCapture);
    return active;
}

void SpaceVelocityFixer::onBinding(XrAction action, Hand hand, BindingKind kind) {
    std::lock_guard lock(m_mutex);
    if (hand != Hand::Unknown) m_actionHands[action].insert(hand);
    if (kind != BindingKind::Other) m_actionKinds[action] = kind;
}

void SpaceVelocityFixer::onActionSpaceCreated(XrSpace space, XrAction action, Hand hand) {
    std::lock_guard lock(m_mutex);
    m_actionSpaces[space] = {action, hand};
}

void SpaceVelocityFixer::onSpaceDestroyed(XrSpace space) {
    std::lock_guard lock(m_mutex);
    m_actionSpaces.erase(space);
    for (auto it = m_pairs.begin(); it != m_pairs.end();)
        it = (it->first.first == space || it->first.second == space) ? m_pairs.erase(it) : std::next(it);
    for (auto it = m_lastCaptureTime.begin(); it != m_lastCaptureTime.end();)
        it = (it->first.first == space || it->first.second == space) ? m_lastCaptureTime.erase(it) : std::next(it);
}

SpaceVelocityFixer::Hand SpaceVelocityFixer::resolveHand(XrAction action, Hand hand) const {
    if (hand != Hand::Unknown) return hand;
    auto it = m_actionHands.find(action);
    // 양손에 묶인 액션을 손 구분 없이 읽으면 어느 손인지 알 수 없다
    return (it != m_actionHands.end() && it->second.size() == 1) ? *it->second.begin() : Hand::Unknown;
}

SpaceVelocityFixer::Hand SpaceVelocityFixer::spaceHand(XrSpace space) const {
    auto it = m_actionSpaces.find(space);
    return it == m_actionSpaces.end() ? Hand::Unknown : resolveHand(it->second.action, it->second.hand);
}

void SpaceVelocityFixer::onActionState(XrAction action, Hand hand, double value) {
    std::lock_guard lock(m_mutex);
    auto kind = m_actionKinds.find(action);
    if (kind == m_actionKinds.end() || kind->second != BindingKind::Grip) return;
    if (!m_settings.enabled || !m_settings.openxrEnabled) return;
    Hand resolved = resolveHand(action, hand);
    if (resolved == Hand::Unknown) return;
    for (auto& [pair, ps] : m_pairs) {
        if (spaceHand(pair.first) != resolved) continue;
        controllerfix::ReleaseEvent ev;
        // 해제 시각은 이 공간의 최신 포즈 시각 기준 (앱은 예측 시각으로 포즈를 조회한다)
        if (ps.releaseBoost.updateGrip(ps.lastSampleSec, value, ev)) {
            ++m_boostCount;
            LayerLog("%s hand release boost: %.2f m/s -> %.2f m/s (peak %.0f ms before release)", handName(resolved),
                     ev.currentSpeed, ev.peakSpeed, ev.peakAgeSec * MS_PER_SEC);
        }
    }
}

void SpaceVelocityFixer::applySettings() {
    for (auto& [pair, ps] : m_pairs) {
        ps.releaseBoost.setConfig(m_settings.releaseBoost);
        ps.fit.setPriorPitch(m_settings.vdFix.pitchDeg);
    }
}

void SpaceVelocityFixer::pollSettings() {
    auto now = std::chrono::steady_clock::now();
    if (now < m_nextSettingsPoll) return;
    m_nextSettingsPoll = now + SETTINGS_POLL_INTERVAL;
    if (!m_settingsFile.reloadIfChanged(m_settings)) return;
    applySettings();
    if (!m_settings.openxrCapture) closeCapture();
    LayerLog("settings reloaded: enabled=%d openxrEnabled=%d vdFrameFix=%s capture=%d linearScale=%.2f releaseBoost=%d",
             m_settings.enabled, m_settings.openxrEnabled, vdFixModeName(m_settings.vdFix.mode), m_settings.openxrCapture,
             m_settings.estimator.linearScale, m_settings.releaseBoost.enabled);
}

void SpaceVelocityFixer::logStats() {
    auto now = std::chrono::steady_clock::now();
    if (now < m_nextStatsLog) return;
    m_nextStatsLog = now + STATS_LOG_INTERVAL;
    LayerLog("stats: actionSpaces=%zu locates=%llu velocityRequested=%llu replaced=%llu boosts=%llu", m_actionSpaces.size(),
             m_locateCount, m_velocityRequestCount, m_replacedCount, m_boostCount);
}

void SpaceVelocityFixer::logVerdict(XrSpace space, PairState& ps) {
    controllerfix::VdFrameFit::Verdict verdict = ps.fit.verdict();
    if (verdict == ps.loggedVerdict) return;
    ps.loggedVerdict = verdict;
    controllerfix::Vec3 axis = ps.fit.fixAxis();
    LayerLog("%s hand space %p: %s (direction error %.1f -> %.1f deg, %s rotation %.1f deg axis %.2f,%.2f,%.2f, %zu samples)",
             handName(spaceHand(space)), static_cast<void*>(space), verdictName(verdict), ps.fit.rawErrorDeg(),
             ps.fit.fitErrorDeg(), ps.fit.refined() ? "fitted" : "default", ps.fit.fixAngleDeg(), axis.x, axis.y, axis.z,
             ps.fit.sampleCount());
}

void SpaceVelocityFixer::closeCapture() {
    if (!m_captureFile) return;
    std::fclose(m_captureFile);
    m_captureFile = nullptr;
    LayerLog("capture closed");
}

// 앱 요청과 별개로 런타임에 속도를 직접 요청해서 원래 값을 기록한다.
void SpaceVelocityFixer::capture(InstanceDispatch& d, XrSpace space, XrSpace baseSpace, XrTime time) {
    SpacePair key{space, baseSpace};
    auto last = m_lastCaptureTime.find(key);
    if (last != m_lastCaptureTime.end() && time <= last->second) return;  // 같은 프레임 중복 조회
    m_lastCaptureTime[key] = time;

    if (!m_captureFile) {
        char stamp[NAME_BUFFER_SIZE];
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        std::strftime(stamp, sizeof(stamp), "openxr_%Y%m%d_%H%M%S.csv", &local);
        std::error_code ec;
        std::filesystem::path dir = dataDir() / CAPTURE_DIR;
        std::filesystem::create_directories(dir, ec);
        std::filesystem::path path = dir / stamp;
        if (_wfopen_s(&m_captureFile, path.c_str(), L"w") != 0 || !m_captureFile) return;
        std::fputs(CAPTURE_HEADER, m_captureFile);
        m_captureStartTime = time;
        LayerLog("capture started: %s", path.string().c_str());
    }

    XrSpaceVelocity vel{XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION, &vel};
    if (XR_FAILED(d.locateSpace(space, baseSpace, time, &loc)) || (loc.locationFlags & POSE_VALID) != POSE_VALID) return;
    double m[3][3];
    toMatrix(loc.pose.orientation, m);
    const XrVector3f& p = loc.pose.position;
    auto deviceId = static_cast<unsigned>(std::distance(m_actionSpaces.begin(), m_actionSpaces.find(space)));
    auto baseIndex = m_baseSpaceIndex.emplace(baseSpace, m_baseSpaceIndex.size()).first->second;
    std::fprintf(m_captureFile, "%.6f,%u,base%zu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,0,0\n",
                 static_cast<double>(time - m_captureStartTime) / NANOSECONDS_PER_SECOND, deviceId, baseIndex,
                 m[0][0], m[0][1], m[0][2], p.x, m[1][0], m[1][1], m[1][2], p.y, m[2][0], m[2][1], m[2][2], p.z,
                 vel.linearVelocity.x, vel.linearVelocity.y, vel.linearVelocity.z,
                 vel.angularVelocity.x, vel.angularVelocity.y, vel.angularVelocity.z);
}

controllerfix::Vec3 SpaceVelocityFixer::fixVelocity(PairState& ps, const controllerfix::Quat& rotation,
                                                    const controllerfix::Vec3& reported) const {
    if (m_settings.vdFix.mode == controllerfix::VdFixMode::On)
        return controllerfix::vdLocalToWorld(rotation, reported, m_settings.vdFix.pitchDeg);
    return ps.fit.apply(rotation, reported);
}

void SpaceVelocityFixer::process(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time,
                                 XrSpaceLocationFlags locationFlags, const XrPosef& pose,
                                 XrSpaceVelocityFlags* velocityFlags, XrVector3f* linear, XrVector3f* angular) {
    std::lock_guard lock(m_mutex);
    pollSettings();
    logStats();
    if (m_actionSpaces.count(space) == 0) return;
    if (m_settings.openxrCapture) capture(dispatch, space, baseSpace, time);
    if (!m_settings.enabled || !m_settings.openxrEnabled) return;
    ++m_locateCount;

    auto [pairIt, inserted] = m_pairs.try_emplace({space, baseSpace});
    PairState& ps = pairIt->second;
    if (inserted) {
        ps.releaseBoost.setConfig(m_settings.releaseBoost);
        ps.fit.setPriorPitch(m_settings.vdFix.pitchDeg);
    }
    controllerfix::EstimatorConfig cfg = m_settings.estimator;
    cfg.angularFrame = controllerfix::AngularFrame::World;  // OpenXR 각속도는 항상 baseSpace 기준
    ps.estimator.setConfig(cfg);

    if ((locationFlags & POSE_VALID) != POSE_VALID) {
        ps.estimator.reset();
        return;
    }
    controllerfix::PoseSample sample;
    sample.timeSec = static_cast<double>(time) / NANOSECONDS_PER_SECOND;
    sample.position = toVec3(pose.position);
    sample.rotation = {pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z};
    ps.estimator.addSample(sample);
    ps.lastSampleSec = sample.timeSec;

    if (!velocityFlags) return;
    ++m_velocityRequestCount;
    bool runtimeLinearValid = (*velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
    bool runtimeAngularValid = (*velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;

    controllerfix::VelocityEstimate out{toVec3(*linear), toVec3(*angular)};
    if (m_settings.vdFix.mode == controllerfix::VdFixMode::Off) {
        // 추정 방식: 느릴 때는 런타임 값, 빠를 때는 포즈 기록 추정값
        controllerfix::VelocityEstimate e;
        if (!ps.estimator.estimate(e)) return;
        controllerfix::BlendWeights w{1.0, 1.0};
        if (m_settings.blendEnabled) w = controllerfix::computeBlendWeights(e, m_settings.blend);
        if (m_settings.replaceLinear) out.linear = runtimeLinearValid ? controllerfix::lerp(out.linear, e.linear, w.linear) : e.linear;
        if (m_settings.replaceAngular) out.angular = runtimeAngularValid ? controllerfix::lerp(out.angular, e.angular, w.angular) : e.angular;
    } else {
        if (!runtimeLinearValid) return;
        if (m_settings.vdFix.mode == controllerfix::VdFixMode::Auto) {
            controllerfix::VelocityEstimate measured;
            if (ps.estimator.estimate(measured)) ps.fit.addSample(sample.timeSec, sample.rotation, measured.linear, out.linear);
            logVerdict(space, ps);
            // 판정 전에는 기본 공식으로 적용한다 (설치 대상이 GXR 사용자). 필요 없는 기기로 판정되면 원래 값.
            if (ps.fit.verdict() == controllerfix::VdFrameFit::Verdict::Normal) return;
        }
        out.linear = scaled(fixVelocity(ps, sample.rotation, out.linear), m_settings.estimator.linearScale);
        // 각속도는 한 번 더 로컬 기준으로 들어온다: ω = R · B · R · ω_reported (SteamVR 드라이버 각속도와 같은 형태)
        if (runtimeAngularValid)
            out.angular = scaled(fixVelocity(ps, sample.rotation, controllerfix::rotate(sample.rotation, out.angular)),
                                 m_settings.estimator.angularScale);
    }

    ps.releaseBoost.addVelocity(sample.timeSec, out);
    controllerfix::VelocityEstimate held;
    if (ps.releaseBoost.override(sample.timeSec, held)) out = held;  // 그립 해제 직후: 직전 최고 속도 유지

    assign(*linear, out.linear);
    assign(*angular, out.angular);
    *velocityFlags |= VELOCITY_VALID;
    ++m_replacedCount;
}
