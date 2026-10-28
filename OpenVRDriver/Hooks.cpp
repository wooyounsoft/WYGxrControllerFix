#include "Hooks.h"

#include <cstring>
#include <string>

#include <MinHook.h>

#include "DriverLog.h"
#include "PoseFixer.h"

namespace {

constexpr int VTABLE_GET_GENERIC_INTERFACE = 0;    // IVRDriverContext
constexpr int VTABLE_TRACKED_DEVICE_POSE_UPDATED = 1;  // IVRServerDriverHost_005/006
constexpr int VTABLE_POLL_NEXT_EVENT = 5;              // IVRServerDriverHost_005/006 (진동 이벤트)
constexpr const char* HOST_005 = "IVRServerDriverHost_005";
constexpr const char* HOST_006 = "IVRServerDriverHost_006";
// driver_oculus 는 IVRDriverInput_005, VD 는 _004/_005 를 쓴다. 버전마다 구현 함수가 다를 수 있어 따로 훅한다.
// 앞 4개 vtable 항목(Create/Update Boolean/Scalar)은 세 버전 모두 같다.
constexpr const char* INPUT_VERSIONS[] = {"IVRDriverInput_003", "IVRDriverInput_004", "IVRDriverInput_005"};
constexpr int INPUT_VERSION_COUNT = sizeof(INPUT_VERSIONS) / sizeof(INPUT_VERSIONS[0]);
constexpr int VTABLE_CREATE_BOOLEAN = 0;
constexpr int VTABLE_UPDATE_BOOLEAN = 1;
constexpr int VTABLE_CREATE_SCALAR = 2;
constexpr int VTABLE_UPDATE_SCALAR = 3;

using CreateBooleanFn = vr::EVRInputError (*)(void*, vr::PropertyContainerHandle_t, const char*, vr::VRInputComponentHandle_t*);
using UpdateBooleanFn = vr::EVRInputError (*)(void*, vr::VRInputComponentHandle_t, bool, double);
using CreateScalarFn = vr::EVRInputError (*)(void*, vr::PropertyContainerHandle_t, const char*, vr::VRInputComponentHandle_t*,
                                             vr::EVRScalarType, vr::EVRScalarUnits);
using UpdateScalarFn = vr::EVRInputError (*)(void*, vr::VRInputComponentHandle_t, float, double);

struct InputOriginals {
    CreateBooleanFn createBoolean = nullptr;
    UpdateBooleanFn updateBoolean = nullptr;
    CreateScalarFn createScalar = nullptr;
    UpdateScalarFn updateScalar = nullptr;
};
InputOriginals g_input[INPUT_VERSION_COUNT];

using GetGenericInterfaceFn = void* (*)(vr::IVRDriverContext*, const char*, vr::EVRInitError*);
using PoseUpdatedFn = void (*)(void*, uint32_t, const vr::DriverPose_t&, uint32_t);
using PollNextEventFn = bool (*)(void*, vr::VREvent_t*, uint32_t);

GetGenericInterfaceFn g_origGetGenericInterface = nullptr;
PoseUpdatedFn g_origPoseUpdated005 = nullptr;
PoseUpdatedFn g_origPoseUpdated006 = nullptr;
PollNextEventFn g_origPollNextEvent005 = nullptr;
PollNextEventFn g_origPollNextEvent006 = nullptr;
bool g_minHookInitialized = false;

void* vtableEntry(void* object, int index) {
    return (*reinterpret_cast<void***>(object))[index];
}

template <typename Fn>
bool hookFunction(void* target, void* detour, Fn* original, const char* name) {
    MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(original));
    if (st == MH_ERROR_ALREADY_CREATED) return true;
    if (st != MH_OK || (st = MH_EnableHook(target)) != MH_OK) {
        DriverLog("hook %s failed: %s", name, MH_StatusToString(st));
        return false;
    }
    DriverLog("hooked %s", name);
    return true;
}

void detourPoseUpdated(PoseUpdatedFn original, void* self, uint32_t index, const vr::DriverPose_t& pose, uint32_t size) {
    if (size < sizeof(vr::DriverPose_t)) {
        original(self, index, pose, size);
        return;
    }
    vr::DriverPose_t fixed = pose;
    PoseFixer::I().process(index, fixed);
    original(self, index, fixed, size);
}

void detourPoseUpdated005(void* self, uint32_t index, const vr::DriverPose_t& pose, uint32_t size) {
    detourPoseUpdated(g_origPoseUpdated005, self, index, pose, size);
}

void detourPoseUpdated006(void* self, uint32_t index, const vr::DriverPose_t& pose, uint32_t size) {
    detourPoseUpdated(g_origPoseUpdated006, self, index, pose, size);
}

// 드라이버가 받아 가는 이벤트 중 진동 이벤트를 조정한다 (진동 강화)
bool detourPollNextEvent(PollNextEventFn original, void* self, vr::VREvent_t* event, uint32_t size) {
    bool got = original(self, event, size);
    if (got && event && size >= sizeof(vr::VREvent_t)) PoseFixer::I().onDriverEvent(*event);
    return got;
}

bool detourPollNextEvent005(void* self, vr::VREvent_t* event, uint32_t size) {
    return detourPollNextEvent(g_origPollNextEvent005, self, event, size);
}

bool detourPollNextEvent006(void* self, vr::VREvent_t* event, uint32_t size) {
    return detourPollNextEvent(g_origPollNextEvent006, self, event, size);
}

