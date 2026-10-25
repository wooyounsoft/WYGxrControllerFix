#include "LayerLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace {

constexpr const char* LOG_RELATIVE_PATH = "WYGxrControllerFix\\openxr_layer.log";
constexpr size_t LOG_BUFFER_SIZE = 1024;
constexpr size_t TIME_BUFFER_SIZE = 32;

std::mutex g_logMutex;

std::filesystem::path logPath() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    return std::filesystem::path(localAppData ? localAppData : ".") / LOG_RELATIVE_PATH;
}

}  // namespace

void LayerLog(const char* fmt, ...) {
    char msg[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char stamp[TIME_BUFFER_SIZE];
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);

    std::lock_guard lock(g_logMutex);
    std::error_code ec;
    std::filesystem::path path = logPath();
    std::filesystem::create_directories(path.parent_path(), ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a") != 0 || !f) return;
    std::fprintf(f, "[%s] %s\n", stamp, msg);
    std::fclose(f);
}
