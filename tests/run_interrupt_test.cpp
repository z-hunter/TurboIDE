#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/run.h"

#include <chrono>
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <thread>

int wmain(int argc, wchar_t **argv) {
    if (argc > 1) {
        DWORD mode = 0;
        const bool normal = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
            (mode & (ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) ==
                (ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        const bool direct = std::wcscmp(argv[1], L"--direct") == 0;
        if (normal == direct)
            return 13;
        Sleep(INFINITE);
    }

    FreeConsole();
    if (!AllocConsole())
        return 10;
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode))
        return 11;
    HANDLE userScreen = createUserScreenBuffer(output);
    if (userScreen == INVALID_HANDLE_VALUE)
        return 11;
    const DWORD turboVisionMode = mode & ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    SetConsoleMode(input, turboVisionMode);

    const auto self = std::filesystem::absolute(argv[0]);
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread watchdog([done] {
        if (WaitForSingleObject(done, 5000) == WAIT_TIMEOUT)
            TerminateProcess(GetCurrentProcess(), 12);
    });
    const auto started = std::chrono::steady_clock::now();
    const auto run = [&](const wchar_t *argument, const ConsoleInputMode &inputMode) {
        std::thread inject([input] {
            Sleep(300);
            INPUT_RECORD event{};
            event.EventType = KEY_EVENT;
            event.Event.KeyEvent.bKeyDown = TRUE;
            event.Event.KeyEvent.wVirtualKeyCode = 'C';
            event.Event.KeyEvent.uChar.UnicodeChar = L'c';
            event.Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
            DWORD written = 0;
            WriteConsoleInputW(input, &event, 1, &written);
        });
        const RunResult result = runProgram({self, self.parent_path(), {argument}},
                                            userScreen, output, input, inputMode);
        inject.join();
        return result;
    };
    const RunResult normal = run(L"--normal", {mode, true, false});
    DWORD normalRestoredMode = 0;
    GetConsoleMode(input, &normalRestoredMode);
    const RunResult direct = run(L"--direct", {mode, true, true});
    SetEvent(done);
    watchdog.join();
    CloseHandle(done);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    DWORD directRestoredMode = 0;
    GetConsoleMode(input, &directRestoredMode);
    SetConsoleMode(input, mode);
    CloseHandle(input);
    CloseHandle(output);
    CloseHandle(userScreen);
    FreeConsole();
    return normal.started && normal.exitCode != STILL_ACTIVE && normal.exitCode != 13 &&
        direct.started && direct.exitCode != STILL_ACTIVE && direct.exitCode != 13 &&
        normalRestoredMode == turboVisionMode && directRestoredMode == turboVisionMode &&
        elapsed < std::chrono::seconds(4) ? 0 : 12;
}
