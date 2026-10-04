#include "build.h"
#include "toolchain_environment.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace {
bool isCxxSource(const std::filesystem::path &source) {
    const auto extension = source.extension().c_str();
    return _wcsicmp(extension, L".cc") == 0 || _wcsicmp(extension, L".cpp") == 0 ||
           _wcsicmp(extension, L".cxx") == 0;
}

bool isCSource(const std::filesystem::path &source) {
    return _wcsicmp(source.extension().c_str(), L".c") == 0;
}

std::wstring quote(const std::wstring &argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += ch;
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            slashes = 0;
            result += ch;
        }
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}

std::wstring compilerPath(const BuildRequest &request) {
    const bool cxx = std::any_of(request.sources.begin(), request.sources.end(), isCxxSource);
    if (!request.compilerPath.empty()) {
        if (!cxx || _wcsicmp(request.compilerPath.filename().c_str(), L"g++.exe") == 0)
            return request.compilerPath.wstring();
        const auto cxxCompiler = request.compilerPath.parent_path() / L"g++.exe";
        return std::filesystem::is_regular_file(cxxCompiler) ? cxxCompiler.wstring() : std::wstring{};
    }
    DWORD required = GetEnvironmentVariableW(L"TURBOIDE_GCC", nullptr, 0);
    if (required) {
        std::wstring path(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"TURBOIDE_GCC", path.data(), required);
        if (length && length < required) {
            path.resize(length);
            if (!cxx)
                return path;
            const auto cxxCompiler = std::filesystem::path(path).parent_path() / L"g++.exe";
            return std::filesystem::is_regular_file(cxxCompiler) ? cxxCompiler.wstring() : std::wstring{};
        }
    }

    std::wstring path(32768, L'\0');
    const DWORD length = SearchPathW(nullptr, cxx ? L"g++.exe" : L"gcc.exe", nullptr,
                                     static_cast<DWORD>(path.size()), path.data(), nullptr);
    if (!length || length >= path.size())
        return {};
    path.resize(length);
    return path;
}

std::filesystem::path bundledConioDirectory() {
    std::wstring executable(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                            static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return {};
    executable.resize(length);
    const auto directory = std::filesystem::path(executable).parent_path() / L"conio";
    return std::filesystem::is_regular_file(directory / L"conio.h") &&
                   std::filesystem::is_regular_file(directory / L"coniow.c")
               ? directory
               : std::filesystem::path{};
}

bool appendNumber(std::wstring_view value, int &number) {
    if (value.empty())
        return false;
    int parsed = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9')
            return false;
        parsed = parsed * 10 + (ch - L'0');
    }
    number = parsed;
    return true;
}

