#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// UEVR 이 붙은 게임이 실행 중인지 주기적으로 확인한다.
// UEVR 은 자체 D-pad 를 쓰므로 그동안 이 드라이버의 D-pad 를 끈다.
class UevrDetector {
public:
    static UevrDetector& I();

    void start();
    void stop();  // 모듈이 내려가기 전에 호출
    bool running() const { return m_running; }

private:
    UevrDetector() = default;
    void run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::atomic<bool> m_running{false};
};
