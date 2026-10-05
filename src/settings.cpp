#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace {
std::filesystem::path fileName() {
    static const auto path = [] {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        const auto root = length && length < MAX_PATH ? std::filesystem::path(buffer) :
            std::filesystem::current_path();
        return root / L"TurboIDE" / L"settings.ini";
    }();
    return path;
}

std::string toUtf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
}

void loadSettings(IDESettings &settings) {
    settings.includeDirs.clear();
    settings.libraryDirs.clear();
    settings.sourceDirs.clear();
    std::ifstream input(fileName(), std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("tab_size=", 0) == 0) {
            try { settings.tabSize = std::clamp(std::stoi(line.substr(9)), 1, 32); }
            catch (...) {}
        } else if (line.rfind("backup_files=", 0) == 0) {
            settings.backupFiles = line.substr(13) == "1";
        } else if (line.rfind("persistent_blocks=", 0) == 0) {
            settings.persistentBlocks = line.substr(18) == "1";
        } else if (line.rfind("default_extension=", 0) == 0) {
            settings.defaultExtension = line.substr(18);
        } else if (line.rfind("compiler_type=", 0) == 0) {
            if (line.substr(14) == "gcc")
                settings.compilerType = "gcc";
        } else if (line.rfind("compiler_path=", 0) == 0) {
            settings.compilerPath = std::filesystem::u8path(line.substr(14));
        } else if (line.rfind("output_directory=", 0) == 0) {
            settings.outputDirectory = std::filesystem::u8path(line.substr(17));
        } else if (line.rfind("direct_console_input=", 0) == 0) {
            settings.directConsoleInput = line.substr(21) == "1";
        } else if (line.rfind("include_dir=", 0) == 0) {
            settings.includeDirs.push_back(std::filesystem::u8path(line.substr(12)));
        } else if (line.rfind("library_dir=", 0) == 0) {
            settings.libraryDirs.push_back(std::filesystem::u8path(line.substr(12)));
        } else if (line.rfind("source_dir=", 0) == 0) {
            settings.sourceDirs.push_back(std::filesystem::u8path(line.substr(11)));
        } else if (line.rfind("current_directory=", 0) == 0) {
            settings.currentDirectory = std::filesystem::u8path(line.substr(18));
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
    output << "backup_files=" << (settings.backupFiles ? 1 : 0) << "\n";
    output << "persistent_blocks=" << (settings.persistentBlocks ? 1 : 0) << "\n";
    output << "default_extension=" << settings.defaultExtension << "\n";
    output << "compiler_type=gcc\n";
    if (!settings.compilerPath.empty())
        output << "compiler_path=" << toUtf8(settings.compilerPath) << "\n";
    if (!settings.outputDirectory.empty())
        output << "output_directory=" << toUtf8(settings.outputDirectory) << "\n";
    output << "direct_console_input=" << (settings.directConsoleInput ? 1 : 0) << "\n";
    for (const auto &directory : settings.includeDirs)
        output << "include_dir=" << toUtf8(directory) << "\n";
    for (const auto &directory : settings.libraryDirs)
        output << "library_dir=" << toUtf8(directory) << "\n";
    for (const auto &directory : settings.sourceDirs)
        output << "source_dir=" << toUtf8(directory) << "\n";
    if (!settings.currentDirectory.empty())
        output << "current_directory=" << toUtf8(settings.currentDirectory) << "\n";
    if (!settings.lastProject.empty())
        output << "last_project=" << toUtf8(settings.lastProject) << "\n";
}
