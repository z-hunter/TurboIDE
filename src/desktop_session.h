#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct DesktopRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct EditorSession {
    std::filesystem::path file;
    DesktopRect bounds;
    int line = 1;
    int column = 1;
};

struct DesktopSession {
    bool hasProjectBounds = false;
    DesktopRect projectBounds;
    std::vector<EditorSession> editors;
    std::filesystem::path activeFile;
};

std::filesystem::path desktopSessionFile(const std::filesystem::path &projectFile);
bool loadDesktopSession(const std::filesystem::path &projectFile, DesktopSession &session);
bool saveDesktopSession(const std::filesystem::path &projectFile, const DesktopSession &session);
