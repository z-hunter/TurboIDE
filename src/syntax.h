#pragma once

#define Uses_TEditWindow
#define Uses_TFileEditor
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TWindow
#include <tvision/tv.h>

#include <vector>

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
    TColorAttr executionLine = 0xE0;
    TColorAttr breakpointLine = 0x4F;
    TColorAttr buildStatus = 0x1B;
};

IDETheme &ideTheme();
void setEditorDiagnostic(TFileEditor *editor, int line);
void setEditorDebugState(TFileEditor *editor, const std::vector<int> &breakpoints,
                         int executionLine);

class SyntaxEditWindow : public TEditWindow {
public:
    SyntaxEditWindow(const TRect &bounds, TStringView fileName, int number);
};
