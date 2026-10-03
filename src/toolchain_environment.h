#pragma once

#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

inline std::vector<wchar_t> toolchainEnvironment(const std::filesystem::path &executable) {
    const std::wstring directory = executable.parent_path().wstring();
    if (directory.empty())
        return {};

    LPWCH source = GetEnvironmentStringsW();
    if (!source)
        return {};
    std::vector<std::wstring> variables;
    for (const wchar_t *entry = source; *entry; entry += std::wcslen(entry) + 1)
        variables.emplace_back(entry);
    FreeEnvironmentStringsW(source);

    bool pathFound = false;
    for (auto &entry : variables)
        if (entry.size() >= 5 && _wcsnicmp(entry.c_str(), L"PATH=", 5) == 0) {
            entry = entry.substr(0, 5) + directory + L";" + entry.substr(5);
            pathFound = true;
            break;
        }
    if (!pathFound)
        variables.emplace_back(L"PATH=" + directory);

    std::vector<wchar_t> environment;
    for (const auto &entry : variables) {
        environment.insert(environment.end(), entry.begin(), entry.end());
        environment.push_back(L'\0');
    }
    environment.push_back(L'\0');
    return environment;
}
