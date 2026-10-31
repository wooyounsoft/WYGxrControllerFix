#include "SpaceVelocityFixer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include "LayerLog.h"
#include "QuatMath.h"
#include "VdFrameFix.h"
#include "VirtualPad.h"

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
constexpr const char* TRIGGER_PATH = "/input/trigger";
constexpr const char* TOUCH_SUFFIX = "/touch";
constexpr const char* THUMBSTICK_PATH = "/input/thumbstick";
constexpr const char* THUMBSTICK_CLICK_PATH = "/input/thumbstick/click";
constexpr const char* HAPTIC_PATH = "/output/haptic";

enum DpadDirection { DpadUp, DpadDown, DpadLeft, DpadRight };
constexpr struct { const char* name; DpadDirection direction; } DPAD_ACTIONS[] = {
    {"dpad_up", DpadUp}, {"dpad_down", DpadDown}, {"dpad_left", DpadLeft}, {"dpad_right", DpadRight}};

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
    if (bindingPath.find(TRIGGER_PATH) != std::string::npos && bindingPath.find(TOUCH_SUFFIX) == std::string::npos) return BindingKind::Trigger;
    if (bindingPath.find(HAPTIC_PATH) != std::string::npos) return BindingKind::Haptic;
    if (bindingPath.find(THUMBSTICK_CLICK_PATH) != std::string::npos) return BindingKind::StickClick;
    // 2축 스틱 자체 (".../thumbstick/touch" 같은 하위 경로는 제외)
    size_t stick = bindingPath.rfind(THUMBSTICK_PATH);
    if (stick != std::string::npos && stick + std::string(THUMBSTICK_PATH).size() == bindingPath.size()) return BindingKind::Stick;
    return BindingKind::Other;
}

bool SpaceVelocityFixer::onInstanceCreated(const std::string& appName, const std::string& runtimeName) {
    std::lock_guard lock(m_mutex);
    VirtualPad::I().setLogger([](const char* message) { LayerLog("%s", message); });
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
    if (kind == BindingKind::Haptic && hand == Hand::Left) m_leftHapticAction = action;
    if (kind != BindingKind::Other) m_actionKinds[action] = kind;
}

void SpaceVelocityFixer::onActionSpaceCreated(XrSpace space, XrSession session, XrAction action, Hand hand) {
    std::lock_guard lock(m_mutex);
    m_actionSpaces[space] = {session, action, hand};
}

void SpaceVelocityFixer::onStickState(XrAction action, Hand hand, XrVector2f& value) {
    std::lock_guard lock(m_mutex);
    if (!m_settings.enabled) return;
    auto kind = m_actionKinds.find(action);
    if (kind == m_actionKinds.end() || kind->second != BindingKind::Stick) return;
    if (dpadActive() && m_dpadHeld && resolveHand(action, hand) == Hand::Left) {
        m_dpadStick = {value.x, value.y};
        if (m_dpadActions.empty()) {  // 게임에 D-pad 입력이 없으면 Xbox 패드로
            controllerfix::DpadState dpad = controllerfix::dpadFromStick(m_settings.dpad, m_dpadStick);
            VirtualPad::I().setDpad(dpad.up, dpad.down, dpad.left, dpad.right);
        }
        value = {0, 0};  // D-pad 로 쓰는 동안 게임에는 스틱을 놓은 것으로 보낸다
        return;
    }
    if (!m_settings.stick.enabled) return;
    controllerfix::Vec2 shaped = controllerfix::shapeStick(m_settings.stick, {value.x, value.y});
    value.x = static_cast<float>(shaped.x);
    value.y = static_cast<float>(shaped.y);
}

void SpaceVelocityFixer::onActionCreated(XrAction action, const std::string& name) {
    std::lock_guard lock(m_mutex);
    for (const auto& entry : DPAD_ACTIONS) {
        if (name != entry.name) continue;
        m_dpadActions[action] = entry.direction;
        LayerLog("game D-pad action: %s", entry.name);
    }
}

bool SpaceVelocityFixer::dpadActionState(XrAction action, bool& pressed) {
    std::lock_guard lock(m_mutex);
    auto it = m_dpadActions.find(action);
    if (it == m_dpadActions.end() || !dpadActive() || !m_dpadHeld) return false;
    controllerfix::DpadState dpad = controllerfix::dpadFromStick(m_settings.dpad, m_dpadStick);
    switch (it->second) {
    case DpadUp: pressed = dpad.up; break;
    case DpadDown: pressed = dpad.down; break;
    case DpadLeft: pressed = dpad.left; break;
    default: pressed = dpad.right; break;
    }
    return true;
}

void SpaceVelocityFixer::setDpadAllowed(bool allowed) {
    std::lock_guard lock(m_mutex);
    m_dpadAllowed = allowed;
    if (!allowed) LayerLog("D-pad off: UEVR has its own");
}

