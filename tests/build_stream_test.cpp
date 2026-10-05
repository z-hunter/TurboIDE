#include "../src/build.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

int main() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH))
        return 1;
    const auto compiler = std::filesystem::path(executable).parent_path() / L"fake_build_compiler.exe";
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary))
        return 1;
    const auto directory = std::filesystem::path(temporary) /
                           (L"turboide-build-stream-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);

    BuildRequest request;
    request.compilerPath = compiler;
    request.workingDirectory = directory;
    request.outputDirectory = directory / L"output";
    request.sources.push_back(directory / L"fake.c");
    request.includeDirs.push_back(directory / L"headers");
    request.libraryDirs.push_back(directory / L"libraries");
    request.sourceDirs.push_back(directory / L"sources");
    std::atomic_bool canceled{false};
    std::vector<std::chrono::steady_clock::time_point> received;
    std::vector<std::string> messages;
    const auto result = runBuild(request, canceled, [&](BuildMessage message) {
        received.push_back(std::chrono::steady_clock::now());
        messages.push_back(std::move(message.text));
    });
    std::filesystem::remove_all(directory);
    return result.succeeded && result.executable.parent_path() == request.outputDirectory && received.size() == 3 &&
                   received[2] - received[1] >= std::chrono::milliseconds(100) &&
                   messages[0].find("-I") != std::string::npos &&
                   messages[0].find("-L") != std::string::npos &&
                   messages[0].find("-iquote") != std::string::npos
               ? 0 : 1;
}
