#include <cassert>
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
}

int main() {
    TestGroup group;
    SyntaxEditor editor(TRect(0, 0, 80, 25), nullptr, nullptr, nullptr,
                        "tests/editor_selection_test.cpp");
    group.insert(&editor);
    editor.setCurPtr(0, 0);

    assert(editor.runFeature(cmMenuBlockStart));
    auto event = keyEvent(kbRight);
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
    group.remove(&editor);
}
