#define Uses_THelpFile
#define Uses_THelpIndex
#define Uses_THelpTopic
#define Uses_TPoint
#define Uses_TStreamable
#define Uses_fpstream
#include <tvision/tv.h>
#include <tvision/helpbase.h>

#include <array>
#include <iostream>
#include <string_view>

namespace {

bool checkTopic(THelpFile &file, int context, std::string_view expected) {
    if (file.index->position(context) <= 0) {
        std::cerr << "missing help context " << context << '\n';
        return false;
    }
    THelpTopic *topic = file.getTopic(context);
    topic->setWidth(120);
    bool found = false;
    for (int line = 1; line <= topic->numLines(); ++line) {
        const TStringView text = topic->getLine(line);
        if (std::string_view(text.data(), text.size()).find(expected) != std::string_view::npos) {
            found = true;
            break;
        }
    }
    for (int reference = 0; reference < topic->getNumCrossRefs(); ++reference) {
        TPoint location{};
        uchar length = 0;
        int target = 0;
        topic->getCrossRef(reference, location, length, target);
        if (file.index->position(target) <= 0) {
            std::cerr << "unresolved link in context " << context << " to " << target << '\n';
            found = false;
        }
    }
    delete topic;
    if (!found)
        std::cerr << "context " << context << " does not contain '" << expected << "'\n";
    return found;
}

} // namespace

int main() {
    auto *stream = new fpstream(TURBOIDE_HELP_DATABASE, std::ios::in | std::ios::binary);
    THelpFile file(*stream);
    if (file.stream->fail() || !file.index) {
        std::cerr << "could not open TurboIDE help database\n";
        return 1;
    }
    int failures = 0;
    for (const auto &[context, heading] : std::array{
             std::pair{10030, std::string_view{"HELP CONTENTS"}},
             std::pair{1917, std::string_view{"printf"}},
             std::pair{12000, std::string_view{"TurboIDE Help"}},
             std::pair{12001, std::string_view{"Jump to Symbol"}},
             std::pair{12002, std::string_view{"Back from Symbol"}},
             std::pair{12003, std::string_view{"Word Completion"}},
             std::pair{12004, std::string_view{"Class Browser"}},
         })
        failures += !checkTopic(file, context, heading);
    return failures ? 1 : 0;
}
