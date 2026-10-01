#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace {
std::filesystem::path fileName() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    const auto root = length && length < MAX_PATH ? std::filesystem::path(buffer) :
        std::filesystem::current_path();
    return root / L"TurboIDE" / L"settings.ini";
}

std::string toUtf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
}

void loadSettings(IDESettings &settings) {
    std::ifstream input(fileName(), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("tab_size=", 0) == 0) {
            try { settings.tabSize = std::clamp(std::stoi(line.substr(9)), 1, 32); }
            catch (...) {}
        } else if (line.rfind("default_extension=", 0) == 0) {
            settings.defaultExtension = line.substr(18);
        } else if (line.rfind("last_project=", 0) == 0) {
            settings.lastProject = std::filesystem::u8path(line.substr(13));
        }
    }
}

void saveSettings(const IDESettings &settings) {
    const auto path = fileName();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return;
    output << "tab_size=" << std::clamp(settings.tabSize, 1, 32) << "\n";
    output << "default_extension=" << settings.defaultExtension << "\n";
    if (!settings.lastProject.empty())
        output << "last_project=" << toUtf8(settings.lastProject) << "\n";
}
