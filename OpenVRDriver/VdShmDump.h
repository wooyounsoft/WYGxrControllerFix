#pragma once

#include <cstdint>

#include <openvr_driver.h>

// 진단: driver_oculus 가 넘긴 DriverPose(보정 전/후)와 같은 순간의 VD 공유 메모리 원본을 CSV 로 기록한다.
// %LOCALAPPDATA%\WYGxrControllerFix\captures\vddump_*.csv, settings.ini 의 vdDump=1 로 켠다.
namespace VdShmDump {

void setEnabled(bool enabled);
void write(uint32_t deviceIndex, const vr::DriverPose_t& original, const vr::DriverPose_t& fixed);

}  // namespace VdShmDump
