#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "desktop_session.h"

class TApplication;
class THelpFile;

// Searches linked alphabetical index topics in a native help database.
std::optional<int> findHelpTopic(THelpFile& file, std::string_view word,
                                 int indexContext);

// Opens a native Turbo Vision help file modally. Returns nullopt after the
// window closes, or an error message if the file cannot be opened.
std::optional<std::string> openHelpDatabase(
    TApplication& app, const std::filesystem::path& path, int topic = 1,
    const DesktopRect* initialBounds = nullptr, DesktopRect* finalBounds = nullptr);

std::optional<std::string> openHelpDatabaseForWord(
    TApplication& app, const std::filesystem::path& path, std::string_view word,
    int indexContext, const DesktopRect* initialBounds = nullptr,
    DesktopRect* finalBounds = nullptr);
