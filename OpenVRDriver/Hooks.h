#pragma once

#include <openvr_driver.h>

// vrserver 안의 IVRDriverContext::GetGenericInterface 를 훅해서
// 다른 드라이버(VD)가 받는 IVRServerDriverHost::TrackedDevicePoseUpdated 를 가로챈다.
// (OpenVR-SpaceCalibrator 와 같은 방식, 둘이 함께 체인됨)
namespace Hooks {

bool install(vr::IVRDriverContext* context);
void uninstall();

}  // namespace Hooks
