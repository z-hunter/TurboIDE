#define Uses_TApplication
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TPalette
#define Uses_TScrollBar
#define Uses_TWindow
#define Uses_fpstream
#define Uses_MsgBox
#include <tvision/tv.h>
#include <tvision/help.h>

#include "legacy_help.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <vector>

namespace {
class BackHelpViewer final : public THelpViewer {
public:
    BackHelpViewer(const TRect& bounds, TScrollBar* hScrollBar,
                   TScrollBar* vScrollBar, THelpFile* file, int context)
        : THelpViewer(bounds, hScrollBar, vScrollBar, file,
                      static_cast<ushort>(context)), currentTopic(context) {}

    void handleEvent(TEvent& event) override {
        if (event.what == evKeyDown &&
            (event.keyDown.keyCode == kbAltLeft || event.keyDown.keyCode == kbBack)) {
            goBack(event);
            return;
        }

        int target = -1;
        if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter &&
            selected > 0 && selected <= topic->getNumCrossRefs()) {
            TPoint location;
            uchar length;
            topic->getCrossRef(selected - 1, location, length, target);
        } else if (event.what == evMouseDown) {
            TPoint mouse = makeLocal(event.mouse.where);
            mouse.x += delta.x;
            mouse.y += delta.y;
            for (int i = 0; i < topic->getNumCrossRefs(); ++i) {
                TPoint location;
                uchar length;
                int ref;
                topic->getCrossRef(i, location, length, ref);
                if (location.y == mouse.y + 1 && mouse.x >= location.x &&
                    mouse.x < location.x + length) {
                    target = ref;
                    break;
                }
            }
        }

        const bool navigating = target >= 0;
        const int previous = currentTopic;
        THelpViewer::handleEvent(event);
        if (navigating) {
            history.push_back(previous);
            currentTopic = target;
        }
    }

private:
    void goBack(TEvent& event) {
        if (!history.empty()) {
            currentTopic = history.back();
            history.pop_back();
            switchToTopic(currentTopic);
        }
        clearEvent(event);
    }

    int currentTopic;
    std::vector<int> history;
};

class BackHelpWindow final : public TWindow {
public:
    BackHelpWindow(THelpFile* file, int context, const DesktopRect* initialBounds)
        : TWindowInit(&TWindow::initFrame),
          TWindow(initialBounds ? TRect(initialBounds->left, initialBounds->top,
                                        initialBounds->right, initialBounds->bottom)
                                : TRect(0, 0, 50, 18),
                  "Help", wnNoNumber) {
        if (!initialBounds) options |= ofCentered;
        TRect viewerBounds(0, 0, size.x, size.y);
        viewerBounds.grow(-2, -1);
        insert(new BackHelpViewer(
            viewerBounds,
            standardScrollBar(sbHorizontal | sbHandleKeyboard),
            standardScrollBar(sbVertical | sbHandleKeyboard), file, context));
    }

    TPalette& getPalette() const override {
        static TPalette palette(cHelpWindow, sizeof(cHelpWindow) - 1);
        return palette;
    }
};

std::optional<std::string> validateHelpFile(const std::filesystem::path& path,
                                            int topic) {
    if (topic < 0 || topic > 0xffff)
        return "The requested help topic ID is invalid.";

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return "The help file could not be opened.";

    std::array<std::uint8_t, 12> header{};
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    if (input.gcount() != static_cast<std::streamsize>(header.size()))
        return "The help file is incomplete or invalid.";

    const std::uint32_t magic = std::uint32_t(header[0]) |
        (std::uint32_t(header[1]) << 8) | (std::uint32_t(header[2]) << 16) |
        (std::uint32_t(header[3]) << 24);
    const std::uint32_t declaredSize = std::uint32_t(header[4]) |
        (std::uint32_t(header[5]) << 8) | (std::uint32_t(header[6]) << 16) |
        (std::uint32_t(header[7]) << 24);
    const std::uint32_t indexOffset = std::uint32_t(header[8]) |
        (std::uint32_t(header[9]) << 8) | (std::uint32_t(header[10]) << 16) |
        (std::uint32_t(header[11]) << 24);
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (magic != static_cast<std::uint32_t>(magicHeader) || ec ||
        fileSize < header.size() || declaredSize != fileSize - 8 ||
        indexOffset < header.size() || indexOffset >= fileSize)
        return "The file is not a valid Turbo Vision help database.";
    return std::nullopt;
}
} // namespace

