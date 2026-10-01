#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "debugger.h"
#include "gdb_mi.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace {
using gdbmi::recordEnd;
using gdbmi::stringAttribute;
std::atomic_bool interruptRequested{false};

BOOL WINAPI handleDebugControlEvent(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        interruptRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

std::wstring quoteWindowsArgument(const std::wstring &argument) {
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

std::string utf8(const std::wstring &value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), needed, nullptr, nullptr);
    return result;
}

std::string miQuote(const std::string &value) {
    std::string result = "\"";
    for (char ch : value) {
        if (ch == '\\' || ch == '"') result += '\\';
        if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else result += ch;
    }
    result += '"';
    return result;
}

bool isSimpleWatchIdentifier(const std::string &expression) {
    if (expression.empty() ||
        !((expression[0] >= 'A' && expression[0] <= 'Z') ||
          (expression[0] >= 'a' && expression[0] <= 'z') || expression[0] == '_'))
        return false;
    for (const char ch : expression)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_'))
            return false;
    return true;
}

std::wstring fromUtf8(const std::string &value) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return std::filesystem::path(value).wstring();
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), needed);
    return result;
}

HANDLE inheritableDuplicate(HANDLE source) {
    HANDLE result = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &result,
                         0, TRUE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return result;
}

std::string trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}
}

GdbSession::~GdbSession() {
    std::string ignored;
    stop(ignored);
}

bool GdbSession::usable(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE;
}

bool GdbSession::launchGdb(std::string &error) {
    wchar_t configured[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"TURBOIDE_GDB", configured, MAX_PATH);
    std::wstring executable;
    if (length > 0 && length < MAX_PATH)
        executable.assign(configured, length);
    else {
        wchar_t found[MAX_PATH]{};
        const DWORD foundLength = SearchPathW(nullptr, L"gdb.exe", nullptr, MAX_PATH,
                                              found, nullptr);
        if (!foundLength || foundLength >= MAX_PATH) {
            error = "GDB was not found. Set TURBOIDE_GDB or add gdb.exe to PATH.";
            return false;
        }
        executable.assign(found, foundLength);
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE inputRead = nullptr, inputWrite = nullptr;
    HANDLE outputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0) ||
        !CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        error = "Cannot create GDB/MI pipes.";
        if (usable(inputRead)) CloseHandle(inputRead);
        if (usable(inputWrite)) CloseHandle(inputWrite);
        if (usable(outputRead)) CloseHandle(outputRead);
        if (usable(outputWrite)) CloseHandle(outputWrite);
        return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    std::wstring command = quoteWindowsArgument(executable) + L" --interpreter=mi2 --quiet";
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (!created) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = "Cannot start GDB.";
        return false;
    }
    if (usable(processJob_) && !AssignProcessToJobObject(processJob_, process.hProcess)) {
        TerminateProcess(process.hProcess, ERROR_CANCELLED);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = "Cannot place GDB in TurboIDE's process job.";
        return false;
    }
    CloseHandle(process.hThread);
    gdbProcess_ = process.hProcess;
    gdbInput_ = inputWrite;
    gdbOutput_ = outputRead;
    return true;
}

