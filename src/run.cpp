#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "run.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace {
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

bool usable(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE;
}

HANDLE duplicateInheritable(HANDLE source) {
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate,
                         0, TRUE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return duplicate;
}

std::atomic_bool interruptRequested{false};

bool takeKeyboardInterrupt(HANDLE input) {
    INPUT_RECORD events[64]{};
    DWORD count = 0;
    if (!PeekConsoleInputW(input, events, static_cast<DWORD>(std::size(events)), &count) || count == 0)
        return false;

    for (DWORD i = 0; i < count; ++i) {
        if (events[i].EventType != KEY_EVENT || !events[i].Event.KeyEvent.bKeyDown)
            continue;
        const auto &key = events[i].Event.KeyEvent;
        const bool ctrlC = key.wVirtualKeyCode == 'C' &&
            (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED));
        if (ctrlC || key.wVirtualKeyCode == VK_CANCEL) {
            FlushConsoleInputBuffer(input);
            return true;
        }
    }
    return false;
}

BOOL WINAPI handleParentControlEvent(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        interruptRequested.store(true);
        return TRUE;
    }
    return FALSE;
}
}

HANDLE createUserScreenBuffer(HANDLE ideScreenBuffer) {
    if (!usable(ideScreenBuffer))
        return INVALID_HANDLE_VALUE;
    CONSOLE_SCREEN_BUFFER_INFO ideInfo{};
    if (!GetConsoleScreenBufferInfo(ideScreenBuffer, &ideInfo))
        return INVALID_HANDLE_VALUE;
    HANDLE screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    if (!usable(screen))
        return INVALID_HANDLE_VALUE;
    if (!SetConsoleScreenBufferSize(screen, ideInfo.dwSize)) {
        CloseHandle(screen);
        return INVALID_HANDLE_VALUE;
    }
    const SMALL_RECT viewport{0, 0,
        static_cast<SHORT>(ideInfo.srWindow.Right - ideInfo.srWindow.Left),
        static_cast<SHORT>(ideInfo.srWindow.Bottom - ideInfo.srWindow.Top)};
    if (!SetConsoleWindowInfo(screen, TRUE, &viewport)) {
        CloseHandle(screen);
        return INVALID_HANDLE_VALUE;
    }
    CONSOLE_SCREEN_BUFFER_INFO startupInfo{};
    const HANDLE startupBuffer = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if (usable(startupBuffer) && GetConsoleScreenBufferInfo(startupBuffer, &startupInfo))
        attributes = startupInfo.wAttributes;
    SetConsoleTextAttribute(screen, attributes);
    return screen;
}

