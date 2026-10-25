#pragma once

#include <map>
#include <mutex>
#include <string>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

// 인스턴스별 다음 레이어/런타임 함수 테이블.
// 한 프로세스에 인스턴스가 여럿일 수 있다 (예: UEVR 의 VDXR 인스턴스 + 게임 자체 Oculus 인스턴스).
struct InstanceDispatch {
    XrInstance instance = XR_NULL_HANDLE;
    bool active = false;  // 대상 런타임 여부
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
    PFN_xrCreateSession createSession = nullptr;
    PFN_xrDestroySession destroySession = nullptr;
    PFN_xrCreateReferenceSpace createReferenceSpace = nullptr;
    PFN_xrCreateActionSpace createActionSpace = nullptr;
    PFN_xrDestroySpace destroySpace = nullptr;
    PFN_xrLocateSpace locateSpace = nullptr;
    PFN_xrLocateSpaces locateSpaces = nullptr;
    PFN_xrLocateSpacesKHR locateSpacesKHR = nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
    // 가로채지 않고 레이어가 직접 쓰는 함수
    PFN_xrPathToString pathToString = nullptr;

    std::mutex pathMutex;
    std::map<XrPath, std::string> pathNames;

    std::string pathName(XrPath path) {
        if (path == XR_NULL_PATH || !pathToString) return {};
        std::lock_guard lock(pathMutex);
        auto it = pathNames.find(path);
        if (it != pathNames.end()) return it->second;
        char buffer[XR_MAX_PATH_LENGTH];
        uint32_t length = 0;
        std::string name;
        if (XR_SUCCEEDED(pathToString(instance, path, XR_MAX_PATH_LENGTH, &length, buffer))) name = buffer;
        pathNames[path] = name;
        return name;
    }
};