void hookHost(void* host, const char* version) {
    if (!host) return;
    void* pose = vtableEntry(host, VTABLE_TRACKED_DEVICE_POSE_UPDATED);
    void* poll = vtableEntry(host, VTABLE_POLL_NEXT_EVENT);
    if (std::strcmp(version, HOST_006) == 0) {
        hookFunction(pose, reinterpret_cast<void*>(&detourPoseUpdated006), &g_origPoseUpdated006, "IVRServerDriverHost_006::TrackedDevicePoseUpdated");
        hookFunction(poll, reinterpret_cast<void*>(&detourPollNextEvent006), &g_origPollNextEvent006, "IVRServerDriverHost_006::PollNextEvent");
    } else {
        hookFunction(pose, reinterpret_cast<void*>(&detourPoseUpdated005), &g_origPoseUpdated005, "IVRServerDriverHost_005::TrackedDevicePoseUpdated");
        hookFunction(poll, reinterpret_cast<void*>(&detourPollNextEvent005), &g_origPollNextEvent005, "IVRServerDriverHost_005::PollNextEvent");
    }
}

template <int V>
vr::EVRInputError detourCreateBoolean(void* self, vr::PropertyContainerHandle_t container, const char* name,
                                      vr::VRInputComponentHandle_t* handle) {
    vr::EVRInputError r = g_input[V].createBoolean(self, container, name, handle);
    if (r == vr::VRInputError_None && handle) PoseFixer::I().onComponentCreated(container, name, *handle, false);
    return r;
}

template <int V>
vr::EVRInputError detourUpdateBoolean(void* self, vr::VRInputComponentHandle_t handle, bool value, double timeOffset) {
    PoseFixer::I().onBooleanUpdated(handle, value, timeOffset);
    return g_input[V].updateBoolean(self, handle, value, timeOffset);
}

template <int V>
vr::EVRInputError detourCreateScalar(void* self, vr::PropertyContainerHandle_t container, const char* name,
                                     vr::VRInputComponentHandle_t* handle, vr::EVRScalarType type, vr::EVRScalarUnits units) {
    vr::EVRInputError r = g_input[V].createScalar(self, container, name, handle, type, units);
    if (r == vr::VRInputError_None && handle) PoseFixer::I().onComponentCreated(container, name, *handle, true);
    return r;
}

template <int V>
vr::EVRInputError detourUpdateScalar(void* self, vr::VRInputComponentHandle_t handle, float value, double timeOffset) {
    value = PoseFixer::I().onScalarUpdated(handle, value, timeOffset);  // 스틱 축이면 데드존/감도 조정
    return g_input[V].updateScalar(self, handle, value, timeOffset);
}

// 여러 버전이 같은 구현 함수를 공유하면 처음 훅한 버전의 detour 가 모두 처리한다 (MH_ERROR_ALREADY_CREATED)
template <int V>
void hookInputVersion(void* input) {
    if (!input) return;
    std::string prefix = std::string(INPUT_VERSIONS[V]) + "::";
    InputOriginals& o = g_input[V];
    hookFunction(vtableEntry(input, VTABLE_CREATE_BOOLEAN), reinterpret_cast<void*>(&detourCreateBoolean<V>), &o.createBoolean, (prefix + "CreateBooleanComponent").c_str());
    hookFunction(vtableEntry(input, VTABLE_UPDATE_BOOLEAN), reinterpret_cast<void*>(&detourUpdateBoolean<V>), &o.updateBoolean, (prefix + "UpdateBooleanComponent").c_str());
    hookFunction(vtableEntry(input, VTABLE_CREATE_SCALAR), reinterpret_cast<void*>(&detourCreateScalar<V>), &o.createScalar, (prefix + "CreateScalarComponent").c_str());
    hookFunction(vtableEntry(input, VTABLE_UPDATE_SCALAR), reinterpret_cast<void*>(&detourUpdateScalar<V>), &o.updateScalar, (prefix + "UpdateScalarComponent").c_str());
}

void hookInput(const char* version, void* input) {
    if (!version) return;
    if (std::strcmp(version, INPUT_VERSIONS[0]) == 0) hookInputVersion<0>(input);
    else if (std::strcmp(version, INPUT_VERSIONS[1]) == 0) hookInputVersion<1>(input);
    else if (std::strcmp(version, INPUT_VERSIONS[2]) == 0) hookInputVersion<2>(input);
}

void* detourGetGenericInterface(vr::IVRDriverContext* self, const char* version, vr::EVRInitError* error) {
    void* iface = g_origGetGenericInterface(self, version, error);
    if (version && (std::strcmp(version, HOST_006) == 0 || std::strcmp(version, HOST_005) == 0)) hookHost(iface, version);
    hookInput(version, iface);
    return iface;
}

}  // namespace

namespace Hooks {

bool install(vr::IVRDriverContext* context) {
    if (MH_Initialize() != MH_OK) {
        DriverLog("MH_Initialize failed");
        return false;
    }
    g_minHookInitialized = true;
    void* target = vtableEntry(context, VTABLE_GET_GENERIC_INTERFACE);
    if (!hookFunction(target, reinterpret_cast<void*>(&detourGetGenericInterface), &g_origGetGenericInterface,
                      "IVRDriverContext::GetGenericInterface"))
        return false;
    // 이미 로드된 드라이버가 같은 구현을 쓰는 경우를 위해 현재 버전도 즉시 훅
    hookHost(vr::VRServerDriverHost(), HOST_006);
    // 그립 해제 감지: 다른 드라이버가 쓰는 버전도 미리 받아 훅 (driver_oculus 는 이 드라이버보다 늦게 로드됨)
    for (const char* version : INPUT_VERSIONS) {
        vr::EVRInitError err = vr::VRInitError_None;
        hookInput(version, g_origGetGenericInterface(context, version, &err));
    }
    return true;
}

void uninstall() {
    if (!g_minHookInitialized) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_minHookInitialized = false;
}

}  // namespace Hooks
