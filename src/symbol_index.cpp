#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "symbol_index.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string>
#include <system_error>

namespace {
std::wstring quoteArgument(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
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

bool readAvailable(HANDLE &pipe, std::string &destination) {
    if (pipe == INVALID_HANDLE_VALUE)
        return false;
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        return false;
    }
    if (!available)
        return true;
    char buffer[8192];
    DWORD read = 0;
    if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
        return false;
    }
    destination.append(buffer, read);
    return true;
}

bool runProcess(const std::filesystem::path &executable,
                const std::vector<std::wstring> &arguments,
                const std::filesystem::path &workingDirectory,
                std::string &standardOutput, std::string &standardError,
                DWORD &exitCode, std::string &error) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outputRead = nullptr, outputWrite = nullptr;
    HANDLE errorRead = nullptr, errorWrite = nullptr;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0) ||
        !CreatePipe(&errorRead, &errorWrite, &security, 0)) {
        if (outputRead) CloseHandle(outputRead);
        if (outputWrite) CloseHandle(outputWrite);
        if (errorRead) CloseHandle(errorRead);
        if (errorWrite) CloseHandle(errorWrite);
        error = "Cannot create Ctags output pipes.";
        return false;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0);
    nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(outputRead);
        CloseHandle(outputWrite);
        CloseHandle(errorRead);
        CloseHandle(errorWrite);
        error = "Cannot isolate Ctags input from the IDE console.";
        return false;
    }

    std::wstring command = quoteArgument(executable.wstring());
    for (const auto &argument : arguments) {
        command += L' ';
        command += quoteArgument(argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = outputWrite;
    startup.hStdError = errorWrite;
    startup.hStdInput = nullInput;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
    CloseHandle(nullInput);
    CloseHandle(outputWrite);
    CloseHandle(errorWrite);
    if (!created) {
        CloseHandle(outputRead);
        CloseHandle(errorRead);
        error = "Cannot start Ctags. Check TURBOIDE_CTAGS or PATH.";
        return false;
    }

    while (outputRead != INVALID_HANDLE_VALUE || errorRead != INVALID_HANDLE_VALUE) {
        readAvailable(outputRead, standardOutput);
        readAvailable(errorRead, standardError);
        if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) {
            readAvailable(outputRead, standardOutput);
            readAvailable(errorRead, standardError);
            if (outputRead == INVALID_HANDLE_VALUE && errorRead == INVALID_HANDLE_VALUE)
                break;
        }
    }
    const bool exited = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!exited) {
        error = "Cannot read the Ctags exit code.";
        return false;
    }
    return true;
}

std::filesystem::path findCtags() {
#if TURBOIDE_ALLOW_EXTERNAL_CTAGS
    DWORD required = GetEnvironmentVariableW(L"TURBOIDE_CTAGS", nullptr, 0);
    if (required) {
        std::wstring configured(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"TURBOIDE_CTAGS", configured.data(), required);
        if (length && length < required) {
            configured.resize(length);
            return configured;
        }
    }
#endif
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                            static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        return {};
    executable.resize(length);
    const auto bundled = std::filesystem::path(executable).parent_path() /
                        L"tools" / L"ctags" / L"ctags.exe";
    std::error_code error;
    return std::filesystem::is_regular_file(bundled, error) && !error ? bundled
                                                                      : std::filesystem::path{};
}

bool parsePositiveNumber(std::string_view text, int &number) {
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && number > 0;
}
}

