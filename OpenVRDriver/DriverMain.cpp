#include <cstring>

#include <openvr_driver.h>

#include "DriverLog.h"
#include "Hooks.h"
#include "PoseFixer.h"
#include "UevrDetector.h"
#include "VirtualPad.h"

namespace {

class ServerProvider : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* context) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(context);
        DriverLog("loaded");
        PoseFixer::I().initSettings();
        if (!Hooks::install(context)) return vr::VRInitError_Driver_Failed;
        UevrDetector::I().start();
        return vr::VRInitError_None;
    }

    void Cleanup() override {
        Hooks::uninstall();
        UevrDetector::I().stop();
        VirtualPad::I().shutdown();
        DriverLog("unloaded");
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }

    const char* const* GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    void RunFrame() override { PoseFixer::I().tick(); }
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}
};

ServerProvider g_provider;

}  // namespace

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interfaceName, int* returnCode) {
    if (std::strcmp(interfaceName, vr::IServerTrackedDeviceProvider_Version) == 0) return &g_provider;
    if (returnCode) *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
