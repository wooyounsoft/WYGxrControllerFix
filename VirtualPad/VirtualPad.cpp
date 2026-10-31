#include "VirtualPad.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <ViGEm/Client.h>

VirtualPad& VirtualPad::I() {
    // 모듈 언로드 시 소멸자가 스레드를 건드리지 않도록 해제하지 않는다 (정리는 shutdown)
    static VirtualPad* instance = new VirtualPad();
    return *instance;
}

void VirtualPad::log(const char* message) const {
    if (m_log) m_log(message);
}

void VirtualPad::setDpad(bool up, bool down, bool left, bool right) {
    uint16_t buttons = (up ? XUSB_GAMEPAD_DPAD_UP : 0) | (down ? XUSB_GAMEPAD_DPAD_DOWN : 0) |
                       (left ? XUSB_GAMEPAD_DPAD_LEFT : 0) | (right ? XUSB_GAMEPAD_DPAD_RIGHT : 0);
    std::lock_guard lock(m_mutex);
    if (m_failed || buttons == m_buttons) return;
    m_buttons = buttons;
    if (!m_thread.joinable()) m_thread = std::thread(&VirtualPad::run, this);
    m_changed.notify_one();
}

void VirtualPad::shutdown() {
    {
        std::lock_guard lock(m_mutex);
        if (!m_thread.joinable()) return;
        m_stop = true;
        m_changed.notify_one();
    }
    m_thread.join();
    std::lock_guard lock(m_mutex);
    m_thread = std::thread();
    m_stop = false;
    m_buttons = 0;
}

void VirtualPad::run() {
    PVIGEM_CLIENT client = vigem_alloc();
    PVIGEM_TARGET pad = vigem_target_x360_alloc();
    bool connected = client && pad && VIGEM_SUCCESS(vigem_connect(client));
    bool added = connected && VIGEM_SUCCESS(vigem_target_add(client, pad));
    if (added) {
        log("virtual gamepad connected");
    } else {
        log("virtual gamepad failed: ViGEmBus is not available");
        std::lock_guard lock(m_mutex);
        m_failed = true;
    }

    uint16_t sent = 0;
    bool first = true;
    std::unique_lock lock(m_mutex);
    while (added) {
        m_changed.wait(lock, [&] { return m_stop || first || m_buttons != sent; });
        if (m_stop) break;
        first = false;
        sent = m_buttons;
        lock.unlock();
        XUSB_REPORT report{};
        report.wButtons = sent;
        vigem_target_x360_update(client, pad, report);
        lock.lock();
    }
    lock.unlock();

    if (added) vigem_target_remove(client, pad);
    if (connected) vigem_disconnect(client);
    if (pad) vigem_target_free(pad);
    if (client) vigem_free(client);
}
