#include "PoseFixer.h"

#include <cmath>
#include <cstdlib>
#include <string>

#include "DriverLog.h"
#include "QuatMath.h"
#include "VdFrameFix.h"
#include "VdShmDump.h"

namespace {

constexpr const char* SETTINGS_RELATIVE_PATH = "WYGxrControllerFix\\settings.ini";

constexpr double FULL_WEIGHT = 1.0;
constexpr double RAD_TO_DEG = 57.29577951308232;
constexpr double QUAT_ANGLE_FACTOR = 2.0;
constexpr double PERCENT = 100.0;  // 회전각 = 2·acos(|w|)

std::filesystem::path settingsPath() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    return std::filesystem::path(localAppData ? localAppData : ".") / SETTINGS_RELATIVE_PATH;
}

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// velocity = lerp(원래, 추정, w). 가속도는 추정값과 맞지 않으므로 같은 비율로 줄인다.
void blendInto(double velocity[3], double acceleration[3], const controllerfix::Vec3& estimated, double weight) {
    controllerfix::Vec3 v = controllerfix::lerp({velocity[0], velocity[1], velocity[2]}, estimated, weight);
    velocity[0] = v.x;
    velocity[1] = v.y;
    velocity[2] = v.z;
    for (int i = 0; i < 3; ++i) acceleration[i] *= 1.0 - weight;
}

controllerfix::Vec3 toVec3(const double v[3]) { return {v[0], v[1], v[2]}; }

const char* vdFixModeName(controllerfix::VdFixMode mode) {
    switch (mode) {
    case controllerfix::VdFixMode::Off: return "off";
    case controllerfix::VdFixMode::On: return "on";
    default: return "auto";
    }
}

const char* verdictName(controllerfix::VdFrameDetector::Verdict v) {
    switch (v) {
    case controllerfix::VdFrameDetector::Verdict::Gxr: return "GXR detected -> fix ON";
    case controllerfix::VdFrameDetector::Verdict::Normal: return "not needed -> fix OFF";
    default: return "undecided";
    }
}

controllerfix::Vec3 scaled(const controllerfix::Vec3& v, double factor) { return {v.x * factor, v.y * factor, v.z * factor}; }

void store(double out[3], const controllerfix::Vec3& v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

}  // namespace

PoseFixer& PoseFixer::I() {
    static PoseFixer instance;
    return instance;
}

PoseFixer::PoseFixer() : m_settingsFile(settingsPath()) {}

void PoseFixer::initSettings() {
    std::lock_guard lock(m_mutex);
    m_settingsFile.ensureExists();
    m_settingsFile.reloadIfChanged(m_settings);
    applySettings();
    DriverLog("settings: %s", m_settingsFile.path().string().c_str());
}

void PoseFixer::applySettings() {
    for (DeviceState& d : m_devices) {
        d.estimator.setConfig(m_settings.estimator);
        d.releaseBoost.setConfig(m_settings.releaseBoost);
        d.kind = DeviceKind::Unknown;  // modelMatch가 바뀌었을 수 있음
    }
    DriverLog("enabled=%d linear=%d angular=%d windowMs=%.0f linearScale=%.2f angularScale=%.2f frame=%s model=%s hmd=%s",
              m_settings.enabled, m_settings.replaceLinear, m_settings.replaceAngular,
              m_settings.estimator.windowSec * 1000.0, m_settings.estimator.linearScale,
              m_settings.estimator.angularScale,
              m_settings.estimator.angularFrame == controllerfix::AngularFrame::Local ? "local" : "world",
              m_settings.modelMatch.c_str(), m_settings.hmdMatch.c_str());
    DriverLog("blend=%d linear %.2f~%.2f m/s angular %.2f~%.2f rad/s", m_settings.blendEnabled,
              m_settings.blend.linearLow, m_settings.blend.linearHigh, m_settings.blend.angularLow,
              m_settings.blend.angularHigh);
    VdShmDump::setEnabled(m_settings.vdDump);
    DriverLog("vdFrameFix=%s pitch %.1f deg", vdFixModeName(m_settings.vdFix.mode), m_settings.vdFix.pitchDeg);
    DriverLog("releaseBoost=%d peak %.0f ms hold %.0f ms minSpeed %.2f grip %.2f/%.2f", m_settings.releaseBoost.enabled,
              m_settings.releaseBoost.peakWindowSec * 1000.0, m_settings.releaseBoost.holdSec * 1000.0,
              m_settings.releaseBoost.minSpeed, m_settings.releaseBoost.gripPress, m_settings.releaseBoost.gripRelease);
}

