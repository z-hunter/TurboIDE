#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "desktop_session.h"

#include <fstream>
#include <sstream>

namespace {
std::string toUtf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

bool readRect(std::string_view text, DesktopRect &rect) {
    char comma;
    std::istringstream input{std::string(text)};
    return bool(input >> rect.left >> comma) && comma == ',' &&
           bool(input >> rect.top >> comma) && comma == ',' &&
           bool(input >> rect.right >> comma) && comma == ',' &&
           bool(input >> rect.bottom);
}

std::string writeRect(const DesktopRect &rect) {
    return std::to_string(rect.left) + ',' + std::to_string(rect.top) + ',' +
           std::to_string(rect.right) + ',' + std::to_string(rect.bottom);
}

bool relativeTo(const std::filesystem::path &file, const std::filesystem::path &root,
                std::filesystem::path &relative) {
    std::error_code error;
    relative = std::filesystem::relative(file, root, error);
    if (error || relative.empty()) return false;
    for (const auto &part : relative)
        if (part == L"..") return false;
    return true;
}

std::filesystem::path storedPath(const std::filesystem::path &file,
                                const std::filesystem::path &root) {
    std::filesystem::path relative;
    return relativeTo(file, root, relative) ? relative :
        std::filesystem::absolute(file).lexically_normal();
}
}

std::filesystem::path desktopSessionFile(const std::filesystem::path &projectFile) {
    auto file = projectFile;
    file.replace_extension(L".dsk");
    return file;
}

bool loadDesktopSession(const std::filesystem::path &projectFile, DesktopSession &session) {
    std::ifstream input(desktopSessionFile(projectFile), std::ios::binary);
    if (!input) return false;
    DesktopSession loaded;
    const auto root = projectFile.parent_path();
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("project=", 0) == 0) {
            loaded.hasProjectBounds = readRect(std::string_view(line).substr(8), loaded.projectBounds);
        } else if (line.rfind("help=", 0) == 0) {
            loaded.hasHelpBounds = readRect(std::string_view(line).substr(5), loaded.helpBounds);
        } else if (line.rfind("active=", 0) == 0) {
            loaded.activeFile = root / std::filesystem::u8path(line.substr(7));
        } else if (line.rfind("editor=", 0) == 0) {
            const auto first = line.find('\t', 7);
            const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
            if (first == std::string::npos || second == std::string::npos) continue;
            EditorSession editor;
            editor.file = root / std::filesystem::u8path(line.substr(7, first - 7));
            if (!readRect(std::string_view(line).substr(first + 1, second - first - 1), editor.bounds))
                continue;
            char comma;
            std::istringstream position(line.substr(second + 1));
            if (!(position >> editor.line >> comma) || comma != ',' || !(position >> editor.column))
                continue;
            if (editor.line < 1 || editor.column < 1) continue;
            loaded.editors.push_back(std::move(editor));
        } else if (line.rfind("breakpoint=", 0) == 0) {
            const auto separator = line.rfind('\t');
            if (separator == std::string::npos) continue;
            try {
                const int breakpointLine = std::stoi(line.substr(separator + 1));
                if (breakpointLine > 0)
                    loaded.breakpoints.emplace_back(root / std::filesystem::u8path(line.substr(11, separator - 11)), breakpointLine);
            } catch (...) {}
        } else if (line.rfind("watch=", 0) == 0) {
            if (line.size() > 6) loaded.watches.push_back(line.substr(6));
        }
    }
    if (input.bad()) return false;
    session = std::move(loaded);
    return true;
}

bool saveDesktopSession(const std::filesystem::path &projectFile, const DesktopSession &session) {
    const auto file = desktopSessionFile(projectFile);
    const auto root = projectFile.parent_path();
    auto temporary = file;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "# TurboIDE desktop session v1\n";
    if (session.hasProjectBounds) output << "project=" << writeRect(session.projectBounds) << "\n";
    if (session.hasHelpBounds) output << "help=" << writeRect(session.helpBounds) << "\n";
    if (!session.activeFile.empty())
        output << "active=" << toUtf8(storedPath(session.activeFile, root)) << "\n";
    for (const auto &editor : session.editors) {
        output << "editor=" << toUtf8(storedPath(editor.file, root)) << '\t' << writeRect(editor.bounds) << '\t'
               << editor.line << ',' << editor.column << "\n";
    }
    for (const auto &breakpoint : session.breakpoints) {
        if (breakpoint.second > 0)
            output << "breakpoint=" << toUtf8(std::filesystem::absolute(breakpoint.first).lexically_normal())
                   << '\t' << breakpoint.second << "\n";
    }
    for (const auto &watch : session.watches)
        if (!watch.empty() && watch.find_first_of("\r\n") == std::string::npos)
            output << "watch=" << watch << "\n";
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
