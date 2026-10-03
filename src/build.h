#pragma once

#include <filesystem>
#include <atomic>
#include <string>
#include <vector>

enum class BuildMessageKind {
    info,
    note,
    warning,
    error,
    fatal
};

struct BuildMessage {
    std::string text;
    std::filesystem::path file;
    int line = 0;
    int column = 0;
    bool hasLocation = false;
    std::string display;
    BuildMessageKind kind = BuildMessageKind::info;
};

struct BuildResult {
    bool started = false;
    bool succeeded = false;
    unsigned long exitCode = 0;
    std::string error;
    std::filesystem::path executable;
    std::vector<BuildMessage> messages;
};

struct BuildRequest {
    std::filesystem::path workingDirectory;
    std::filesystem::path compilerPath;
    std::vector<std::filesystem::path> sources;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<std::wstring> defines;
    std::vector<std::wstring> libraries;
};

BuildResult runBuild(const BuildRequest &request, const std::atomic_bool &cancelRequested);
