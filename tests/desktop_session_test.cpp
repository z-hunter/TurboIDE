#include "../src/desktop_session.h"

#include <filesystem>

int main() {
    const auto project = std::filesystem::temp_directory_path() / "turboide_desktop_session_test.prj";
    std::error_code error;
    std::filesystem::remove(desktopSessionFile(project), error);

    DesktopSession saved;
    saved.hasHelpBounds = true;
    saved.helpBounds = {3, 2, 68, 22};
    if (!saveDesktopSession(project, saved)) return 1;

    DesktopSession loaded;
    if (!loadDesktopSession(project, loaded)) return 2;
    std::filesystem::remove(desktopSessionFile(project), error);
    return loaded.hasHelpBounds && loaded.helpBounds.left == 3 &&
                   loaded.helpBounds.top == 2 && loaded.helpBounds.right == 68 &&
                   loaded.helpBounds.bottom == 22 ?
               0 :
               3;
}
