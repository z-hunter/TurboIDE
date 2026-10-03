#define Uses_THelpFile
#define Uses_THelpIndex
#define Uses_THelpTopic
#define Uses_TPoint
#define Uses_TStreamable
#define Uses_ipstream
#define Uses_iopstream
#define Uses_fpstream
#include <tvision/tv.h>
#include <tvision/helpbase.h>

#include "../src/legacy_help.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int32_t helpMagic = 0x46484246;
constexpr int contentsContext = 399;
constexpr int nativeContentsContext = 10030;
constexpr int indexContext = 10031;
constexpr int codeContext = 1191;

bool containsLine(THelpTopic *topic, std::string_view expected, bool rejectControls) {
    topic->setWidth(120);
    for (int i = 1; i <= topic->numLines(); ++i) {
        const TStringView line = topic->getLine(i);
        const std::string_view text(line.data(), line.size());
        if (text.find(expected) == std::string_view::npos)
            continue;
        if (!rejectControls)
            return true;
        if (text.find("\xE2\x99\xA3") != std::string_view::npos)
            return false;
        for (unsigned char ch : text)
            if ((ch < 0x20 && ch != '\t') || ch == 0x05)
                return false;
        return true;
    }
    return false;
}

bool checkTopic(THelpFile &file, int context, std::string_view expected,
                const char *description) {
    if (file.index->position(context) <= 0) {
        std::cerr << "missing context " << context << " (" << description << ")\n";
        return false;
    }
    THelpTopic *topic = file.getTopic(context);
    const bool found = containsLine(topic, expected, false);
    delete topic;
    if (!found)
        std::cerr << "context " << context << " does not render '" << expected << "'\n";
    return found;
}

int checkReferences(THelpFile &file, int context) {
    THelpTopic *topic = file.getTopic(context);
    int unresolved = 0;
    for (int i = 0; i < topic->getNumCrossRefs(); ++i) {
        TPoint location{};
        uchar length = 0;
        int target = 0;
        topic->getCrossRef(i, location, length, target);
        if (file.index->position(target) <= 0) {
            ++unresolved;
            std::cerr << "unresolved cross-reference in context " << context
                      << " to " << target << '\n';
        }
    }
    delete topic;
    return unresolved;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: help_database_check <native-help.h32>\n";
        return 2;
    }

    std::ifstream raw(argv[1], std::ios::binary);
    std::array<unsigned char, 4> header{};
    raw.read(reinterpret_cast<char *>(header.data()), header.size());
    const int32_t magic = static_cast<int32_t>(header[0]) |
                          (static_cast<int32_t>(header[1]) << 8) |
                          (static_cast<int32_t>(header[2]) << 16) |
                          (static_cast<int32_t>(header[3]) << 24);
    if (!raw || magic != helpMagic) {
        std::cerr << "invalid Turbo Vision help magic in " << argv[1] << '\n';
        return 1;
    }

    auto *stream = new fpstream(argv[1], std::ios::in | std::ios::binary);
    THelpFile file(*stream);
    if (!file.index || file.indexPos < 12) {
        std::cerr << "invalid Turbo Vision help index\n";
        return 1;
    }

    int failures = 0;
    failures += !checkTopic(file, contentsContext, "HELP CONTENTS", "contents");
    failures += !checkTopic(file, nativeContentsContext, "HELP CONTENTS", "native contents");
    failures += !checkTopic(file, indexContext, "Turbo C Help Index", "index");
    failures += !checkTopic(file, codeContext, "#include <stdio.h>", "code example");

    int unresolved = checkReferences(file, contentsContext) +
                     checkReferences(file, nativeContentsContext);

    for (const auto &entry : {std::pair{"printf", 1917}, {"malloc", 1867},
                              {"sizeof", 1375}, {"struct", 1377}}) {
        const auto found = findHelpTopic(file, entry.first, indexContext);
        if (!found || *found != entry.second) {
            std::cerr << "wrong index target for " << entry.first << '\n';
            ++failures;
        }
    }
    if (findHelpTopic(file, "not_a_borland_help_topic", indexContext)) {
        std::cerr << "unknown identifier unexpectedly matched an index entry\n";
        ++failures;
    }
    if (findHelpTopic(file, "PRINTF", indexContext) != std::optional<int>{1917}) {
        std::cerr << "case-insensitive index lookup failed\n";
        ++failures;
    }

    THelpTopic *code = file.getTopic(codeContext);
    if (!containsLine(code, "#include <stdio.h>", true)) {
        std::cerr << "code topic " << codeContext
                  << " is missing the exact include line or contains control glyphs\n";
        ++failures;
    }
    delete code;

    std::cout << "help database: " << argv[1] << '\n'
              << "contexts checked: 4; unresolved links in contents: " << unresolved << '\n';
    return failures || unresolved ? 1 : 0;
}
