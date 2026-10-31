#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

// ViGEmBus 가상 Xbox 패드. D-pad 조합을 처음 쓸 때 패드를 연결한다 (안 쓰면 패드가 생기지 않음).
// 입력 훅을 막지 않도록 패드 연결과 전송은 전용 스레드에서 처리한다.
class VirtualPad {
public:
    using LogFn = void (*)(const char* message);

    static VirtualPad& I();

    void setLogger(LogFn log) { m_log = log; }
    void setDpad(bool up, bool down, bool left, bool right);
    // 패드를 제거하고 스레드를 끝낸다. 모듈이 내려가기 전에 호출 (다음 setDpad 에서 다시 시작)
    void shutdown();

private:
    VirtualPad() = default;
    void run();
    void log(const char* message) const;

    LogFn m_log = nullptr;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::thread m_thread;
    uint16_t m_buttons = 0;
    bool m_stop = false;
    bool m_failed = false;  // ViGEmBus 없음 등. 다시 시도하지 않는다
};