void PoseFixer::tick() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    if (now >= m_nextSettingsPoll) {
        m_nextSettingsPoll = now + SETTINGS_POLL_INTERVAL;
        if (m_settingsFile.reloadIfChanged(m_settings)) applySettings();
    }
    if (now >= m_nextStatsLog) {
        m_nextStatsLog = now + STATS_LOG_INTERVAL;
        for (uint32_t i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i].kind == DeviceKind::Target)
                DriverLog("device %u: replaced %llu poses, release boosts %llu", i, m_devices[i].replacedCount,
                          m_devices[i].boostCount);
        }
    }
}

PoseFixer::DeviceKind PoseFixer::classify(uint32_t deviceIndex) const {
    vr::CVRPropertyHelpers* props = vr::VRProperties();
    if (!m_settings.hmdMatch.empty()) {
        vr::PropertyContainerHandle_t hmd = props->TrackedDeviceToPropertyContainer(vr::k_unTrackedDeviceIndex_Hmd);
        std::string hmdModel = hmd == vr::k_ulInvalidPropertyContainer ? "" : props->GetStringProperty(hmd, vr::Prop_ModelNumber_String);
        if (hmdModel.empty()) return DeviceKind::Unknown;  // 헤드셋 속성 아직 없음 → 나중에 다시
        if (hmdModel.find(m_settings.hmdMatch) == std::string::npos) {
            DriverLog("device %u: HMD '%s' does not match '%s' -> ignored", deviceIndex, hmdModel.c_str(), m_settings.hmdMatch.c_str());
            return DeviceKind::Ignored;
        }
    }
    vr::PropertyContainerHandle_t container = props->TrackedDeviceToPropertyContainer(deviceIndex);
    if (container == vr::k_ulInvalidPropertyContainer) return DeviceKind::Unknown;

    std::string model = props->GetStringProperty(container, vr::Prop_ModelNumber_String);
    if (model.empty()) return DeviceKind::Unknown;  // 아직 속성이 안 채워짐 → 나중에 다시
    auto deviceClass = static_cast<vr::ETrackedDeviceClass>(props->GetInt32Property(container, vr::Prop_DeviceClass_Int32));
    bool target = deviceClass == vr::TrackedDeviceClass_Controller && model.find(m_settings.modelMatch) != std::string::npos;
    DriverLog("device %u: model='%s' class=%d -> %s", deviceIndex, model.c_str(), deviceClass, target ? "TARGET" : "ignored");
    return target ? DeviceKind::Target : DeviceKind::Ignored;
}