void SpaceVelocityFixer::onSessionDestroyed(XrSession session) {
    std::lock_guard lock(m_mutex);
    m_viewSpaces.erase(session);
}

void SpaceVelocityFixer::setDpadHeld(bool held) {
    if (held == m_dpadHeld) return;
    m_dpadHeld = held;
    m_dpadStick = {};
    m_dpadHapticPending = held && m_settings.dpad.haptic;
    if (!held) VirtualPad::I().setDpad(false, false, false, false);
    LayerLog("D-pad %s", held ? "on" : "off");
}

// 게임의 왼손 진동 액션으로 짧게 울린다. 액션이 없는 게임은 진동 없이 넘어간다.
void SpaceVelocityFixer::pulseDpadHaptic(InstanceDispatch& dispatch, XrSpace space) {
    if (!m_dpadHapticPending || m_leftHapticAction == XR_NULL_HANDLE || spaceHand(space) != Hand::Left) return;
    m_dpadHapticPending = false;
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = static_cast<XrDuration>(controllerfix::DpadConfig::HAPTIC_DURATION_SEC * NANOSECONDS_PER_SECOND);
    vibration.frequency = static_cast<float>(controllerfix::DpadConfig::HAPTIC_FREQUENCY_HZ);
    vibration.amplitude = static_cast<float>(controllerfix::DpadConfig::HAPTIC_AMPLITUDE);
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = m_leftHapticAction;
    XrPath leftHand = XR_NULL_PATH;
    if (dispatch.stringToPath) dispatch.stringToPath(dispatch.instance, LEFT_HAND_PATH, &leftHand);
    info.subactionPath = leftHand;
    XrSession session = m_actionSpaces[space].session;
    auto* haptic = reinterpret_cast<const XrHapticBaseHeader*>(&vibration);
    if (XR_SUCCEEDED(dispatch.applyHapticFeedback(session, &info, haptic))) return;
    info.subactionPath = XR_NULL_PATH;  // 손 구분 없는 액션
    dispatch.applyHapticFeedback(session, &info, haptic);
}

void SpaceVelocityFixer::updateDpadGesture(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time,
                                           const controllerfix::Vec3& hand) {
    if (!dpadActive() || !m_settings.dpad.gesture || spaceHand(space) != Hand::Left) return;
    XrSession session = m_actionSpaces[space].session;
    auto view = m_viewSpaces.find(session);
    if (view == m_viewSpaces.end()) {
        XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        info.poseInReferenceSpace.orientation.w = 1;
        XrSpace created = XR_NULL_HANDLE;
        if (XR_FAILED(dispatch.createReferenceSpace(session, &info, &created))) return;
        view = m_viewSpaces.emplace(session, created).first;
    }
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(dispatch.locateSpace(view->second, baseSpace, time, &head)) || (head.locationFlags & POSE_VALID) != POSE_VALID) return;
    double dx = hand.x - head.pose.position.x, dy = hand.y - head.pose.position.y, dz = hand.z - head.pose.position.z;
    setDpadHeld(controllerfix::dpadGestureHeld(m_settings.dpad, m_dpadHeld, std::sqrt(dx * dx + dy * dy + dz * dz)));
}

