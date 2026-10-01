#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

struct DebugBreakpoint {
    std::filesystem::path file;
    int line = 0;
};

struct DebugVariable {
    std::string name;
    std::string value;
    std::string type;
};

struct DebugStop {
    std::string reason;
    std::string signalName;
    std::string signalMeaning;
    std::filesystem::path file;
    int line = 0;
    bool exited = false;
    int exitCode = 0;
    bool hasExitCode = false;
};

class GdbSession {
public:
    GdbSession() = default;
    ~GdbSession();
    GdbSession(const GdbSession &) = delete;
    GdbSession &operator=(const GdbSession &) = delete;

    bool start(const std::filesystem::path &executable,
               const std::filesystem::path &workingDirectory,
               const std::vector<std::wstring> &arguments,
               const std::vector<DebugBreakpoint> &breakpoints,
               HANDLE userScreen, HANDLE consoleInput,
               DebugStop &firstStop, std::string &error);
    bool resume(DebugStop &stop, std::string &error);
    bool stepInto(DebugStop &stop, std::string &error);
    bool stepOver(DebugStop &stop, std::string &error);
    bool setBreakpoints(const std::vector<DebugBreakpoint> &breakpoints,
                        std::string &error);
    std::vector<DebugVariable> locals(std::string &error);
    bool evaluate(const std::string &expression, std::string &value, std::string &error);
    bool stop(std::string &error);
    bool active() const { return usable(gdbProcess_); }
    bool running() const { return running_; }

private:
    static bool usable(HANDLE handle);
    bool launchGdb(std::string &error);
    bool sendCommand(const std::string &command, std::string &reply, std::string &error);
    bool readLine(std::string &line, DWORD timeoutMs);
    bool waitForStop(DebugStop &stop, std::string &error);
    bool executeAndWait(const char *command, DebugStop &stop, std::string &error);
    bool insertBreakpoint(const std::string &location, bool temporary,
                          std::string &error);
    static bool takeKeyboardInterrupt(HANDLE input);
    void closeHandles();

    HANDLE gdbProcess_ = INVALID_HANDLE_VALUE;
    HANDLE gdbInput_ = INVALID_HANDLE_VALUE;
    HANDLE gdbOutput_ = INVALID_HANDLE_VALUE;
    HANDLE debuggeeProcess_ = INVALID_HANDLE_VALUE;
    HANDLE debuggeeThread_ = INVALID_HANDLE_VALUE;
    HANDLE processJob_ = INVALID_HANDLE_VALUE;
    HANDLE userScreen_ = INVALID_HANDLE_VALUE;
    HANDLE consoleInput_ = INVALID_HANDLE_VALUE;
    std::string inputBuffer_;
    std::vector<std::string> pendingLines_;
    unsigned token_ = 1;
    bool running_ = false;
    bool handlerRegistered_ = false;
};