void PoseFixer::process(uint32_t deviceIndex, vr::DriverPose_t& pose) {
    if (deviceIndex >= m_devices.size()) return;
    std::lock_guard lock(m_mutex);
    if (!m_settings.enabled) return;

    DeviceState& dev = m_devices[deviceIndex];
    if (dev.kind == DeviceKind::Unknown) dev.kind = classify(deviceIndex);
    if (dev.kind != DeviceKind::Target) return;

    if (!pose.poseIsValid || pose.result != vr::TrackingResult_Running_OK) {
        dev.estimator.reset();
        return;
    }

    const vr::DriverPose_t original = pose;

    controllerfix::PoseSample sample;
    sample.timeSec = nowSec() + pose.poseTimeOffset;
    sample.position = {pose.vecPosition[0], pose.vecPosition[1], pose.vecPosition[2]};
    sample.rotation = {pose.qRotation.w, pose.qRotation.x, pose.qRotation.y, pose.qRotation.z};
    dev.estimator.addSample(sample);

    if (m_settings.vdFix.mode != controllerfix::VdFixMode::Off) {
        vr::DriverPose_t candidate = pose;
        applyVdFrameFix(dev, deviceIndex, candidate);
        if (m_settings.vdFix.mode == controllerfix::VdFixMode::Auto && !useVdFrameFix(dev, deviceIndex, original, candidate)) {
            VdShmDump::write(deviceIndex, original, pose);
            return;  // 보정이 필요 없는 기기 (Quest, Steam Link 등): 원래 값 그대로
        }
        pose = candidate;
        controllerfix::VelocityEstimate fixed{toVec3(pose.vecVelocity), toVec3(pose.vecAngularVelocity)};
        dev.releaseBoost.addVelocity(sample.timeSec, fixed);
        controllerfix::VelocityEstimate held;
        if (dev.releaseBoost.override(sample.timeSec, held)) {  // 그립 해제 직후: 직전 최고 속도 유지
            blendInto(pose.vecVelocity, pose.vecAcceleration, held.linear, FULL_WEIGHT);
            blendInto(pose.vecAngularVelocity, pose.vecAngularAcceleration, held.angular, FULL_WEIGHT);
        }
        ++dev.replacedCount;
        VdShmDump::write(deviceIndex, original, pose);
        return;
    }

    controllerfix::VelocityEstimate est;
    bool estimated = dev.estimator.estimate(est);
    if (estimated) dev.releaseBoost.addVelocity(sample.timeSec, est);

    controllerfix::BlendWeights w{1.0, 1.0};
    controllerfix::VelocityEstimate held;
    if (dev.releaseBoost.override(sample.timeSec, held)) est = held;  // 그립 해제 직후: 직전 최고 속도 유지
    else if (!estimated) return;
    else if (m_settings.blendEnabled) w = controllerfix::computeBlendWeights(est, m_settings.blend);
    if (m_settings.replaceLinear) blendInto(pose.vecVelocity, pose.vecAcceleration, est.linear, w.linear);
    if (m_settings.replaceAngular) blendInto(pose.vecAngularVelocity, pose.vecAngularAcceleration, est.angular, w.angular);
    ++dev.replacedCount;
}

void PoseFixer::onComponentCreated(vr::PropertyContainerHandle_t container, const char* name,
                                   vr::VRInputComponentHandle_t handle, bool scalar) {
    if (!name) return;
    std::string path(name);
    bool grip = scalar ? path == GRIP_VALUE_PATH : path == GRIP_CLICK_PATH;
    if (!grip) return;
    std::lock_guard lock(m_mutex);
    m_gripComponents[handle] = {container, scalar};
    int device = deviceForContainer(container);
    if (scalar && device >= 0) m_devices[device].hasScalarGrip = true;
    DriverLog("grip component '%s' created (device %d)", name, device);
}

void PoseFixer::onScalarUpdated(vr::VRInputComponentHandle_t handle, float value, double timeOffset) {
    onGripValue(handle, value, timeOffset);
}

void PoseFixer::onBooleanUpdated(vr::VRInputComponentHandle_t handle, bool value, double timeOffset) {
    onGripValue(handle, value ? 1.0 : 0.0, timeOffset);
}

int PoseFixer::deviceForContainer(vr::PropertyContainerHandle_t container) {
    auto it = m_containerToDevice.find(container);
    if (it != m_containerToDevice.end()) return static_cast<int>(it->second);
    vr::CVRPropertyHelpers* props = vr::VRProperties();
    for (uint32_t i = 0; i < m_devices.size(); ++i) {
        if (props->TrackedDeviceToPropertyContainer(i) == container) {
            m_containerToDevice[container] = i;
            return static_cast<int>(i);
        }
    }
    return -1;  // 장치가 아직 등록 전 → 다음 업데이트에서 다시
}

