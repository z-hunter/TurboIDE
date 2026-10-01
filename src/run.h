#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

struct RunRequest {
    std::filesystem::path executable;
    std::filesystem::path workingDirectory;
    std::vector<std::wstring> arguments;
};

struct RunResult {
    bool started = false;
    DWORD exitCode = 0;
    HANDLE screenBuffer = INVALID_HANDLE_VALUE;
    std::string error;
};

HANDLE createUserScreenBuffer(HANDLE ideScreenBuffer);
RunResult runProgram(const RunRequest &request, HANDLE userScreenBuffer,
                     HANDLE ideScreenBuffer, HANDLE consoleInput);
bool showUserScreen(HANDLE screenBuffer, HANDLE ideScreenBuffer, HANDLE consoleInput,
                    bool allowShell, const std::filesystem::path &workingDirectory,
                    std::string &error);
bool runCommandPrompt(HANDLE screenBuffer, HANDLE ideScreenBuffer, HANDLE consoleInput,
                     const std::filesystem::path &workingDirectory, std::string &error);
