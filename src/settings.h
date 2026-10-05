#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct IDESettings {
    int tabSize = 8;
    bool backupFiles = true;
    bool persistentBlocks = true;
    std::string defaultExtension = ".c";
    std::string compilerType = "gcc";
    std::filesystem::path compilerPath;
    std::filesystem::path outputDirectory;
    bool directConsoleInput = false;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<std::filesystem::path> libraryDirs;
    std::vector<std::filesystem::path> sourceDirs;
    std::filesystem::path currentDirectory;
    std::filesystem::path lastProject;
};

void loadSettings(IDESettings &settings);
void saveSettings(const IDESettings &settings);
