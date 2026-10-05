#include <cassert>
#include <cstring>
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

TEvent keyEvent(ushort keyCode, ushort modifiers = 0) {
    TEvent event{};
    event.what = evKeyDown;
    event.keyDown.keyCode = keyCode;
    event.keyDown.controlKeyState = modifiers;
    return event;
}

TEvent textEvent(char ch, ushort modifiers = 0) {
    TEvent event = keyEvent(static_cast<ushort>(static_cast<unsigned char>(ch)), modifiers);
    event.keyDown.charScan.charCode = ch;
    return event;
}
}

int main() {
    TestGroup group;
    SyntaxEditor editor(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr,
                        "tests/editor_selection_test.cpp");
    group.insert(&editor);
    constexpr char text[] = "one\nabc\nxyz\n";
    std::memcpy(editor.buffer + editor.bufSize - sizeof(text) + 1, text, sizeof(text) - 1);
    editor.setBufLen(sizeof(text) - 1);
    editor.setCurPtr(0, 0);

    auto event = keyEvent(kbCtrlB, kbCtrlShift | kbShift | kbAltShift);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbCtrlK, kbCtrlShift | kbShift | kbAltShift);
    editor.handleEvent(event);
    assert(editor.selStart == 0);
    assert(editor.selEnd == 2);

    editor.setCurPtr(3, 0);
    event = keyEvent(kbCtrlA);
    editor.handleEvent(event);
    assert(editor.curPtr == 0);
    event = keyEvent(kbCtrlA, kbCtrlShift | kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 0);
    assert(editor.selEnd == editor.bufLen);

    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight, kbShift);
    editor.handleEvent(event);

    assert(editor.selStart == 2);
    assert(editor.selEnd == 3);
    assert(editor.runFeature(cmMenuBlockEnd));
    assert(editor.selStart == 2);
    assert(editor.selEnd == 3);

    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = {};
    event.what = evMouseDown;
    event.mouse.where = TPoint {4, 0};
    event.mouse.buttons = mbLeftButton;
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    assert(editor.selStart == editor.selEnd);

    editor.setCurPtr(4, 0);
    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    assert(editor.selStart == 4);
    assert(editor.selEnd == 6);

    event = keyEvent(kbRight, kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 4);
    assert(editor.selEnd == 7);

    event = {};
    event.what = evCommand;
    event.message.command = cmTextStart;
    editor.handleEvent(event);
    assert(editor.curPtr == 0);
    assert(editor.selStart == 4);
    assert(editor.selEnd == 7);

    event = keyEvent(kbRight, kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 0);
    assert(editor.selEnd == 1);

    editor.setCurPtr(4, 0);
    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    event = keyEvent(kbUp);
    editor.handleEvent(event);
    assert(editor.curPtr == 2);
    assert(editor.selStart == 4);
    assert(editor.selEnd == 6);

    event = keyEvent(kbRight, kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 2);
    assert(editor.selEnd == 3);

    editor.setCurPtr(4, 0);
    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    event = {};
    event.what = evMouseDown;
    event.mouse.where = TPoint {1, 0};
    event.mouse.buttons = mbLeftButton;
    editor.handleEvent(event);
    assert(editor.curPtr == 1);
    assert(editor.selStart == editor.selEnd);

    event = keyEvent(kbRight, kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 1);
    assert(editor.selEnd == 2);

    editor.setCurPtr(4, 0);
    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    event = {};
    event.what = evCommand;
    event.message.command = cmTextStart;
    editor.handleEvent(event);
    event = textEvent('{', kbShift);
    editor.handleEvent(event);
    assert(text(editor) == "{one\nabc\nxyz\n");
    assert(editor.selStart == 5);
    assert(editor.selEnd == 8);

    event = keyEvent(kbCtrlK);
    editor.handleEvent(event);
    event = keyEvent('B', kbShift);
    editor.handleEvent(event);
    event = keyEvent(kbDown);
    editor.handleEvent(event);
    event = keyEvent(kbCtrlK);
    editor.handleEvent(event);
    event = keyEvent('K', kbShift);
    editor.handleEvent(event);
    event = {};
    event.what = evMouseDown;
    event.mouse.where = TPoint {1, 0};
    event.mouse.buttons = mbLeftButton;
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuRectClear));
    assert(text(editor) == "{one\nabc\nxyz\n");

    editor.setCurPtr(5, 0);
    assert(editor.runFeature(cmMenuBlockStart));
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    event = keyEvent(kbRight);
    editor.handleEvent(event);
    assert(editor.runFeature(cmMenuBlockEnd));
    event = {};
    event.what = evCommand;
    event.message.command = cmTextStart;
    editor.handleEvent(event);
    event = keyEvent(kbCtrlF, kbCtrlShift | kbShift);
    editor.handleEvent(event);
    assert(editor.selStart == 0);
    assert(editor.selEnd == 4);
    group.remove(&editor);
}