std::optional<int> findHelpTopic(THelpFile& file, std::string_view word,
                                 int indexContext) {
    if (word.empty() || !file.index || file.index->position(indexContext) <= 0)
        return std::nullopt;

    auto equalIgnoringAsciiCase = [](std::string_view label, std::string_view value) {
        if (label.size() != value.size()) return false;
        for (std::size_t i = 0; i < label.size(); ++i) {
            auto lower = [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 'a' - 'A') : c;
            };
            if (lower(static_cast<unsigned char>(label[i])) !=
                lower(static_cast<unsigned char>(value[i]))) return false;
        }
        return true;
    };

    std::optional<int> caseInsensitiveMatch;
    std::unique_ptr<THelpTopic> index(file.getTopic(indexContext));
    for (int bucket = 0; bucket < index->getNumCrossRefs(); ++bucket) {
        TPoint location;
        uchar length;
        int bucketContext;
        index->getCrossRef(bucket, location, length, bucketContext);
        if (file.index->position(bucketContext) <= 0) continue;
        std::unique_ptr<THelpTopic> page(file.getTopic(bucketContext));
        page->setWidth(255);
        for (int entry = 0; entry < page->getNumCrossRefs(); ++entry) {
            int topicContext;
            page->getCrossRef(entry, location, length, topicContext);
            const TStringView line = page->getLine(location.y);
            const std::string_view text(line.data(), line.size());
            if (location.x < 0 || static_cast<std::size_t>(location.x) + length > text.size() ||
                file.index->position(topicContext) <= 0) continue;
            const auto label = text.substr(location.x, length);
            if (label == word) return topicContext;
            if (!caseInsensitiveMatch && equalIgnoringAsciiCase(label, word))
                caseInsensitiveMatch = topicContext;
        }
    }
    return caseInsensitiveMatch;
}

namespace {
std::optional<std::string> openHelpDatabaseImpl(TApplication& app,
                                                const std::filesystem::path& path,
                                                int topic, std::string_view word,
                                                const DesktopRect* initialBounds,
                                                DesktopRect* finalBounds) {
    if (auto error = validateHelpFile(path, topic))
        return error;

    const std::string fileName = path.string();
    auto stream = std::make_unique<fpstream>(fileName.c_str(), ios::in);
    if (stream->fail())
        return "The help file could not be opened.";

    auto* helpFile = new THelpFile(*stream.release());
    if (helpFile->stream->fail() || !helpFile->index) {
        delete helpFile;
        return "The file is not a valid Turbo Vision help database.";
    }
    if (!word.empty()) {
        if (helpFile->index->position(topic) <= 0) {
            delete helpFile;
            return "The help database has no alphabetical index.";
        }
        const auto found = findHelpTopic(*helpFile, word, topic);
        if (!found) {
            delete helpFile;
            return "No Help topic found for '" + std::string(word) + "'.";
        }
        topic = *found;
    }
    if (helpFile->index->position(topic) <= 0) {
        delete helpFile;
        return "The help database does not contain the requested topic.";
    }

    TView* rawWindow = new BackHelpWindow(helpFile, topic, initialBounds);
    if (!rawWindow) {
        delete helpFile;
        return "The help window could not be created.";
    }
    TView* window = app.validView(rawWindow);
    if (!window) {
        // validView destroys the rejected window, whose viewer owns helpFile.
        return "The help window could not be created.";
    }
    app.execView(window);
    if (finalBounds) {
        const TRect bounds = window->getBounds();
        *finalBounds = {bounds.a.x, bounds.a.y, bounds.b.x, bounds.b.y};
    }
    TObject::destroy(window);
    return std::nullopt;
}
} // namespace

std::optional<std::string> openHelpDatabase(TApplication& app,
                                          const std::filesystem::path& path,
                                          int topic,
                                          const DesktopRect* initialBounds,
                                          DesktopRect* finalBounds) {
    return openHelpDatabaseImpl(app, path, topic, {}, initialBounds, finalBounds);
}

std::optional<std::string> openHelpDatabaseForWord(TApplication& app,
                                                  const std::filesystem::path& path,
                                                  std::string_view word,
                                                  int indexContext,
                                                  const DesktopRect* initialBounds,
                                                  DesktopRect* finalBounds) {
    return openHelpDatabaseImpl(app, path, indexContext, word, initialBounds, finalBounds);
}
