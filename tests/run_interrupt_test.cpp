#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/run.h"

#include <chrono>
#include <atomic>
#include <filesystem>
#include <thread>

int wmain(int argc, wchar_t **argv) {
    if (argc > 1)
        Sleep(INFINITE);

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
    SetConsoleMode(input, mode & ~ENABLE_PROCESSED_INPUT);

    const auto self = std::filesystem::absolute(argv[0]);
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread watchdog([done] {
        if (WaitForSingleObject(done, 5000) == WAIT_TIMEOUT)
            TerminateProcess(GetCurrentProcess(), 12);
    });
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

    const auto started = std::chrono::steady_clock::now();
    const RunResult result = runProgram({self, self.parent_path(), {L"--child"}}, output, input);
    inject.join();
    SetEvent(done);
    watchdog.join();
    CloseHandle(done);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    SetConsoleMode(input, mode);
    CloseHandle(input);
    CloseHandle(output);
    FreeConsole();
    return result.started && result.exitCode != STILL_ACTIVE &&
        elapsed < std::chrono::seconds(4) ? 0 : 12;
}
