#include <cassert>
#include <cstring>
#include <filesystem>
#include "../src/syntax.cpp"
#include "../src/editor_features.cpp"

void setEditorPrefixHint(TFileEditor *, int) {}
void advanceEditorPrefixHintPage(TFileEditor *) {}

namespace {
class TestGroup final : public TGroup {
public:
    TestGroup() : TGroup(TRect(0, 0, 80, 25)) {}
    void getEvent(TEvent &event) override { event.what = evMouseUp; }
    void putEvent(TEvent &) override {}
};

class DrawBufferProbe final : public TDrawBuffer {
public:
    TColorAttr attribute(ushort position) const { return data[position].attribute; }
};

TEvent command(ushort value) {
    TEvent event{};
    event.what = evCommand;
    event.message.command = value;
    return event;
}

TEvent key(char value) {
    TEvent event{};
    event.what = evKeyDown;
    event.keyDown.keyCode = static_cast<ushort>(value);
    event.keyDown.text[0] = value;
    event.keyDown.textLength = 1;
    return event;
}

std::string text(TFileEditor &editor) {
    std::string result;
    for (uint i = 0; i < editor.bufLen; ++i)
        result.push_back(editor.bufChar(i));
    return result;
}
}

int main() {
    TestGroup group;
    SyntaxEditor editor(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr, "");
    group.insert(&editor);

    for (char c : std::string("abc")) {
        auto event = key(c);
        editor.handleEvent(event);
    }
    assert(text(editor) == "abc");
    TEvent event{};
    event.what = evKeyDown;
    event.keyDown.keyCode = kbCtrlU;
    editor.handleEvent(event);
    assert(text(editor).empty());
    event = {};
    event.what = evKeyDown;
    event.keyDown.keyCode = kbBack;
    event.keyDown.controlKeyState = kbAltShift | kbShift;
    editor.handleEvent(event);
    assert(text(editor) == "abc");

    event = command(cmBackSpace);
    editor.handleEvent(event);
    event = command(cmBackSpace);
    editor.handleEvent(event);
    assert(text(editor) == "a");
    event = command(cmUndo);
    editor.handleEvent(event);
    assert(text(editor) == "abc");
    event = command(cmRedo);
    editor.handleEvent(event);
    assert(text(editor) == "a");
    event = command(cmUndo);
    editor.handleEvent(event);
    assert(text(editor) == "abc");

    SyntaxEditor diagnostic(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr, "warning.c");
    group.insert(&diagnostic);
    constexpr char warningText[] = "abc\nxyz\n";
    std::memcpy(diagnostic.buffer + diagnostic.bufSize - sizeof(warningText) + 1,
                warningText, sizeof(warningText) - 1);
    diagnostic.setBufLen(sizeof(warningText) - 1);
    setEditorDiagnostic(&diagnostic, 1, true);
    DrawBufferProbe warningBuffer;
    diagnostic.formatLine(warningBuffer, 0, 0, 3, ideTheme().normalText);
    assert(warningBuffer.attribute(0) == ideTheme().warningLine);
    diagnostic.setCurPtr(1, 0);
    event = key('!');
    diagnostic.handleEvent(event);
    DrawBufferProbe normalBuffer;
    diagnostic.formatLine(normalBuffer, 0, 0, 3, ideTheme().normalText);
    assert(normalBuffer.attribute(0) == ideTheme().normalText);
    group.remove(&diagnostic);

    SyntaxEditor autoDiagnostic(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr, "warnings.c");
    group.insert(&autoDiagnostic);
    std::memcpy(autoDiagnostic.buffer + autoDiagnostic.bufSize - sizeof(warningText) + 1,
                warningText, sizeof(warningText) - 1);
    autoDiagnostic.setBufLen(sizeof(warningText) - 1);
    setEditorWarnings(&autoDiagnostic, {1, 2});
    DrawBufferProbe firstWarningBuffer;
    autoDiagnostic.formatLine(firstWarningBuffer, 0, 0, 3, ideTheme().normalText);
    assert(firstWarningBuffer.attribute(0) == ideTheme().warningLine);
    autoDiagnostic.setCurPtr(1, 0);
    event = key('!');
    autoDiagnostic.handleEvent(event);
    DrawBufferProbe clearedWarningBuffer;
    autoDiagnostic.formatLine(clearedWarningBuffer, 0, 0, 3, ideTheme().normalText);
    assert(clearedWarningBuffer.attribute(0) == ideTheme().normalText);
    DrawBufferProbe secondWarningBuffer;
    autoDiagnostic.formatLine(secondWarningBuffer, autoDiagnostic.nextLine(0), 0, 3,
                              ideTheme().normalText);
    assert(secondWarningBuffer.attribute(0) == ideTheme().warningLine);
    group.remove(&autoDiagnostic);

    pseudoMacros().push_back({"zz", "undo test", "SNIP", 0});
    editor.insertText("zz", 2, False);
    assert(editor.runFeature(cmExpandPmacro));
    assert(text(editor) == "abcSNIP");
    event = command(cmUndo);
    editor.handleEvent(event);
    assert(text(editor) == "abczz");
    event = command(cmRedo);
    editor.handleEvent(event);
    assert(text(editor) == "abcSNIP");

    event = command(cmUndo);
    editor.handleEvent(event);
    editor.insertText("!", 1, False);
    event = command(cmRedo);
    editor.handleEvent(event);
    assert(text(editor) == "abczz!");

    const std::string beforeMany = text(editor);
    for (int i = 0; i < 128; ++i)
        editor.insertText("x", 1, False);
    for (int i = 0; i < 128; ++i) {
        event = command(cmUndo);
        editor.handleEvent(event);
    }
    assert(text(editor) == beforeMany);

    constexpr char mixed[] = "one\r\ntwo\nthree\r";
    editor.setBufLen(0);
    std::memcpy(editor.buffer + editor.bufSize - sizeof(mixed) + 1, mixed, sizeof(mixed) - 1);
    editor.setBufLen(sizeof(mixed) - 1);
    editor.setCurPtr(editor.bufLen, 0);
    editor.insertText("x", 1, False);
    event = command(cmUndo);
    editor.handleEvent(event);
    assert(text(editor) == std::string(mixed, sizeof(mixed) - 1));

    const auto path = std::filesystem::temp_directory_path() / "turboide-editor-undo-test.c";
    std::filesystem::remove(path);
    SyntaxEditor saved(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr, path.u8string());
    group.insert(&saved);
    saved.insertText("a", 1, False);
    assert(saved.saveFile());
    assert(!saved.modified);
    saved.insertText("b", 1, False);
    assert(saved.modified);
    event = command(cmUndo);
    saved.handleEvent(event);
    assert(text(saved) == "a");
    assert(!saved.modified);
    event = command(cmRedo);
    saved.handleEvent(event);
    assert(saved.modified);
    group.remove(&saved);
    std::filesystem::remove(path);
    group.remove(&editor);
}