void SpaceVelocityFixer::tuneHaptic(XrHapticVibration& vibration) {
    std::lock_guard lock(m_mutex);
    if (!m_settings.enabled || !m_settings.haptic.enabled) return;
    double amplitude = vibration.amplitude;
    double durationSec = vibration.duration > 0 ? static_cast<double>(vibration.duration) / NANOSECONDS_PER_SECOND : 0;
    controllerfix::tuneHaptic(m_settings.haptic, amplitude, durationSec);
    vibration.amplitude = static_cast<float>(amplitude);
    if (vibration.duration > 0) vibration.duration = static_cast<XrDuration>(durationSec * NANOSECONDS_PER_SECOND);
    ++m_hapticCount;
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
    if (kind != m_actionKinds.end() && kind->second == BindingKind::StickClick) {
        if (dpadActive() && !m_settings.dpad.gesture && resolveHand(action, hand) == Hand::Right) setDpadHeld(value > 0);
        return;
    }
    BindingKind wanted = m_settings.releaseBoost.button == controllerfix::ReleaseButton::Trigger ? BindingKind::Trigger : BindingKind::Grip;
    if (kind == m_actionKinds.end() || kind->second != wanted) return;
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
    LayerLog("stats: actionSpaces=%zu locates=%llu velocityRequested=%llu replaced=%llu boosts=%llu haptics=%llu",
             m_actionSpaces.size(), m_locateCount, m_velocityRequestCount, m_replacedCount, m_boostCount, m_hapticCount);
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

bool SpaceVelocityFixer::fixVelocities(PairState& ps, XrSpace space, const controllerfix::PoseSample& sample,
                                       XrSpaceVelocityFlags flags, controllerfix::VelocityEstimate& out, bool& fixActive) {
    fixActive = false;
    bool runtimeLinearValid = (flags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
    bool runtimeAngularValid = (flags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
    if (m_settings.vdFix.mode == controllerfix::VdFixMode::Off) {
        // 추정 방식: 느릴 때는 런타임 값, 빠를 때는 포즈 기록 추정값
        controllerfix::VelocityEstimate e;
        if (!ps.estimator.estimate(e)) return false;
        controllerfix::BlendWeights w{1.0, 1.0};
        if (m_settings.blendEnabled) w = controllerfix::computeBlendWeights(e, m_settings.blend);
        if (m_settings.replaceLinear) out.linear = runtimeLinearValid ? controllerfix::lerp(out.linear, e.linear, w.linear) : e.linear;
        if (m_settings.replaceAngular) out.angular = runtimeAngularValid ? controllerfix::lerp(out.angular, e.angular, w.angular) : e.angular;
        fixActive = true;
        return true;
    }
    if (!runtimeLinearValid) return false;
    if (m_settings.vdFix.mode == controllerfix::VdFixMode::Auto) {
        controllerfix::VelocityEstimate measured;
        if (ps.estimator.estimate(measured)) ps.fit.addSample(sample.timeSec, sample.rotation, measured.linear, out.linear);
        logVerdict(space, ps);
        // 판정 전에는 기본 공식으로 적용한다 (설치 대상이 GXR 사용자). 필요 없는 기기로 판정되면 원래 값.
        if (ps.fit.verdict() == controllerfix::VdFrameFit::Verdict::Normal) return false;
    }
    out.linear = scaled(fixVelocity(ps, sample.rotation, out.linear), m_settings.estimator.linearScale);
    // 각속도는 한 번 더 로컬 기준으로 들어온다: ω = R · B · R · ω_reported (SteamVR 드라이버 각속도와 같은 형태)
    if (runtimeAngularValid)
        out.angular = scaled(fixVelocity(ps, sample.rotation, controllerfix::rotate(sample.rotation, out.angular)),
                             m_settings.estimator.angularScale);
    fixActive = true;
    return true;
}

void SpaceVelocityFixer::process(InstanceDispatch& dispatch, XrSpace space, XrSpace baseSpace, XrTime time,
                                 XrSpaceLocationFlags locationFlags, XrPosef& pose,
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
        ps.hasLastOutput = false;
        return;
    }
    controllerfix::PoseSample sample;
    sample.timeSec = static_cast<double>(time) / NANOSECONDS_PER_SECOND;
    // 추적 튐: 추정/판별/놓기 기록에 넣지 않고 마지막 보정 속도를 유지한다
    if (velocityFlags && ps.hasLastOutput) {
        double speed = std::sqrt(linear->x * linear->x + linear->y * linear->y + linear->z * linear->z);
        if (ps.spikeGuard.isSpike(m_settings.spikeGuard, sample.timeSec, speed)) {
            *linear = ps.lastLinear;
            *angular = ps.lastAngular;
            return;
        }
    }
    sample.position = toVec3(pose.position);
    sample.rotation = {pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z};
    ps.estimator.addSample(sample);
    // 게임이 여러 예측 시각으로 조회하므로 마지막 조회가 아니라 가장 늦은 시각을 해제 기준으로 쓴다
    ps.lastSampleSec = std::max(ps.lastSampleSec, sample.timeSec);
    updateDpadGesture(dispatch, space, baseSpace, time, sample.position);
    pulseDpadHaptic(dispatch, space);

    // 1. 속도 보정
    controllerfix::VelocityEstimate out;
    bool velocityChanged = false;
    bool fixActive = false;
    if (velocityFlags) {
        ++m_velocityRequestCount;
        out = {toVec3(*linear), toVec3(*angular)};
        velocityChanged = fixVelocities(ps, space, sample, *velocityFlags, out, fixActive);
    }

    // 2. 그립 해제 직후: 직전 최고 속도 유지
    if (velocityFlags && fixActive) {
        ps.releaseBoost.addVelocity(sample.timeSec, out);
        controllerfix::VelocityEstimate held;
        if (ps.releaseBoost.override(sample.timeSec, held)) out = held;
    }

    // 4. 컨트롤러 각도 조정 (손 기준 회전, 위치/속도는 그대로)
    if (m_settings.angleOffset.enabled) {
        controllerfix::Quat q = controllerfix::multiply(sample.rotation, controllerfix::angleOffsetRotation(m_settings.angleOffset));
        pose.orientation = {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
    }

    if (!velocityFlags || !velocityChanged) return;
    assign(*linear, out.linear);
    assign(*angular, out.angular);
    *velocityFlags |= VELOCITY_VALID;
    ps.lastLinear = *linear;
    ps.lastAngular = *angular;
    ps.hasLastOutput = true;
    ++m_replacedCount;
}