std::wstring toWide(std::string_view value) {
    if (value.empty())
        return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    UINT page = CP_UTF8;
    if (!length) {
        page = CP_ACP;
        length = MultiByteToWideChar(page, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    }
    if (!length)
        return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(page, page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                        value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string toUtf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (!length)
        return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

BuildMessage parseMessage(std::string_view bytes, const std::filesystem::path &workingDirectory) {
    const std::wstring wide = toWide(bytes);
    BuildMessage message;
    message.text = toUtf8(wide);

    size_t marker = std::wstring::npos;
    for (const auto &[candidate, kind] : {
             std::pair{std::wstring_view(L": fatal error: "), BuildMessageKind::fatal},
             std::pair{std::wstring_view(L": error: "), BuildMessageKind::error},
             std::pair{std::wstring_view(L": warning: "), BuildMessageKind::warning},
             std::pair{std::wstring_view(L": note: "), BuildMessageKind::note}}) {
        const size_t found = wide.find(candidate);
        if (found != std::wstring::npos && (marker == std::wstring::npos || found < marker)) {
            marker = found;
            message.kind = kind;
        }
    }
    if (marker == std::wstring::npos)
        return message;

    const auto diagnosticPrefix = std::wstring_view(wide).substr(0, marker);
    const size_t lastColon = diagnosticPrefix.rfind(L':');
    if (lastColon == std::wstring::npos)
        return message;
    int lastNumber = 0;
    if (!appendNumber(diagnosticPrefix.substr(lastColon + 1), lastNumber))
        return message;

    size_t lineColon = lastColon;
    int line = lastNumber;
    int column = 0;
    const size_t previousColon = lastColon == 0 ? std::wstring::npos : diagnosticPrefix.rfind(L':', lastColon - 1);
    if (previousColon != std::wstring::npos) {
        int previousNumber = 0;
        if (appendNumber(diagnosticPrefix.substr(previousColon + 1,
                                                lastColon - previousColon - 1), previousNumber)) {
            lineColon = previousColon;
            line = previousNumber;
            column = lastNumber;
        }
    }

    const auto fileText = diagnosticPrefix.substr(0, lineColon);
    if (fileText.empty())
        return message;
    std::filesystem::path path{std::wstring(fileText)};
    if (path.is_relative())
        path = workingDirectory / path;
    message.file = path.lexically_normal();
    message.line = line;
    message.column = column;
    message.hasLocation = line > 0;
    message.display = path.filename().u8string() + ":" + std::to_string(line);
    if (column > 0)
        message.display += ":" + std::to_string(column);
    message.display += toUtf8(std::wstring_view(wide).substr(marker));
    return message;
}

void addArg(std::wstring &command, const std::wstring &arg) {
    command += L' ';
    command += quote(arg);
}
}

BuildResult runBuild(const BuildRequest &request, const std::atomic_bool &cancelRequested,
                     const BuildOutputCallback &onOutput) {
    BuildResult result;
    const std::wstring compiler = compilerPath(request);
    if (compiler.empty()) {
        result.error = "GCC/G++ was not found. Configure the toolchain in Options > Compiler, set TURBOIDE_GCC, or add it to PATH.";
        return result;
    }
    if (request.sources.empty()) {
        result.error = "No C or C++ source files to compile.";
        return result;
    }

    const auto outputDirectory = request.workingDirectory / L".turboide-build";
    if (!CreateDirectoryW(outputDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        result.error = "Cannot create .turboide-build directory.";
        return result;
    }
    const DWORD directoryAttributes = GetFileAttributesW(outputDirectory.c_str());
    if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
        (directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        result.error = ".turboide-build exists but is not a directory.";
        return result;
    }
    result.executable = outputDirectory / L"program.exe";
    DeleteFileW(result.executable.c_str());

    std::wstring command = quote(compiler);
    addArg(command, L"-g");
    addArg(command, L"-O0");
    addArg(command, L"-Wall");
    addArg(command, L"-Wextra");
    const auto conioDirectory = bundledConioDirectory();
    if (!conioDirectory.empty())
        addArg(command, L"-I" + conioDirectory.wstring());
    for (const auto &path : request.includeDirs)
        addArg(command, L"-I" + path.wstring());
    for (const auto &define : request.defines)
        addArg(command, L"-D" + define);
    const bool cxx = std::any_of(request.sources.begin(), request.sources.end(), isCxxSource);
    for (const auto &source : request.sources) {
        const bool cSource = cxx && isCSource(source);
        if (cSource)
            addArg(command, L"-x");
        if (cSource)
            addArg(command, L"c");
        addArg(command, source.wstring());
        if (cSource)
            addArg(command, L"-x");
        if (cSource)
            addArg(command, L"none");
    }
    if (!conioDirectory.empty()) {
        if (cxx) {
            addArg(command, L"-x");
            addArg(command, L"c");
        }
        addArg(command, (conioDirectory / L"coniow.c").wstring());
        if (cxx) {
            addArg(command, L"-x");
            addArg(command, L"none");
        }
    }
    for (const auto &library : request.libraries)
        addArg(command, library.compare(0, 2, L"-l") == 0 ? library : L"-l" + library);
    addArg(command, L"-o");
    addArg(command, L".turboide-build\\program.exe");
    BuildMessage commandMessage{"$ " + toUtf8(command)};
    result.messages.push_back(commandMessage);
    if (onOutput)
        onOutput(std::move(commandMessage));

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        result.error = "Cannot create compiler output pipe.";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    auto environment = toolchainEnvironment(std::filesystem::path(compiler));
    const BOOL created = CreateProcessW(compiler.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        environment.empty() ? nullptr : environment.data(),
                                        request.workingDirectory.c_str(), &startup, &process);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        result.error = "Cannot start GCC. Check TURBOIDE_GCC or PATH.";
        return result;
    }

    result.started = true;
    std::string pendingOutput;
    const auto consumeOutput = [&](bool flush) {
        size_t start = 0;
        while (start < pendingOutput.size()) {
            const size_t end = pendingOutput.find('\n', start);
            if (end == std::string::npos && !flush)
                break;
            const size_t stop = end == std::string::npos ? pendingOutput.size() : end;
            auto line = std::string_view(pendingOutput).substr(start, stop - start);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            BuildMessage message = parseMessage(line, request.workingDirectory);
            result.messages.push_back(message);
            if (onOutput)
                onOutput(std::move(message));
            if (end == std::string::npos) {
                start = pendingOutput.size();
                break;
            }
            start = end + 1;
        }
        pendingOutput.erase(0, start);
    };
    char buffer[4096];
    bool canceled = false;
    for (;;) {
        if (cancelRequested.load() && !canceled) {
            canceled = true;
            TerminateProcess(process.hProcess, ERROR_CANCELLED);
        }
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD bytesRead = 0;
            const DWORD requestBytes = std::min<DWORD>(available, sizeof(buffer));
            if (ReadFile(readPipe, buffer, requestBytes, &bytesRead, nullptr) && bytesRead > 0) {
                pendingOutput.append(buffer, bytesRead);
                consumeOutput(false);
            }
            continue;
        }
        if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
            if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                break;
        }
    }
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    consumeOutput(true);
    if (canceled)
        result.error = "Build canceled by Ctrl-Break.";
    if (result.messages.empty() && result.error.empty())
        result.messages.push_back({result.exitCode == 0 ? "Build succeeded." : "Build failed.", {}, 0, 0, false});

    result.succeeded = !canceled && result.exitCode == 0 &&
        GetFileAttributesW(result.executable.c_str()) != INVALID_FILE_ATTRIBUTES;
    return result;
}
