#pragma once

#define Uses_TEditWindow
#define Uses_TFileEditor
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TWindow
#include <tvision/tv.h>

#include <vector>
#include <string>

struct IDETheme {
    IDETheme();

    TPalette application;
    TPalette messagesWindow;
    TPalette messagesList;
    TColorAttr normalText = 0x17;
    TColorAttr keyword = 0x1F;
    TColorAttr comment = 0x13;
    TColorAttr stringLiteral = 0x1A;
    TColorAttr number = 0x1C;
    TColorAttr preprocessor = 0x1D;
    TColorAttr diagnosticLine = 0x4E;
    TColorAttr warningLine = 0x6E;
    TColorAttr executionLine = 0xE0;
    TColorAttr breakpointLine = 0x4F;
    TColorAttr matchingBracket = 0x4F;
    TColorAttr buildStatus = 0x1B;
};

IDETheme &ideTheme();
void setEditorDiagnostic(TFileEditor *editor, int line, bool warning = false);
void setEditorWarnings(TFileEditor *editor, const std::vector<int> &lines);
void setEditorDebugState(TFileEditor *editor, const std::vector<int> &breakpoints,
                         int executionLine);
void setEditorPrefixHint(TFileEditor *editor, int mode);
void advanceEditorPrefixHintPage(TFileEditor *editor);
bool editorSupportsPrefixKeys(TFileEditor *editor);
void setDefaultPersistentBlocks(bool enabled);
void setEditorPersistentBlocks(TFileEditor *editor, bool enabled);
bool editorPersistentBlocks(TFileEditor *editor);
void moveEditorCursor(TFileEditor *editor, uint position);

class SyntaxEditWindow : public TEditWindow {
public:
    SyntaxEditWindow(const TRect &bounds, TStringView fileName, int number);
    const char *getTitle(short maxSize) override;

private:
    std::string titleBuffer_;
};