namespace {
bool runShell(HANDLE screen, HANDLE consoleInput,
              const std::filesystem::path &workingDirectory, std::string &error) {
    HANDLE childInput = duplicateInheritable(consoleInput);
    HANDLE childOutput = duplicateInheritable(screen);
    if (!usable(childInput) || !usable(childOutput)) {
        if (usable(childInput)) CloseHandle(childInput);
        if (usable(childOutput)) CloseHandle(childOutput);
        error = "Cannot prepare the console handles for cmd.exe.";
        return false;
    }

    DWORD size = GetEnvironmentVariableW(L"ComSpec", nullptr, 0);
    std::wstring executable;
    if (size > 1) {
        executable.resize(size);
        const DWORD actual = GetEnvironmentVariableW(L"ComSpec", executable.data(), size);
        executable.resize(actual);
    } else {
        executable = L"cmd.exe";
    }
    std::wstring command = quote(executable) + L" /D";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = childOutput;
    PROCESS_INFORMATION process{};
    const wchar_t *directory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NEW_PROCESS_GROUP, nullptr, directory, &startup, &process);
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (!created) {
        error = "Cannot start cmd.exe.";
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}

RunResult runProgram(const RunRequest &request, HANDLE screen, HANDLE ideScreenBuffer,
                     HANDLE consoleInput) {
    RunResult result;
    if (!usable(screen)) {
        result.error = "The user screen buffer is unavailable.";
        return result;
    }
    if (!usable(ideScreenBuffer)) {
        result.error = "Cannot access the IDE screen buffer (CONOUT$).";
        return result;
    }
    if (!usable(consoleInput)) {
        result.error = "Cannot access the console input buffer (CONIN$).";
        return result;
    }

    DWORD inputMode = 0;
    const bool restoreInputMode = GetConsoleMode(consoleInput, &inputMode) != FALSE;

    HANDLE childInput = duplicateInheritable(consoleInput);
    HANDLE childOutput = duplicateInheritable(screen);
    if (!usable(childInput) || !usable(childOutput)) {
        if (usable(childInput)) CloseHandle(childInput);
        if (usable(childOutput)) CloseHandle(childOutput);
        result.error = "Cannot prepare console input for the program.";
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = childOutput;

    std::wstring command = quote(request.executable.wstring());
    for (const auto &argument : request.arguments) {
        command += L' ';
        command += quote(argument);
    }
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(request.executable.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NEW_PROCESS_GROUP, nullptr, request.workingDirectory.c_str(), &startup, &process);
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (!created) {
        SetConsoleActiveScreenBuffer(ideScreenBuffer);
        result.error = "Cannot start the program.";
        return result;
    }

    result.started = true;
    interruptRequested.store(false);
    SetConsoleCtrlHandler(handleParentControlEvent, TRUE);
    if (!SetConsoleActiveScreenBuffer(screen)) {
        TerminateProcess(process.hProcess, ERROR_CANCELLED);
        WaitForSingleObject(process.hProcess, INFINITE);
        SetConsoleCtrlHandler(handleParentControlEvent, FALSE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.error = "Cannot activate the user screen buffer.";
        return result;
    }
    bool interrupted = false;
    auto interruptDeadline = std::chrono::steady_clock::time_point{};
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        if (!interrupted && (interruptRequested.exchange(false) || takeKeyboardInterrupt(consoleInput))) {
            interrupted = true;
            interruptDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process.dwProcessId);
        }
        if (interrupted && std::chrono::steady_clock::now() >= interruptDeadline)
            TerminateProcess(process.hProcess, ERROR_CANCELLED);
    }
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    SetConsoleCtrlHandler(handleParentControlEvent, FALSE);
    if (restoreInputMode)
        SetConsoleMode(consoleInput, inputMode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (!SetConsoleActiveScreenBuffer(ideScreenBuffer)) {
        result.error = "The program ended, but the IDE screen could not be restored.";
        return result;
    }
    result.screenBuffer = screen;
    return result;
}

bool showUserScreen(HANDLE screenBuffer, HANDLE ideScreenBuffer, HANDLE consoleInput,
                    bool allowShell, const std::filesystem::path &workingDirectory,
                    std::string &error) {
    if (!usable(screenBuffer) || !SetConsoleActiveScreenBuffer(screenBuffer))
        return false;

    const std::wstring hint = allowShell
        ? L"Alt-F5: return to IDE  |  Enter: shell (type exit to return here)"
        : L"Alt-F5: return to IDE";
    std::vector<CHAR_INFO> savedRow;
    SMALL_RECT hintRect{};
    auto restoreHint = [&] {
        if (savedRow.empty())
            return;
        COORD size{static_cast<SHORT>(savedRow.size()), 1};
        COORD origin{0, 0};
        WriteConsoleOutputW(screenBuffer, savedRow.data(), size, origin, &hintRect);
        savedRow.clear();
    };
    auto showHint = [&] {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(screenBuffer, &info))
            return;
        hintRect = {info.srWindow.Left, info.srWindow.Bottom,
                    info.srWindow.Right, info.srWindow.Bottom};
        const SHORT width = static_cast<SHORT>(hintRect.Right - hintRect.Left + 1);
        COORD size{width, 1};
        COORD origin{0, 0};
        savedRow.resize(static_cast<size_t>(width));
        SMALL_RECT readRect = hintRect;
        if (!ReadConsoleOutputW(screenBuffer, savedRow.data(), size, origin, &readRect)) {
            savedRow.clear();
            return;
        }
        hintRect = readRect;
        std::vector<CHAR_INFO> cells(static_cast<size_t>(width));
        const WORD attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
            FOREGROUND_INTENSITY | BACKGROUND_BLUE;
        for (auto &cell : cells) {
            cell.Char.UnicodeChar = L' ';
            cell.Attributes = attributes;
        }
        const size_t count = (std::min)(hint.size(), cells.size());
        for (size_t i = 0; i < count; ++i)
            cells[i].Char.UnicodeChar = hint[i];
        if (!WriteConsoleOutputW(screenBuffer, cells.data(), size, origin, &hintRect))
            restoreHint();
    };
    showHint();

    INPUT_RECORD event{};
    DWORD read = 0;
    bool ok = true;
    for (;;) {
        if (!ReadConsoleInputW(consoleInput, &event, 1, &read) || read != 1) {
            ok = false;
            break;
        }
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
            continue;
        const auto &key = event.Event.KeyEvent;
        const bool altF5 = key.wVirtualKeyCode == VK_F5 &&
            (key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED));
        if (altF5 || key.wVirtualKeyCode == VK_ESCAPE)
            break;
        if (allowShell && key.wVirtualKeyCode == VK_RETURN) {
            restoreHint();
            if (!runShell(screenBuffer, consoleInput, workingDirectory, error)) {
                ok = false;
                break;
            }
            showHint();
        }
    }
    restoreHint();
    return SetConsoleActiveScreenBuffer(ideScreenBuffer) && ok;
}

bool runCommandPrompt(HANDLE screenBuffer, HANDLE ideScreenBuffer, HANDLE consoleInput,
                     const std::filesystem::path &workingDirectory, std::string &error) {
    if (!usable(screenBuffer) || !SetConsoleActiveScreenBuffer(screenBuffer))
        return false;
    const bool ok = runShell(screenBuffer, consoleInput, workingDirectory, error);
    const bool restored = SetConsoleActiveScreenBuffer(ideScreenBuffer) != FALSE;
    return ok && restored;
}