bool parseCtagsOutput(std::string_view output, const std::filesystem::path &root,
                      std::vector<CodeSymbol> &symbols, std::string &error) {
    std::vector<CodeSymbol> parsed;
    size_t offset = 0;
    size_t lineNumber = 0;
    while (offset < output.size()) {
        ++lineNumber;
        const size_t end = output.find('\n', offset);
        auto line = output.substr(offset, end == std::string_view::npos
            ? output.size() - offset : end - offset);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        offset = end == std::string_view::npos ? output.size() : end + 1;
        if (line.empty() || line.rfind("!_TAG_", 0) == 0)
            continue;

        const size_t firstTab = line.find('\t');
        const size_t secondTab = firstTab == std::string_view::npos
            ? firstTab : line.find('\t', firstTab + 1);
        const size_t thirdTab = secondTab == std::string_view::npos
            ? secondTab : line.find('\t', secondTab + 1);
        if (firstTab == std::string_view::npos || secondTab == std::string_view::npos ||
            thirdTab == std::string_view::npos) {
            error = "Invalid Ctags record at output line " + std::to_string(lineNumber) + ".";
            return false;
        }
        const auto name = line.substr(0, firstTab);
        const auto path = line.substr(firstTab + 1, secondTab - firstTab - 1);
        const auto address = line.substr(secondTab + 1, thirdTab - secondTab - 1);
        const size_t terminator = address.find(";\"");
        int lineValue = 0;
        if (name.empty() || path.empty() || terminator == std::string_view::npos ||
            !parsePositiveNumber(address.substr(0, terminator), lineValue)) {
            error = "Invalid Ctags location at output line " + std::to_string(lineNumber) + ".";
            return false;
        }

        CodeSymbol symbol;
        symbol.name.assign(name);
        try {
            symbol.file = std::filesystem::u8path(path);
        } catch (...) {
            error = "Invalid UTF-8 path in Ctags output at line " + std::to_string(lineNumber) + ".";
            return false;
        }
        if (symbol.file.is_relative())
            symbol.file = root / symbol.file;
        symbol.file = symbol.file.lexically_normal();
        symbol.line = lineValue;

        size_t fieldStart = thirdTab + 1;
        while (fieldStart < line.size()) {
            const size_t fieldEnd = line.find('\t', fieldStart);
            const auto field = line.substr(fieldStart, fieldEnd == std::string_view::npos
                ? line.size() - fieldStart : fieldEnd - fieldStart);
            if (field.size() > 5 && field.substr(0, 5) == "kind:")
                symbol.kind.assign(field.substr(5));
            else if (field.size() > 5 && field.substr(0, 5) == "line:") {
                int fieldLine = 0;
                if (parsePositiveNumber(field.substr(5), fieldLine))
                    symbol.line = fieldLine;
            } else if (field.size() > 6 && field.substr(0, 6) == "scope:")
                symbol.scope.assign(field.substr(6));
            else if (field.size() > 9 && field.substr(0, 9) == "inherits:")
                symbol.inherits.assign(field.substr(9));
            else if (field.size() == 1 && symbol.kind.empty())
                symbol.kind.assign(field);
            if (fieldEnd == std::string_view::npos)
                break;
            fieldStart = fieldEnd + 1;
        }
        parsed.push_back(std::move(symbol));
    }
    symbols = std::move(parsed);
    error.clear();
    return true;
}

bool buildCtagsIndex(const std::vector<std::filesystem::path> &files,
                     const std::filesystem::path &root,
                     std::vector<CodeSymbol> &symbols, std::string &error) {
    if (files.empty()) {
        error = "There are no project source files to index.";
        return false;
    }
    const auto executable = findCtags();
    if (executable.empty()) {
#if TURBOIDE_ALLOW_EXTERNAL_CTAGS
        error = "Universal Ctags was not found. Set TURBOIDE_CTAGS for this developer build.";
#else
        error = "Bundled Universal Ctags was not found. Restore the complete TurboIDE portable package.";
#endif
        return false;
    }
    std::string version, versionError;
    DWORD exitCode = 0;
    if (!runProcess(executable, {L"--version"}, root, version, versionError, exitCode, error))
        return false;
    if (exitCode != 0 || version.find("Universal Ctags") == std::string::npos) {
        error = "The selected ctags.exe is not a compatible Universal Ctags build.";
        if (!versionError.empty()) error += " " + versionError;
        return false;
    }

    wchar_t temporaryDirectory[MAX_PATH]{};
    const DWORD directoryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
    if (!directoryLength || directoryLength >= MAX_PATH) {
        error = "Cannot locate the temporary directory for Ctags.";
        return false;
    }
    wchar_t listName[MAX_PATH]{};
    if (!GetTempFileNameW(temporaryDirectory, L"tid", 0, listName)) {
        error = "Cannot create a temporary Ctags file list.";
        return false;
    }
    const std::filesystem::path listPath(listName);
    {
        std::ofstream list(listPath, std::ios::binary | std::ios::trunc);
        for (const auto &file : files) {
            const auto absolute = std::filesystem::absolute(file).lexically_normal();
            const auto utf8 = absolute.u8string();
            list.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            list.write("\r\n", 2);
        }
        if (!list) {
            DeleteFileW(listPath.c_str());
            error = "Cannot write the temporary Ctags file list.";
            return false;
        }
    }

    std::string output, diagnostics;
    const bool started = runProcess(executable,
        {L"--options=NONE", L"--excmd=number", L"--fields=+n+i+Z", L"-f", L"-",
         L"-L", listPath.wstring()}, root, output, diagnostics, exitCode, error);
    DeleteFileW(listPath.c_str());
    if (!started)
        return false;
    if (exitCode != 0) {
        error = diagnostics.empty() ? "Universal Ctags failed to build the symbol index."
                                   : "Universal Ctags failed: " + diagnostics;
        return false;
    }
    return parseCtagsOutput(output, root, symbols, error);
}