void PoseFixer::onGripValue(vr::VRInputComponentHandle_t handle, double value, double timeOffset) {
    std::lock_guard lock(m_mutex);
    auto it = m_gripComponents.find(handle);
    if (it == m_gripComponents.end() || !m_settings.enabled) return;
    int device = deviceForContainer(it->second.container);
    if (device < 0) return;
    DeviceState& dev = m_devices[device];
    if (dev.kind != DeviceKind::Target) return;
    if (!it->second.scalar && dev.hasScalarGrip) return;  // 값 그립이 있으면 click 은 무시 (중복 해제 방지)
    controllerfix::ReleaseEvent ev;
    if (dev.releaseBoost.updateGrip(nowSec() + timeOffset, value, ev)) {
        ++dev.boostCount;
        DriverLog("device %d release boost: %.2f m/s -> %.2f m/s (peak %.0f ms before release)", device,
                  ev.currentSpeed, ev.peakSpeed, ev.peakAgeSec * 1000.0);
    }
}

// 판정 전에는 적용한다 (설치 대상이 GXR 사용자). 정상 기기로 판정되면 끈다.
bool PoseFixer::useVdFrameFix(DeviceState& dev, uint32_t deviceIndex, const vr::DriverPose_t& original, const vr::DriverPose_t& fixed) {
    controllerfix::VelocityEstimate measured;
    if (dev.estimator.estimate(measured))
        dev.detector.addVote(measured.linear, toVec3(original.vecVelocity), toVec3(fixed.vecVelocity));
    controllerfix::VdFrameDetector::Verdict verdict = dev.detector.verdict();
    if (verdict != dev.loggedVerdict) {
        dev.loggedVerdict = verdict;
        DriverLog("device %u vdFrameFix auto: %s (fix votes %.0f%% of %zu)", deviceIndex, verdictName(verdict),
                  dev.detector.fixRatio() * PERCENT, dev.detector.votes());
    }
    return verdict != controllerfix::VdFrameDetector::Verdict::Normal;
}

void PoseFixer::applyVdFrameFix(DeviceState& dev, uint32_t deviceIndex, vr::DriverPose_t& pose) {
    if (!dev.frameLogged) {
        dev.frameLogged = true;
        // driver_oculus 는 Touch 모델 오프셋(약 39°)을 qDriverFromHeadRotation 에 넣고 qRotation 은 VD 원본 회전을 둔다.
        // 0° 근처로 나오면 qRotation 에 오프셋이 섞인 것이므로 보정 기준이 맞지 않는다는 뜻.
        double offsetDeg = QUAT_ANGLE_FACTOR * std::acos(std::fmin(1.0, std::fabs(pose.qDriverFromHeadRotation.w))) * RAD_TO_DEG;
        DriverLog("device %u vdFrameFix: pitch %.1f deg, qDriverFromHeadRotation %.1f deg", deviceIndex,
                  m_settings.vdFix.pitchDeg, offsetDeg);
    }
    controllerfix::Quat rotation{pose.qRotation.w, pose.qRotation.x, pose.qRotation.y, pose.qRotation.z};
    double pitch = m_settings.vdFix.pitchDeg;
    // linearScale / angularScale: 게임별 세기 조절 (가속도도 같은 배율이라야 SteamVR 외삽이 맞음)
    double linearScale = m_settings.estimator.linearScale;
    double angularScale = m_settings.estimator.angularScale;
    store(pose.vecVelocity, scaled(controllerfix::vdLocalToWorld(rotation, toVec3(pose.vecVelocity), pitch), linearScale));
    store(pose.vecAngularVelocity, scaled(controllerfix::vdDriverAngularToWorld(rotation, toVec3(pose.vecAngularVelocity), pitch), angularScale));
    store(pose.vecAcceleration, scaled(controllerfix::vdLocalToWorld(rotation, toVec3(pose.vecAcceleration), pitch), linearScale));
}