bool GdbSession::readLine(std::string &line, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        const size_t newline = inputBuffer_.find('\n');
        if (newline != std::string::npos) {
            line = trimLine(inputBuffer_.substr(0, newline + 1));
            inputBuffer_.erase(0, newline + 1);
            return true;
        }
        DWORD available = 0;
        if (PeekNamedPipe(gdbOutput_, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[4096];
            DWORD read = 0;
            if (!ReadFile(gdbOutput_, buffer,
                          std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || !read)
                return false;
            inputBuffer_.append(buffer, read);
            continue;
        }
        if (!usable(gdbProcess_) || WaitForSingleObject(gdbProcess_, 0) == WAIT_OBJECT_0)
            return false;
        if (GetTickCount64() >= deadline)
            return false;
        Sleep(10);
    }
}

bool GdbSession::sendCommand(const std::string &command, std::string &reply,
                             std::string &error) {
    const unsigned token = token_++;
    const std::string prefix = std::to_string(token);
    const std::string text = prefix + command + "\n";
    DWORD written = 0;
    if (!WriteFile(gdbInput_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size()) {
        error = "Cannot write to the GDB/MI channel.";
        return false;
    }
    for (;;) {
        auto found = std::find_if(pendingLines_.begin(), pendingLines_.end(),
            [&prefix](const std::string &line) { return line.rfind(prefix + "^", 0) == 0; });
        if (found != pendingLines_.end()) {
            reply = *found;
            pendingLines_.erase(found);
            if (reply.rfind(prefix + "^error", 0) == 0) {
                error = stringAttribute(reply, "msg");
                if (error.empty()) error = "GDB rejected command: " + command;
                return false;
            }
            return true;
        }
        std::string line;
        if (!readLine(line, 10000)) {
            error = "Timed out waiting for GDB/MI.";
            return false;
        }
        pendingLines_.push_back(std::move(line));
    }
}

bool GdbSession::waitForStop(DebugStop &stop, std::string &error) {
    ULONGLONG interruptDeadline = 0;
    bool interruptSent = false;
    for (;;) {
        auto found = std::find_if(pendingLines_.begin(), pendingLines_.end(),
            [](const std::string &line) { return line.rfind("*stopped", 0) == 0; });
        if (found != pendingLines_.end()) {
            const std::string event = *found;
            pendingLines_.erase(found);
            stop.reason = stringAttribute(event, "reason");
            stop.signalName = stringAttribute(event, "signal-name");
            stop.signalMeaning = stringAttribute(event, "signal-meaning");
            const std::string file = stringAttribute(event, "fullname");
            if (!file.empty()) stop.file = std::filesystem::path(fromUtf8(file));
            else {
                const std::string relative = stringAttribute(event, "file");
                if (!relative.empty()) stop.file = std::filesystem::path(fromUtf8(relative));
            }
            const std::string line = stringAttribute(event, "line");
            stop.line = line.empty() ? 0 : std::atoi(line.c_str());
            stop.exited = stop.reason.rfind("exited", 0) == 0;
            const std::string exitCode = stringAttribute(event, "exit-code");
            if (!exitCode.empty()) {
                stop.exitCode = static_cast<int>(std::strtol(exitCode.c_str(), nullptr, 0));
                stop.hasExitCode = true;
            }
            running_ = false;
            return true;
        }

        if (!interruptSent &&
            (interruptRequested.exchange(false) || takeKeyboardInterrupt(consoleInput_))) {
            const std::string command = std::to_string(token_++) + "-exec-interrupt\n";
            DWORD written = 0;
            WriteFile(gdbInput_, command.data(), static_cast<DWORD>(command.size()),
                      &written, nullptr);
            interruptDeadline = GetTickCount64() + 2000;
            interruptSent = true;
        }
        if (interruptSent && GetTickCount64() >= interruptDeadline &&
            usable(debuggeeProcess_) && WaitForSingleObject(debuggeeProcess_, 0) == WAIT_TIMEOUT) {
            TerminateProcess(debuggeeProcess_, ERROR_CANCELLED);
            interruptDeadline = GetTickCount64() + 1000;
        }
        if (usable(debuggeeProcess_) && WaitForSingleObject(debuggeeProcess_, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(debuggeeProcess_, &code);
            stop.reason = "exited";
            stop.exited = true;
            stop.exitCode = static_cast<int>(code);
            stop.hasExitCode = true;
            running_ = false;
            return true;
        }

        std::string line;
        if (!readLine(line, 50)) {
            if (!usable(gdbProcess_) || WaitForSingleObject(gdbProcess_, 0) == WAIT_OBJECT_0) {
                error = "GDB exited while the program was running.";
                return false;
            }
            continue;
        }
        pendingLines_.push_back(std::move(line));
    }
}

bool GdbSession::insertBreakpoint(const std::string &location, bool temporary,
                                  std::string &error) {
    std::string reply;
    const std::string command = std::string("-break-insert ") +
        (temporary ? "-t " : "") + miQuote(location);
    return sendCommand(command, reply, error);
}

bool GdbSession::start(const std::filesystem::path &executable,
                       const std::filesystem::path &workingDirectory,
                       const std::vector<std::wstring> &arguments,
                       const std::vector<DebugBreakpoint> &breakpoints,
                       HANDLE userScreen, HANDLE consoleInput,
                       DebugStop &firstStop, std::string &error) {
    stop(error);
    error.clear();
    userScreen_ = userScreen;
    consoleInput_ = consoleInput;
    supportsMayCallFunctions_ = false;
    mayCallFunctionsEnabled_ = true;

    processJob_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!usable(processJob_) || !SetInformationJobObject(processJob_, JobObjectExtendedLimitInformation,
            &jobLimits, sizeof(jobLimits))) {
        error = "Cannot create the debugger process job.";
        closeHandles();
        return false;
    }

    HANDLE childInput = inheritableDuplicate(consoleInput);
    HANDLE childOutput = inheritableDuplicate(userScreen);
    if (!usable(childInput) || !usable(childOutput)) {
        if (usable(childInput)) CloseHandle(childInput);
        if (usable(childOutput)) CloseHandle(childOutput);
        error = "Cannot prepare console handles for the debuggee.";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = childOutput;
    std::wstring command = quoteWindowsArgument(executable.wstring());
    for (const auto &argument : arguments) {
        command += L' ';
        command += quoteWindowsArgument(argument);
    }
    PROCESS_INFORMATION target{};
    const wchar_t *directory = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
        TRUE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, nullptr, directory, &startup, &target);
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (!created) {
        error = "Cannot start the debuggee.";
        closeHandles();
        return false;
    }
    if (!AssignProcessToJobObject(processJob_, target.hProcess)) {
        TerminateProcess(target.hProcess, ERROR_CANCELLED);
        CloseHandle(target.hThread);
        CloseHandle(target.hProcess);
        error = "Cannot place the debuggee in TurboIDE's process job.";
        closeHandles();
        return false;
    }
    debuggeeProcess_ = target.hProcess;
    debuggeeThread_ = target.hThread;

    if (!launchGdb(error)) {
        stop(error);
        return false;
    }
    handlerRegistered_ = SetConsoleCtrlHandler(handleDebugControlEvent, TRUE) != FALSE;
    interruptRequested.store(false);

    std::string reply;
    if (!sendCommand("-gdb-set pagination off", reply, error) ||
        !sendCommand("-gdb-set confirm off", reply, error) ||
        !sendCommand("-gdb-set may-write-memory on", reply, error) ||
        !sendCommand("-gdb-set may-write-registers on", reply, error) ||
        !sendCommand("-file-exec-and-symbols " + miQuote(utf8(executable.wstring())), reply, error)) {
        error = "GDB setup failed: " + error;
        stop(error);
        return false;
    }
    std::string featureError;
    if (sendCommand("-gdb-show may-call-functions", reply, featureError)) {
        supportsMayCallFunctions_ = true;
        mayCallFunctionsEnabled_ = stringAttribute(reply, "value") != "off";
    }
    std::string sourceFilesReply;
    std::string sourceFilesError;
    if (sendCommand("-file-list-exec-source-files", sourceFilesReply, sourceFilesError) &&
        sourceFilesReply.find("files=[]") != std::string::npos) {
        error = "GDB loaded the executable but found no source debug information: " +
            executable.u8string() + ". Rebuild with a GCC/GDB-compatible toolchain.";
        stop(error);
        return false;
    }
    const std::string attach = "-target-attach " + std::to_string(target.dwProcessId);
    const std::string attachToken = std::to_string(token_);
    const std::string attachText = attachToken + attach + "\n";
    DWORD attachWritten = 0;
    if (!WriteFile(gdbInput_, attachText.data(), static_cast<DWORD>(attachText.size()),
                   &attachWritten, nullptr) || attachWritten != attachText.size()) {
        error = "Cannot attach GDB to the debuggee.";
        stop(error);
        return false;
    }
    ++token_;
    bool attachStopped = false;
    const ULONGLONG attachDeadline = GetTickCount64() + 10000;
    while (!attachStopped && GetTickCount64() < attachDeadline) {
        std::string line;
        if (!readLine(line, 100)) continue;
        if (line.rfind("*stopped", 0) == 0) {
            attachStopped = true;
        } else if (line.rfind(attachToken + "^error", 0) == 0) {
            error = stringAttribute(line, "msg");
            error = "GDB failed while attaching to the debuggee (PID " +
                std::to_string(target.dwProcessId) + "): " +
                (error.empty() ? "unknown GDB error" : error);
            break;
        } else {
            pendingLines_.push_back(std::move(line));
        }
    }
    if (!attachStopped) {
        if (error.empty()) error = "GDB did not stop after attaching to the debuggee.";
        stop(error);
        return false;
    }

    if (!setBreakpoints(breakpoints, error)) {
        stop(error);
        return false;
    }
    if (ResumeThread(debuggeeThread_) == static_cast<DWORD>(-1)) {
        error = "Cannot resume the debuggee's initial thread.";
        stop(error);
        return false;
    }
    std::string continueReply;
    if (!sendCommand("-exec-continue", continueReply, error)) {
        error = "GDB could not start the debuggee: " + error;
        stop(error);
        return false;
    }
    running_ = true;
    if (!SetConsoleActiveScreenBuffer(userScreen_)) {
        error = "Cannot activate the User Screen for debugging.";
        stop(error);
        return false;
    }
    return waitForStop(firstStop, error);
}

bool GdbSession::setBreakpoints(const std::vector<DebugBreakpoint> &breakpoints,
                               std::string &error) {
    std::string reply;
    if (!sendCommand("-break-delete", reply, error))
        return false;
    if (!insertBreakpoint("main", true, error)) {
        error = "GDB could not set its initial breakpoint at main: " + error;
        return false;
    }
    for (const auto &breakpoint : breakpoints) {
        if (breakpoint.line <= 0) continue;
        const auto fullPath = std::filesystem::absolute(breakpoint.file).lexically_normal();
        const std::string location = utf8(fullPath.wstring()) + ":" + std::to_string(breakpoint.line);
        if (!insertBreakpoint(location, false, error)) {
            error = "GDB could not set breakpoint at " + location + ": " + error;
            return false;
        }
    }
    return true;
}

bool GdbSession::executeAndWait(const char *command, DebugStop &stop, std::string &error) {
    std::string reply;
    if (!sendCommand(command, reply, error)) return false;
    running_ = true;
    return waitForStop(stop, error);
}

bool GdbSession::resume(DebugStop &stop, std::string &error) {
    return executeAndWait("-exec-continue", stop, error);
}

bool GdbSession::stepInto(DebugStop &stop, std::string &error) {
    return executeAndWait("-exec-step", stop, error);
}

bool GdbSession::stepOver(DebugStop &stop, std::string &error) {
    return executeAndWait("-exec-next", stop, error);
}

std::vector<DebugVariable> GdbSession::locals(std::string &error) {
    std::string reply;
    std::vector<DebugVariable> result;
    if (!sendCommand("-stack-list-variables --simple-values", reply, error))
        return result;
    const size_t variables = reply.find("variables=[");
    if (variables == std::string::npos) return result;
    size_t pos = variables + 11;
    while ((pos = reply.find("{name=\"", pos)) != std::string::npos) {
        const size_t end = recordEnd(reply, pos);
        if (end == std::string::npos) break;
        const std::string object = reply.substr(pos, end - pos);
        DebugVariable variable;
        variable.name = stringAttribute(object, "name");
        variable.value = stringAttribute(object, "value");
        variable.type = stringAttribute(object, "type");
        if (!variable.name.empty()) result.push_back(std::move(variable));
        pos = end;
    }
    return result;
}

bool GdbSession::evaluate(const std::string &expression, std::string &value,
                          std::string &error) {
    error.clear();
    value.clear();
    if (running_) { error = "Watches can only be evaluated while the program is stopped."; return false; }
    if (expression.find('$') != std::string::npos) {
        error = "Register expressions are not allowed in Watches.";
        return false;
    }
    if (!supportsMayCallFunctions_ && !isSimpleWatchIdentifier(expression)) {
        error = "This GDB cannot disable function calls; on this version, Watches support simple variable names only.";
        return false;
    }
    std::string reply, firstError;
    const auto set = [&](const char *setting, bool enabled, std::string &why) {
        std::string ignored;
        return sendCommand(std::string("-gdb-set ") + setting + (enabled ? " on" : " off"), ignored, why);
    };
    if ((supportsMayCallFunctions_ && !set("may-call-functions", false, firstError)) ||
        !set("may-write-memory", false, firstError)) {
        error = firstError;
    } else {
        sendCommand("-data-evaluate-expression " + miQuote(expression), reply, error);
        if (error.empty()) {
            value = stringAttribute(reply, "value");
            if (value.empty() && reply.find("value=\"\"") == std::string::npos)
                error = "GDB returned no value for this expression.";
        }
    }
    std::string restoreError;
    const bool callsRestored = !supportsMayCallFunctions_ ||
        set("may-call-functions", mayCallFunctionsEnabled_, restoreError);
    const bool memoryRestored = set("may-write-memory", true, restoreError);
    if (!callsRestored || !memoryRestored) {
        error = "Could not restore GDB's write permissions: " + restoreError;
        return false;
    }
    return error.empty();
}

bool GdbSession::stop(std::string &error) {
    if (usable(debuggeeProcess_)) {
        if (WaitForSingleObject(debuggeeProcess_, 0) == WAIT_TIMEOUT) {
            if (running_ && usable(gdbInput_)) {
                const std::string interrupt = std::to_string(token_++) + "-exec-interrupt\n";
                DWORD written = 0;
                WriteFile(gdbInput_, interrupt.data(), static_cast<DWORD>(interrupt.size()),
                          &written, nullptr);
            }
            if (WaitForSingleObject(debuggeeProcess_, 1500) == WAIT_TIMEOUT &&
                !TerminateProcess(debuggeeProcess_, ERROR_CANCELLED))
                error = "Cannot terminate the debuggee created by TurboIDE.";
            if (WaitForSingleObject(debuggeeProcess_, 2000) == WAIT_TIMEOUT && error.empty())
                error = "The debuggee did not exit after termination.";
        }
    }
    if (usable(gdbProcess_) && WaitForSingleObject(gdbProcess_, 0) == WAIT_TIMEOUT) {
        if (usable(gdbInput_)) {
            const std::string quit = std::to_string(token_++) + "-gdb-exit\n";
            DWORD written = 0;
            WriteFile(gdbInput_, quit.data(), static_cast<DWORD>(quit.size()), &written, nullptr);
        }
        if (WaitForSingleObject(gdbProcess_, 1000) == WAIT_TIMEOUT) {
            TerminateProcess(gdbProcess_, ERROR_CANCELLED);
            WaitForSingleObject(gdbProcess_, 2000);
        }
    }
    closeHandles();
    return error.empty();
}

bool GdbSession::takeKeyboardInterrupt(HANDLE input) {
    INPUT_RECORD events[64]{};
    DWORD count = 0;
    if (!input || !PeekConsoleInputW(input, events, 64, &count) || !count)
        return false;
    for (DWORD i = 0; i < count; ++i) {
        if (events[i].EventType == KEY_EVENT && events[i].Event.KeyEvent.bKeyDown &&
            (events[i].Event.KeyEvent.wVirtualKeyCode == VK_CANCEL ||
             (events[i].Event.KeyEvent.wVirtualKeyCode == 'C' &&
              (events[i].Event.KeyEvent.dwControlKeyState &
               (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))))) {
            FlushConsoleInputBuffer(input);
            return true;
        }
    }
    return false;
}

void GdbSession::closeHandles() {
    if (handlerRegistered_) {
        SetConsoleCtrlHandler(handleDebugControlEvent, FALSE);
        handlerRegistered_ = false;
    }
    for (HANDLE *handle : {&gdbInput_, &gdbOutput_, &gdbProcess_,
                           &debuggeeThread_, &debuggeeProcess_, &processJob_}) {
        if (usable(*handle)) CloseHandle(*handle);
        *handle = INVALID_HANDLE_VALUE;
    }
    userScreen_ = INVALID_HANDLE_VALUE;
    consoleInput_ = INVALID_HANDLE_VALUE;
    inputBuffer_.clear();
    pendingLines_.clear();
    running_ = false;
}
