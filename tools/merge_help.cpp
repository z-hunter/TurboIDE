#define Uses_THelpFile
#define Uses_THelpIndex
#define Uses_THelpTopic
#define Uses_TStreamable
#define Uses_fpstream
#include <tvision/tv.h>
#include <tvision/helpbase.h>

#include <iostream>

namespace {

bool copyTopics(THelpFile &source, THelpFile &destination, const char *name) {
    if (!source.index) {
        std::cerr << name << " has no help index\n";
        return false;
    }
    for (int context = 0; context < source.index->size; ++context) {
        if (source.index->position(context) <= 0)
            continue;
        if (destination.index->position(context) > 0) {
            std::cerr << "duplicate help context " << context << " in " << name << '\n';
            return false;
        }
        THelpTopic *topic = source.getTopic(context);
        destination.recordPositionInIndex(context);
        destination.putTopic(topic);
        delete topic;
    }
    return true;
}

bool merge(const char *legacyPath, const char *turboIDEPath, const char *outputPath) {
    auto *legacyStream = new fpstream(legacyPath, ios::in | ios::binary);
    THelpFile legacy(*legacyStream);
    if (legacy.stream->fail()) {
        std::cerr << "could not open legacy help database\n";
        return false;
    }
    auto *turboIDEStream = new fpstream(turboIDEPath, ios::in | ios::binary);
    THelpFile turboIDE(*turboIDEStream);
    if (turboIDE.stream->fail()) {
        std::cerr << "could not open TurboIDE help database\n";
        return false;
    }
    auto *outputStream = new fpstream(outputPath, ios::in | ios::out | ios::trunc | ios::binary);
    THelpFile output(*outputStream);
    if (output.stream->fail()) {
        std::cerr << "could not create merged help database\n";
        return false;
    }
    return copyTopics(legacy, output, "legacy help database") &&
           copyTopics(turboIDE, output, "TurboIDE help database");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: merge_help <legacy.h32> <turboide.h32> <output.h32>\n";
        return 2;
    }
    return merge(argv[1], argv[2], argv[3]) ? 0 : 1;
}
