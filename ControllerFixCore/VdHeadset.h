#pragma once

#include <filesystem>
#include <string>

namespace controllerfix {

// VD 는 게임과 SteamVR 에 어떤 기기든 Quest 로 보고하지만, 접속할 때 실제 기기 이름을 Streamer 설정 파일에 저장한다.
// (GXR: "Samsung Galaxy XR", Quest 3: "Meta Quest 3" — 2026-11-02 실측, 접속 후 18초 안에 갱신)
class VdHeadset {
public:
    explicit VdHeadset(std::filesystem::path path = defaultPath()) : m_path(std::move(path)) {}

    static std::filesystem::path defaultPath();
    static std::string parseDeviceName(const std::string& json);  // 없으면 빈 문자열
    static bool isQuestName(const std::string& name);
    static bool isGxrName(const std::string& name);

    // 파일이 바뀌었으면 다시 읽는다. 기기 이름이 바뀌면 true. 못 읽으면 이전 이름을 유지한다.
    bool poll();
    bool isQuest() const { return isQuestName(m_name); }
    bool isGxr() const { return isGxrName(m_name); }
    const char* fixLabel() const { return isQuest() ? "fix OFF" : "auto"; }  // 로그용
    const std::string& name() const { return m_name; }

private:
    std::filesystem::path m_path;
    std::filesystem::file_time_type m_lastWrite{};
    std::string m_name;
};

}  // namespace controllerfix
