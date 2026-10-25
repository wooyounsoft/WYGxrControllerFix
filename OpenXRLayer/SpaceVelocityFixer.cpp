#include "SpaceVelocityFixer.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>

#include "LayerLog.h"

namespace {

constexpr const char* DATA_RELATIVE_DIR = "WYGxrControllerFix";
constexpr const char* SETTINGS_FILE = "settings.ini";
constexpr const char* CAPTURE_DIR = "captures";
constexpr double NANOSECONDS_PER_SECOND = 1e9;
constexpr size_t NAME_BUFFER_SIZE = 64;
constexpr XrSpaceLocationFlags POSE_VALID = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
constexpr XrSpaceVelocityFlags VELOCITY_VALID = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;

// analyze_capture.py 와 같은 형식
constexpr const char* CAPTURE_HEADER =
    "t,device,role,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,vx,vy,vz,wx,wy,wz,grip,trigger\n";

std::filesystem::path dataDir() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    return std::filesystem::path(localAppData ? localAppData : ".") / DATA_RELATIVE_DIR;
}

controllerfix::Vec3 toVec3(const XrVector3f& v) { return {v.x, v.y, v.z}; }

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

}  // namespace

SpaceVelocityFixer& SpaceVelocityFixer::I() {
    static SpaceVelocityFixer instance;
    return instance;
}

SpaceVelocityFixer::SpaceVelocityFixer() : m_settingsFile(dataDir() / SETTINGS_FILE) {}

bool SpaceVelocityFixer::onInstanceCreated(const std::string& appName, const std::string& runtimeName) {
    std::lock_guard lock(m_mutex);
    m_settingsFile.ensureExists();
    m_settingsFile.reloadIfChanged(m_settings);
    bool active = m_settings.runtimeMatch.empty() || runtimeName.find(m_settings.runtimeMatch) != std::string::npos;
    if (active) m_appName = appName;
    LayerLog("app='%s' runtime='%s' -> %s (openxrEnabled=%d enabled=%d capture=%d)", appName.c_str(), runtimeName.c_str(),
             active ? "ACTIVE" : "inactive (runtime mismatch)", m_settings.openxrEnabled, m_settings.enabled,
             m_settings.openxrCapture);
    return active;
}

void SpaceVelocityFixer::onActionSpaceCreated(XrSpace space) {
    std::lock_guard lock(m_mutex);
    m_actionSpaces.insert(space);
}

void SpaceVelocityFixer::onSpaceDestroyed(XrSpace space) {
    std::lock_guard lock(m_mutex);
    m_actionSpaces.erase(space);
    for (auto it = m_estimators.begin(); it != m_estimators.end();)
        it = (it->first.first == space || it->first.second == space) ? m_estimators.erase(it) : std::next(it);
    for (auto it = m_lastCaptureTime.begin(); it != m_lastCaptureTime.end();)
        it = (it->first.first == space || it->first.second == space) ? m_lastCaptureTime.erase(it) : std::next(it);
}

void SpaceVelocityFixer::pollSettings() {
    auto now = std::chrono::steady_clock::now();
    if (now < m_nextSettingsPoll) return;
    m_nextSettingsPoll = now + SETTINGS_POLL_INTERVAL;
    if (!m_settingsFile.reloadIfChanged(m_settings)) return;
    for (auto& [pair, estimator] : m_estimators) estimator.reset();
    if (!m_settings.openxrCapture) closeCapture();
    LayerLog("settings reloaded: enabled=%d openxrEnabled=%d capture=%d blend=%d linearScale=%.2f", m_settings.enabled,
             m_settings.openxrEnabled, m_settings.openxrCapture, m_settings.blendEnabled, m_settings.estimator.linearScale);
}

void SpaceVelocityFixer::logStats() {
    auto now = std::chrono::steady_clock::now();
    if (now < m_nextStatsLog) return;
    m_nextStatsLog = now + STATS_LOG_INTERVAL;
    LayerLog("stats: actionSpaces=%zu locates=%llu velocityRequested=%llu replaced=%llu", m_actionSpaces.size(),
             m_locateCount, m_velocityRequestCount, m_replacedCount);
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
    std::fprintf(m_captureFile, "%.6f,%u,space,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,0,0\n",
                 static_cast<double>(time - m_captureStartTime) / NANOSECONDS_PER_SECOND, deviceId,
                 m[0][0], m[0][1], m[0][2], p.x, m[1][0], m[1][1], m[1][2], p.y, m[2][0], m[2][1], m[2][2], p.z,
                 vel.linearVelocity.x, vel.linearVelocity.y, vel.linearVelocity.z,
                 vel.angularVelocity.x, vel.angularVelocity.y, vel.angularVelocity.z);
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

    controllerfix::VelocityEstimator& est = m_estimators[{space, baseSpace}];
    controllerfix::EstimatorConfig cfg = m_settings.estimator;
    cfg.angularFrame = controllerfix::AngularFrame::World;  // OpenXR 각속도는 항상 baseSpace 기준
    est.setConfig(cfg);

    if ((locationFlags & POSE_VALID) != POSE_VALID) {
        est.reset();
        return;
    }
    controllerfix::PoseSample sample;
    sample.timeSec = static_cast<double>(time) / NANOSECONDS_PER_SECOND;
    sample.position = toVec3(pose.position);
    sample.rotation = {pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z};
    est.addSample(sample);

    if (!velocityFlags) return;
    ++m_velocityRequestCount;
    controllerfix::VelocityEstimate e;
    if (!est.estimate(e)) return;

    // 런타임 속도가 유효하면 느린 구간에서는 그 값을 섞어 떨림을 막는다.
    controllerfix::BlendWeights w{1.0, 1.0};
    bool runtimeLinearValid = (*velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
    bool runtimeAngularValid = (*velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
    if (m_settings.blendEnabled) w = controllerfix::computeBlendWeights(e, m_settings.blend);
    if (m_settings.replaceLinear)
        assign(*linear, runtimeLinearValid ? controllerfix::lerp(toVec3(*linear), e.linear, w.linear) : e.linear);
    if (m_settings.replaceAngular)
        assign(*angular, runtimeAngularValid ? controllerfix::lerp(toVec3(*angular), e.angular, w.angular) : e.angular);
    *velocityFlags |= VELOCITY_VALID;
    ++m_replacedCount;
}
