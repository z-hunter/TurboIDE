#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Project {
    std::filesystem::path file;
    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<std::wstring> defines;
    std::vector<std::wstring> libraries;
    std::vector<std::wstring> arguments;
    std::filesystem::path runDirectory;
};

bool loadProject(const std::filesystem::path &file, Project &project, std::string &error);
bool saveProject(const Project &project, std::string &error);
