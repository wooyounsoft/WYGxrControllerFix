#include "VdShmDump.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <Windows.h>

#include "DriverLog.h"

namespace {

constexpr const wchar_t* SHM_NAME = L"VirtualDesktop.Oculus.DriverData";
constexpr size_t SHM_SIZE = 0x208;
constexpr const char* CAPTURE_RELATIVE_DIR = "WYGxrControllerFix\\captures";
constexpr size_t FRAME_INDEX_OFFSET = 0xf0;
// 손 블록: quat xyzw +0, pos +0x10, linVel +0x1c, linAcc +0x28, angVel +0x34, flag +0x40
constexpr size_t HAND_BASE_OFFSETS[] = {0x4c, 0x90};
constexpr size_t HAND_FLOATS = 16;  // quat 4 + pos 3 + linVel 3 + linAcc 3 + angVel 3
constexpr size_t HAND_FLAG_OFFSET = 0x40;
constexpr int FLUSH_EVERY_ROWS = 500;

bool g_enabled = false;
HANDLE g_mapping = nullptr;
const uint8_t* g_view = nullptr;
std::ofstream g_out;
int g_rowsSinceFlush = 0;

bool openShm() {
    if (g_view) return true;
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, SHM_NAME);
    if (!g_mapping) return false;
    g_view = static_cast<const uint8_t*>(MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, SHM_SIZE));
    if (!g_view) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
    return g_view != nullptr;
}

bool openFile() {
    if (g_out.is_open()) return true;
    const char* localAppData = std::getenv("LOCALAPPDATA");
    std::filesystem::path dir = std::filesystem::path(localAppData ? localAppData : ".") / CAPTURE_RELATIVE_DIR;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char name[64];
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::strftime(name, sizeof(name), "vddump_%Y%m%d_%H%M%S.csv", &local);
    g_out.open(dir / name);
    if (!g_out) return false;
    g_out << "t,device,poseTimeOffset,qw,qx,qy,qz,px,py,pz,vx,vy,vz,wx,wy,wz,ax,ay,az,awx,awy,awz,"
             "dhw,dhx,dhy,dhz,dhtx,dhty,dhtz,wdw,wdx,wdy,wdz,frame,fvx,fvy,fvz,fwx,fwy,fwz,fax,fay,faz";
    for (const char* hand : {"L", "R"}) {
        for (const char* f : {"qx", "qy", "qz", "qw", "px", "py", "pz", "vx", "vy", "vz", "ax", "ay", "az", "wx", "wy", "wz"})
            g_out << ',' << hand << f;
        g_out << ',' << hand << "flag";
    }
    g_out << '\n';
    DriverLog("vdDump: writing %s", (dir / name).string().c_str());
    return true;
}

void closeAll() {
    if (g_out.is_open()) g_out.close();
    if (g_view) UnmapViewOfFile(g_view);
    if (g_mapping) CloseHandle(g_mapping);
    g_view = nullptr;
    g_mapping = nullptr;
}

}  // namespace

namespace VdShmDump {

void setEnabled(bool enabled) {
    if (g_enabled && !enabled) closeAll();
    g_enabled = enabled;
}

void write(uint32_t deviceIndex, const vr::DriverPose_t& pose, const vr::DriverPose_t& fixed) {
    if (!g_enabled || !openShm() || !openFile()) return;
    using namespace std::chrono;
    double t = duration<double>(steady_clock::now().time_since_epoch()).count();
    g_out.precision(9);
    g_out << t << ',' << deviceIndex << ',' << pose.poseTimeOffset << ',' << pose.qRotation.w << ',' << pose.qRotation.x << ','
          << pose.qRotation.y << ',' << pose.qRotation.z;
    for (const double* v : {pose.vecPosition, pose.vecVelocity, pose.vecAngularVelocity, pose.vecAcceleration, pose.vecAngularAcceleration})
        g_out << ',' << v[0] << ',' << v[1] << ',' << v[2];
    const vr::HmdQuaternion_t& dh = pose.qDriverFromHeadRotation;
    const vr::HmdQuaternion_t& wd = pose.qWorldFromDriverRotation;
    g_out << ',' << dh.w << ',' << dh.x << ',' << dh.y << ',' << dh.z << ',' << pose.vecDriverFromHeadTranslation[0] << ','
          << pose.vecDriverFromHeadTranslation[1] << ',' << pose.vecDriverFromHeadTranslation[2] << ',' << wd.w << ',' << wd.x << ','
          << wd.y << ',' << wd.z;
    int32_t frame;
    std::memcpy(&frame, g_view + FRAME_INDEX_OFFSET, sizeof(frame));
    g_out << ',' << frame;
    for (const double* v : {fixed.vecVelocity, fixed.vecAngularVelocity, fixed.vecAcceleration}) g_out << ',' << v[0] << ',' << v[1] << ',' << v[2];
    for (size_t base : HAND_BASE_OFFSETS) {
        float f[HAND_FLOATS];
        std::memcpy(f, g_view + base, sizeof(f));
        for (float x : f) g_out << ',' << x;
        g_out << ',' << static_cast<int>(g_view[base + HAND_FLAG_OFFSET]);
    }
    g_out << '\n';
    if (++g_rowsSinceFlush >= FLUSH_EVERY_ROWS) {
        g_out.flush();
        g_rowsSinceFlush = 0;
    }
}

}  // namespace VdShmDump
