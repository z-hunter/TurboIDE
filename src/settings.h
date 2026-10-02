#pragma once

#include <filesystem>
#include <string>

struct IDESettings {
    int tabSize = 8;
    bool backupFiles = true;
    bool persistentBlocks = true;
    std::string defaultExtension = ".c";
    std::filesystem::path currentDirectory;
    std::filesystem::path lastProject;
};

void loadSettings(IDESettings &settings);
void saveSettings(const IDESettings &settings);
