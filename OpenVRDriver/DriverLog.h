#pragma once

#include <cstdarg>
#include <cstdio>

#include <openvr_driver.h>

// SteamVR vrserver.txt 로그 (prefix: WYGxrControllerFix)
inline void DriverLog(const char* fmt, ...) {
    static constexpr size_t LOG_BUFFER_SIZE = 1024;
    char buf[LOG_BUFFER_SIZE];
    int prefix = std::snprintf(buf, sizeof(buf), "[WYGxrControllerFix] ");
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf + prefix, sizeof(buf) - prefix, fmt, args);
    va_end(args);
    if (vr::IVRDriverLog* log = vr::VRDriverLog()) log->Log(buf);
}
