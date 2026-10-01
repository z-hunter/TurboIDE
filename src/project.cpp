#include "project.h"

#include <fstream>
#include <string>
#include <string_view>
#include <windows.h>

namespace {
std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool utf8ToWide(std::string_view text, std::wstring &wide) {
    if (text.empty()) {
        wide.clear();
        return true;
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size)
        return false;
    wide.resize(static_cast<size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), wide.data(), size) == size;
}

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size, nullptr, nullptr)) return {};
    return result;
}

std::string utf8Path(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

bool addPath(std::string_view value, const std::filesystem::path &root,
             std::vector<std::filesystem::path> &paths) {
    std::wstring wide;
    if (!utf8ToWide(value, wide))
        return false;
    auto path = std::filesystem::path(wide);
    if (path.is_relative())
        path = root / path;
    paths.push_back(path.lexically_normal());
    return true;
}
}

bool loadProject(const std::filesystem::path &file, Project &project, std::string &error) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "Cannot read project file.";
        return false;
    }

    Project loaded;
    loaded.file = std::filesystem::absolute(file).lexically_normal();
    const auto root = loaded.file.parent_path();
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3);
        auto value = trim(line);
        if (value.empty() || value.front() == '#')
            continue;

        const auto equals = value.find('=');
        if (equals == std::string_view::npos) {
            error = "Line " + std::to_string(lineNumber) + ": expected key=value.";
            return false;
        }
        const auto key = trim(value.substr(0, equals));
        const auto setting = trim(value.substr(equals + 1));
        if (setting.empty()) {
            error = "Line " + std::to_string(lineNumber) + ": value is empty.";
            return false;
        }

        if (key == "source") {
            if (!addPath(setting, root, loaded.sources)) {
                error = "Line " + std::to_string(lineNumber) + ": path is not valid UTF-8.";
                return false;
            }
        } else if (key == "include") {
            if (!addPath(setting, root, loaded.includeDirs)) {
                error = "Line " + std::to_string(lineNumber) + ": path is not valid UTF-8.";
                return false;
            }
        } else if (key == "rundir") {
            std::wstring wide;
            if (!utf8ToWide(setting, wide)) {
                error = "Line " + std::to_string(lineNumber) + ": path is not valid UTF-8.";
                return false;
            }
            loaded.runDirectory = std::filesystem::path(wide);
            if (loaded.runDirectory.is_relative())
                loaded.runDirectory = root / loaded.runDirectory;
            loaded.runDirectory = loaded.runDirectory.lexically_normal();
        } else if (key == "define" || key == "library" || key == "arg") {
            std::wstring wide;
            if (!utf8ToWide(setting, wide)) {
                error = "Line " + std::to_string(lineNumber) + ": value is not valid UTF-8.";
                return false;
            }
            if (key == "define")
                loaded.defines.push_back(std::move(wide));
            else if (key == "library")
                loaded.libraries.push_back(std::move(wide));
            else
                loaded.arguments.push_back(std::move(wide));
        } else {
            error = "Line " + std::to_string(lineNumber) + ": unknown key.";
            return false;
        }
    }
    if (input.bad()) {
        error = "Error while reading project file.";
        return false;
    }
    if (loaded.sources.empty()) {
        error = "Project has no source= entries.";
        return false;
    }

    project = std::move(loaded);
    error.clear();
    return true;
}

bool saveProject(const Project &project, std::string &error) {
    const auto file = std::filesystem::absolute(project.file).lexically_normal();
    auto temporary = file;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) { error = "Cannot write project file."; return false; }
    const auto root = file.parent_path();
    std::error_code ec;
    for (const auto &source : project.sources) {
        auto relative = std::filesystem::relative(source, root, ec);
        output << "source=" << utf8Path(ec ? source : relative) << "\n";
        ec.clear();
    }
    for (const auto &path : project.includeDirs) {
        auto relative = std::filesystem::relative(path, root, ec);
        output << "include=" << utf8Path(ec ? path : relative) << "\n";
        ec.clear();
    }
    for (const auto &define : project.defines) output << "define=" << wideToUtf8(define) << "\n";
    for (const auto &library : project.libraries) output << "library=" << wideToUtf8(library) << "\n";
    for (const auto &argument : project.arguments) output << "arg=" << wideToUtf8(argument) << "\n";
    if (!project.runDirectory.empty()) {
        auto relative = std::filesystem::relative(project.runDirectory, root, ec);
        output << "rundir=" << utf8Path(ec ? project.runDirectory : relative) << "\n";
        ec.clear();
    }
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str());
        error = "Cannot replace project file.";
        return false;
    }
    error.clear();
    return true;
}
