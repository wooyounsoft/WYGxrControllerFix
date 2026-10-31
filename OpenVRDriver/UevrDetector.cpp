#include "UevrDetector.h"

#include <chrono>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include "DriverLog.h"

namespace {

constexpr auto POLL_INTERVAL = std::chrono::seconds(3);
constexpr const wchar_t* UEVR_MODULE_NAME = L"UEVRBackend.dll";
constexpr const wchar_t* UNREAL_EXE_SUFFIX = L"-Shipping.exe";  // Unreal 게임만 모듈을 확인한다

bool endsWith(const wchar_t* text, const wchar_t* suffix) {
    size_t textLength = wcslen(text), suffixLength = wcslen(suffix);
    return textLength >= suffixLength && _wcsicmp(text + textLength - suffixLength, suffix) == 0;
}

bool hasUevrModule(DWORD processId) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{sizeof(module)};
    bool found = false;
    for (BOOL ok = Module32FirstW(snapshot, &module); ok && !found; ok = Module32NextW(snapshot, &module))
        found = _wcsicmp(module.szModule, UEVR_MODULE_NAME) == 0;
    CloseHandle(snapshot);
    return found;
}

bool uevrGameRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W process{sizeof(process)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snapshot, &process); ok && !found; ok = Process32NextW(snapshot, &process))
        found = endsWith(process.szExeFile, UNREAL_EXE_SUFFIX) && hasUevrModule(process.th32ProcessID);
    CloseHandle(snapshot);
    return found;
}

}  // namespace

UevrDetector& UevrDetector::I() {
    static UevrDetector instance;
    return instance;
}

void UevrDetector::start() {
    if (!m_thread.joinable()) m_thread = std::thread(&UevrDetector::run, this);
}

void UevrDetector::stop() {
    if (!m_thread.joinable()) return;
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

void UevrDetector::run() {
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        lock.unlock();
        bool running = uevrGameRunning();
        if (running != m_running.exchange(running)) DriverLog("UEVR game %s -> D-pad %s", running ? "found" : "gone", running ? "off" : "on");
        lock.lock();
        m_wake.wait_for(lock, POLL_INTERVAL, [this] { return m_stop; });
    }
}
