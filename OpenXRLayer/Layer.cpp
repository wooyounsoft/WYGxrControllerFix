// OpenXR API 레이어 진입점: 로더 협상 + 함수 가로채기
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Dispatch.h"
#include "LayerLog.h"
#include "SpaceVelocityFixer.h"

namespace {

constexpr const char* LAYER_NAME = "XR_APILAYER_WY_gxr_controllerfix";
constexpr const wchar_t* VDXR_MODULE_NAME = L"virtualdesktop-openxr.dll";
constexpr const char* VDXR_MODULE_TAG = " [VirtualDesktop runtime dll]";  // runtimeMatch=VirtualDesktop 에 걸림

// 핸들 → 인스턴스 테이블. space/session 핸들로 어느 런타임에 보낼지 찾는다.
std::mutex g_mapMutex;
std::map<XrInstance, std::unique_ptr<InstanceDispatch>> g_instances;
std::map<XrSession, InstanceDispatch*> g_sessions;
std::map<XrSpace, InstanceDispatch*> g_spaces;

InstanceDispatch* byInstance(XrInstance h) {
    std::lock_guard lock(g_mapMutex);
    auto it = g_instances.find(h);
    return it == g_instances.end() ? nullptr : it->second.get();
}
InstanceDispatch* bySession(XrSession h) {
    std::lock_guard lock(g_mapMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}
InstanceDispatch* bySpace(XrSpace h) {
    std::lock_guard lock(g_mapMutex);
    auto it = g_spaces.find(h);
    return it == g_spaces.end() ? nullptr : it->second;
}

template <typename T>
const T* findInChain(const void* next, XrStructureType type) {
    for (auto* base = static_cast<const XrBaseInStructure*>(next); base; base = base->next)
        if (base->type == type) return reinterpret_cast<const T*>(base);
    return nullptr;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrDestroyInstance(XrInstance instance) {
    InstanceDispatch* d = byInstance(instance);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->destroyInstance(instance);
    std::lock_guard lock(g_mapMutex);
    for (auto it = g_sessions.begin(); it != g_sessions.end();) it = (it->second == d) ? g_sessions.erase(it) : std::next(it);
    for (auto it = g_spaces.begin(); it != g_spaces.end();) it = (it->second == d) ? g_spaces.erase(it) : std::next(it);
    g_instances.erase(instance);
    LayerLog("instance destroyed");
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    InstanceDispatch* d = byInstance(instance);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->createSession(instance, info, session);
    if (XR_SUCCEEDED(r)) {
        {
            std::lock_guard lock(g_mapMutex);
            g_sessions[*session] = d;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrDestroySession(XrSession session) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->destroySession(session);
    std::lock_guard lock(g_mapMutex);
    g_sessions.erase(session);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* info, XrSpace* space) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->createReferenceSpace(session, info, space);
    if (XR_SUCCEEDED(r)) {
        std::lock_guard lock(g_mapMutex);
        g_spaces[*space] = d;
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* info, XrSpace* space) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->createActionSpace(session, info, space);
    if (XR_SUCCEEDED(r)) {
        {
            std::lock_guard lock(g_mapMutex);
            g_spaces[*space] = d;
        }
        if (d->active && info)
            SpaceVelocityFixer::I().onActionSpaceCreated(*space, session, info->action,
                                                         SpaceVelocityFixer::handOf(d->pathName(info->subactionPath)));
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrDestroySpace(XrSpace space) {
    InstanceDispatch* d = bySpace(space);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    SpaceVelocityFixer::I().onSpaceDestroyed(space);
    XrResult r = d->destroySpace(space);
    std::lock_guard lock(g_mapMutex);
    g_spaces.erase(space);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) {
    InstanceDispatch* d = bySpace(space);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->locateSpace(space, baseSpace, time, location);
    if (XR_FAILED(r) || !location || !d->active) return r;
    auto* velocity = const_cast<XrSpaceVelocity*>(findInChain<XrSpaceVelocity>(location->next, XR_TYPE_SPACE_VELOCITY));
    SpaceVelocityFixer::I().process(*d, space, baseSpace, time, location->locationFlags, location->pose,
                                    velocity ? &velocity->velocityFlags : nullptr,
                                    velocity ? &velocity->linearVelocity : nullptr,
                                    velocity ? &velocity->angularVelocity : nullptr);
    return r;
}

void processSpaces(InstanceDispatch& d, const XrSpacesLocateInfo* info, XrSpaceLocations* locations) {
    auto* velocities = const_cast<XrSpaceVelocities*>(findInChain<XrSpaceVelocities>(locations->next, XR_TYPE_SPACE_VELOCITIES));
    for (uint32_t i = 0; i < info->spaceCount && i < locations->locationCount; ++i) {
        XrSpaceVelocityData* v = (velocities && i < velocities->velocityCount) ? &velocities->velocities[i] : nullptr;
        SpaceVelocityFixer::I().process(d, info->spaces[i], info->baseSpace, info->time, locations->locations[i].locationFlags,
                                        locations->locations[i].pose, v ? &v->velocityFlags : nullptr,
                                        v ? &v->linearVelocity : nullptr, v ? &v->angularVelocity : nullptr);
    }
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrLocateSpaces(XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locations) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->locateSpaces(session, info, locations);
    if (XR_SUCCEEDED(r) && info && locations && d->active) processSpaces(*d, info, locations);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrLocateSpacesKHR(XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locations) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->locateSpacesKHR(session, info, locations);
    if (XR_SUCCEEDED(r) && info && locations && d->active) processSpaces(*d, info, locations);
    return r;
}

// 그립 액션과 손을 기록한다 (해제 보정).
XRAPI_ATTR XrResult XRAPI_CALL Layer_xrSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* suggested) {
    InstanceDispatch* d = byInstance(instance);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->suggestInteractionProfileBindings(instance, suggested);
    if (XR_SUCCEEDED(r) && d->active && suggested) {
        for (uint32_t i = 0; i < suggested->countSuggestedBindings; ++i) {
            const XrActionSuggestedBinding& b = suggested->suggestedBindings[i];
            std::string path = d->pathName(b.binding);
            SpaceVelocityFixer::I().onBinding(b.action, SpaceVelocityFixer::handOf(path), SpaceVelocityFixer::kindOf(path));
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrGetActionStateFloat(XrSession session, const XrActionStateGetInfo* info, XrActionStateFloat* state) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->getActionStateFloat(session, info, state);
    if (XR_SUCCEEDED(r) && d->active && info && state && state->isActive)
        SpaceVelocityFixer::I().onActionState(info->action, SpaceVelocityFixer::handOf(d->pathName(info->subactionPath)), state->currentState);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* info, XrActionStateBoolean* state) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->getActionStateBoolean(session, info, state);
    if (XR_SUCCEEDED(r) && d->active && info && state && state->isActive)
        SpaceVelocityFixer::I().onActionState(info->action, SpaceVelocityFixer::handOf(d->pathName(info->subactionPath)), state->currentState ? 1.0 : 0.0);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrGetActionStateVector2f(XrSession session, const XrActionStateGetInfo* info, XrActionStateVector2f* state) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    XrResult r = d->getActionStateVector2f(session, info, state);
    if (XR_SUCCEEDED(r) && d->active && info && state && state->isActive) SpaceVelocityFixer::I().onStickState(info->action, state->currentState);
    return r;
}

// 진동 강화: 앱의 구조체는 건드리지 않고 복사본을 조정해서 넘긴다
XRAPI_ATTR XrResult XRAPI_CALL Layer_xrApplyHapticFeedback(XrSession session, const XrHapticActionInfo* info, const XrHapticBaseHeader* haptic) {
    InstanceDispatch* d = bySession(session);
    if (!d) return XR_ERROR_HANDLE_INVALID;
    if (!d->active || !haptic || haptic->type != XR_TYPE_HAPTIC_VIBRATION) return d->applyHapticFeedback(session, info, haptic);
    XrHapticVibration vibration = *reinterpret_cast<const XrHapticVibration*>(haptic);
    SpaceVelocityFixer::I().tuneHaptic(vibration);
    return d->applyHapticFeedback(session, info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

// 다음 레이어/런타임 함수를 얻어서, 있으면 우리 함수로 바꿔 돌려준다.
template <typename Pfn>
XrResult wrap(InstanceDispatch& d, const char* name, PFN_xrVoidFunction* function, Pfn& next, Pfn ours) {
    XrResult r = d.getInstanceProcAddr(d.instance, name, function);
    if (XR_SUCCEEDED(r) && *function) {
        next = reinterpret_cast<Pfn>(*function);
        *function = reinterpret_cast<PFN_xrVoidFunction>(ours);
    }
    return r;
}

XrResult wrapByName(InstanceDispatch& d, const char* name, PFN_xrVoidFunction* function, bool& handled) {
    handled = true;
    if (std::strcmp(name, "xrDestroyInstance") == 0) return wrap(d, name, function, d.destroyInstance, &Layer_xrDestroyInstance);
    if (std::strcmp(name, "xrCreateSession") == 0) return wrap(d, name, function, d.createSession, &Layer_xrCreateSession);
    if (std::strcmp(name, "xrDestroySession") == 0) return wrap(d, name, function, d.destroySession, &Layer_xrDestroySession);
    if (std::strcmp(name, "xrCreateReferenceSpace") == 0) return wrap(d, name, function, d.createReferenceSpace, &Layer_xrCreateReferenceSpace);
    if (std::strcmp(name, "xrCreateActionSpace") == 0) return wrap(d, name, function, d.createActionSpace, &Layer_xrCreateActionSpace);
    if (std::strcmp(name, "xrDestroySpace") == 0) return wrap(d, name, function, d.destroySpace, &Layer_xrDestroySpace);
    if (std::strcmp(name, "xrLocateSpace") == 0) return wrap(d, name, function, d.locateSpace, &Layer_xrLocateSpace);
    if (std::strcmp(name, "xrLocateSpaces") == 0) return wrap(d, name, function, d.locateSpaces, &Layer_xrLocateSpaces);
    if (std::strcmp(name, "xrLocateSpacesKHR") == 0) return wrap(d, name, function, d.locateSpacesKHR, &Layer_xrLocateSpacesKHR);
    if (std::strcmp(name, "xrSuggestInteractionProfileBindings") == 0)
        return wrap(d, name, function, d.suggestInteractionProfileBindings, &Layer_xrSuggestInteractionProfileBindings);
    if (std::strcmp(name, "xrGetActionStateFloat") == 0) return wrap(d, name, function, d.getActionStateFloat, &Layer_xrGetActionStateFloat);
    if (std::strcmp(name, "xrGetActionStateBoolean") == 0) return wrap(d, name, function, d.getActionStateBoolean, &Layer_xrGetActionStateBoolean);
    if (std::strcmp(name, "xrGetActionStateVector2f") == 0) return wrap(d, name, function, d.getActionStateVector2f, &Layer_xrGetActionStateVector2f);
    if (std::strcmp(name, "xrApplyHapticFeedback") == 0) return wrap(d, name, function, d.applyHapticFeedback, &Layer_xrApplyHapticFeedback);
    handled = false;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

PFN_xrGetInstanceProcAddr g_lastNextGetInstanceProcAddr = nullptr;  // 인스턴스 없이 호출되는 전역 함수용

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (std::strcmp(name, "xrGetInstanceProcAddr") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(&Layer_xrGetInstanceProcAddr);
        return XR_SUCCESS;
    }
    InstanceDispatch* d = byInstance(instance);
    if (!d) return g_lastNextGetInstanceProcAddr ? g_lastNextGetInstanceProcAddr(instance, name, function) : XR_ERROR_HANDLE_INVALID;
    bool handled = false;
    XrResult r = wrapByName(*d, name, function, handled);
    return handled ? r : d->getInstanceProcAddr(instance, name, function);
}

// 런타임 이름 + 실제 로드된 런타임 DLL 표시.
// VDXR 은 OVRPlugin 게임에서 "OculusXR 호환 모드"로 이름을 "Oculus" 로 보고하므로 DLL 로 판별한다.
std::string runtimeName(InstanceDispatch& d) {
    std::string name;
    PFN_xrGetInstanceProperties getProps = nullptr;
    if (XR_SUCCEEDED(d.getInstanceProcAddr(d.instance, "xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction*>(&getProps))) && getProps) {
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(getProps(d.instance, &props))) name = props.runtimeName;
    }
    if (GetModuleHandleW(VDXR_MODULE_NAME)) name += VDXR_MODULE_TAG;
    return name;
}

XRAPI_ATTR XrResult XRAPI_CALL Layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {
    if (!layerInfo || !layerInfo->nextInfo || std::strcmp(layerInfo->nextInfo->layerName, LAYER_NAME) != 0)
        return XR_ERROR_INITIALIZATION_FAILED;
    PFN_xrGetInstanceProcAddr nextGipa = layerInfo->nextInfo->nextGetInstanceProcAddr;
    g_lastNextGetInstanceProcAddr = nextGipa;

    XrApiLayerCreateInfo chained = *layerInfo;
    chained.nextInfo = layerInfo->nextInfo->next;
    XrResult r = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &chained, instance);
    if (XR_FAILED(r)) return r;

    auto d = std::make_unique<InstanceDispatch>();
    d->instance = *instance;
    d->getInstanceProcAddr = nextGipa;
    // 로더가 이후 xrGetInstanceProcAddr 로 다시 요청하지만, 직접 호출 경로 대비 미리 채움
    const char* hooked[] = {"xrDestroyInstance", "xrCreateSession", "xrDestroySession", "xrCreateReferenceSpace",
                            "xrCreateActionSpace", "xrDestroySpace", "xrLocateSpace",
                            "xrSuggestInteractionProfileBindings", "xrGetActionStateFloat", "xrGetActionStateBoolean",
                            "xrGetActionStateVector2f", "xrApplyHapticFeedback"};
    for (const char* name : hooked) {
        PFN_xrVoidFunction fn = nullptr;
        bool handled = false;
        wrapByName(*d, name, &fn, handled);
    }
    nextGipa(*instance, "xrPathToString", reinterpret_cast<PFN_xrVoidFunction*>(&d->pathToString));
    d->active = SpaceVelocityFixer::I().onInstanceCreated(info->applicationInfo.applicationName, runtimeName(*d));

    std::lock_guard lock(g_mapMutex);
    g_instances[*instance] = std::move(d);
    return r;
}

}  // namespace

extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request) {
    if (!loaderInfo || !request || std::strcmp(layerName, LAYER_NAME) != 0 ||
        loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;

    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = &Layer_xrGetInstanceProcAddr;
    request->createApiLayerInstance = &Layer_xrCreateApiLayerInstance;
    return XR_SUCCESS;
}
