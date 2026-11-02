#include "VdHeadset.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace controllerfix {

namespace {

constexpr const char* PROGRAM_DATA_ENV = "ProgramData";
constexpr const char* STREAMER_SETTINGS_RELATIVE_PATH = "Virtual Desktop\\StreamerSettings.json";
constexpr const char* DEVICE_NAME_KEY = "\"DeviceName\"";
constexpr const char* QUEST_NAME_PART = "Quest";
constexpr const char* GXR_NAME_PART = "Galaxy XR";

}  // namespace

std::filesystem::path VdHeadset::defaultPath() {
    const char* programData = std::getenv(PROGRAM_DATA_ENV);
    return std::filesystem::path(programData ? programData : ".") / STREAMER_SETTINGS_RELATIVE_PATH;
}

std::string VdHeadset::parseDeviceName(const std::string& json) {
    size_t key = json.find(DEVICE_NAME_KEY);
    if (key == std::string::npos) return {};
    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return {};
    size_t open = json.find('"', colon);
    if (open == std::string::npos) return {};
    size_t close = json.find('"', open + 1);
    if (close == std::string::npos) return {};
    return json.substr(open + 1, close - open - 1);
}

bool VdHeadset::isQuestName(const std::string& name) { return name.find(QUEST_NAME_PART) != std::string::npos; }

bool VdHeadset::isGxrName(const std::string& name) { return name.find(GXR_NAME_PART) != std::string::npos; }

bool VdHeadset::poll() {
    std::error_code ec;
    auto writeTime = std::filesystem::last_write_time(m_path, ec);
    if (ec || writeTime == m_lastWrite) return false;
    std::ifstream in(m_path);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    std::string name = parseDeviceName(text.str());
    if (name.empty()) return false;  // 저장 중일 수 있다 → 다음 확인에서 다시 읽는다
    m_lastWrite = writeTime;
    if (name == m_name) return false;
    m_name = name;
    return true;
}

}  // namespace controllerfix
