#define Uses_TApplication
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TColorDialog
#define Uses_TColorGroup
#define Uses_TColorItem
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEditWindow
#define Uses_TEditor
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_TFileEditor
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TListViewer
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TMenu
#define Uses_TPoint
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TDrawBuffer
#define Uses_TSubMenu
#define Uses_TChDirDialog
#define Uses_TKeys
#define Uses_THistory
#define Uses_TSItem
#define Uses_MsgBox
#define Uses_TReplaceDialogRec
#define Uses_TFindDialogRec
#define Uses_TScrollBar
#define Uses_TScreen
#define Uses_TMenuPopup
#include <tvision/tv.h>

#include "build.h"
#include "debugger.h"
#include "desktop_session.h"
#include "editor_features.h"
#include "legacy_help.h"
#include "project.h"
#include "run.h"
#include "settings.h"
#include "syntax.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <filesystem>
#include <cwchar>

namespace {
std::atomic_bool buildControlHandlerActive{false};
std::atomic_bool buildCancelRequested{false};

BOOL WINAPI handleBuildControlEvent(DWORD controlType) {
    if (controlType == CTRL_BREAK_EVENT && buildControlHandlerActive.load()) {
        buildCancelRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

constexpr ushort cmAbout = 100;
constexpr ushort cmNotReady = 101;
constexpr ushort cmNewProject = 102;
constexpr ushort cmOpenProject = 103;
constexpr ushort cmCompile = 104;
constexpr ushort cmBuild = 105;
constexpr ushort cmBuildResult = 106;
constexpr ushort cmRun = 107;
constexpr ushort cmUserScreen = 108;
constexpr ushort cmNextMessage = 109;
constexpr ushort cmPrevMessage = 110;
constexpr ushort cmShowMessages = 111;
constexpr ushort cmDismissBuildPopup = 112;
constexpr ushort cmCompilerMessages = 113;
constexpr ushort cmProjectWindow = 114;
constexpr ushort cmProjectAdd = 115;
constexpr ushort cmProjectDelete = 116;
constexpr ushort cmProjectClose = 117;
constexpr ushort cmRunParameters = 118;
constexpr ushort cmRunDirectory = 119;
constexpr ushort cmEnvironment = 120;
constexpr ushort cmWindowList = 121;
constexpr ushort cmChangeDirectory = 122;
constexpr ushort cmMessagesClear = 137;
constexpr ushort cmMessagesGotoSource = 138;
constexpr ushort cmMessagesTrackSource = 139;
constexpr ushort cmDebugStart = 123;
constexpr ushort cmDebugContinue = 124;
constexpr ushort cmDebugStepInto = 125;
constexpr ushort cmDebugStepOver = 126;
constexpr ushort cmDebugToggleBreakpoint = 127;
constexpr ushort cmDebugStop = 128;
constexpr ushort cmShowWatches = 129;
constexpr ushort cmCommandPrompt = 130;
constexpr ushort cmAddWatch = 131;
constexpr ushort cmEvaluateWatches = 132;
constexpr ushort cmShowLocals = 134;
constexpr ushort cmHelp = 135;
constexpr ushort cmColors = 136;
constexpr ushort cmGoToLine = 145;
constexpr ushort cmCompiler = 178;
constexpr ushort cmCompilerBrowse = 179;

// Borland TCHELP context IDs. Future TurboIDE documentation keeps these IDs.
constexpr ushort hcEditWindow = 402;
constexpr ushort hcWatchWindow = 403;
constexpr ushort hcMessagesWindow = 405;
constexpr ushort hcProjectWindow = 409;
constexpr ushort hcFileMenu = 411;
constexpr ushort hcEditMenu = 412;
constexpr ushort hcSearchMenu = 413;
constexpr ushort hcRunMenu = 414;
constexpr ushort hcCompileMenu = 415;
constexpr ushort hcDebugMenu = 416;
constexpr ushort hcProjectMenu = 417;
constexpr ushort hcOptionsMenu = 418;
constexpr ushort hcWindowMenu = 419;
constexpr ushort hcHelpMenu = 420;
constexpr ushort hcToolsMenu = 757;
constexpr ushort hcEnvironmentMenu = 736;
constexpr ushort hcEnvironmentPreferences = 885;
constexpr ushort hcEnvironmentEditor = 899;
constexpr ushort hcEnvironmentMouse = 903;
constexpr ushort hcEnvironmentStartup = 908;
constexpr ushort hcEnvironmentColors = 915;
constexpr ushort hcFindDialog = 562;
constexpr ushort hcReplaceDialog = 566;
constexpr ushort hcGoToLineDialog = 568;
constexpr ushort hcRunParametersDialog = 572;
constexpr ushort hcAddWatchDialog = 590;
constexpr ushort hcEditorDialog = 899;
constexpr ushort hcEditorTabSize = 899;
constexpr ushort hcEditorPersistentBlocks = 899;
constexpr ushort cmHelpContents = 176;
constexpr ushort cmHelpIndex = 177;

std::function<void(ushort)> contextHelpHandler;

TMenuItem *disabledMenuItem(TMenuItem *item) {
    item->disabled = True;
    return item;
}

class TurboIDEApp;
class BuildMessagesWindow;
class BuildProgressWindow;
class ProjectFilesWindow;
class DebugWatchesWindow;
class DebugWatchWindow;

class ContextHelpDialog : public TDialog {
public:
    ContextHelpDialog(const TRect &bounds, const char *title, ushort context)
        : TWindowInit(&TWindow::initFrame), TDialog(bounds, title) { helpCtx = context; }

    void handleEvent(TEvent &event) override {
        TDialog::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmHelp) {
            if (contextHelpHandler) contextHelpHandler(getHelpCtx());
            clearEvent(event);
        }
    }
};

class CompilerDialog final : public ContextHelpDialog {
public:
    CompilerDialog(const TRect &bounds)
        : TWindowInit(&TWindow::initFrame), ContextHelpDialog(bounds, "Compiler Options", hcOptionsMenu) {}

    void handleEvent(TEvent &event) override {
        ContextHelpDialog::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmCompilerBrowse) {
            endModal(cmCompilerBrowse);
            clearEvent(event);
        }
    }
};

class ToolchainDirectoryDialog final : public TChDirDialog {
public:
    explicit ToolchainDirectoryDialog(std::filesystem::path &compilerPath)
        : TWindowInit(&TWindow::initFrame), TChDirDialog(cdNormal, 209), compilerPath_(compilerPath) {
        growTo(68, 20);
        auto *cancel = new TButton(TRect(55, 15, 65, 17), "Cancel", cmCancel, bfNormal);
        cancel->growMode = gfGrowLoX | gfGrowHiX;
        insert(cancel);
        insert(new TStaticText(TRect(2, 18, 66, 19),
            "Select the folder, not a file: gcc.exe, g++.exe, gdb.exe."));
    }

    Boolean valid(ushort command) override {
        if (!TChDirDialog::valid(command) || command != cmOK)
            return command != cmOK;

        std::error_code error;
        const auto directory = std::filesystem::current_path(error);
        std::string missing;
        for (const wchar_t *tool : {L"gcc.exe", L"g++.exe", L"gdb.exe"})
            if (error || !std::filesystem::is_regular_file(directory / tool, error)) {
                if (!missing.empty()) missing += ", ";
                missing += std::filesystem::path(tool).string();
            }
        if (!missing.empty()) {
            messageBox(("The selected folder is missing: " + missing + ".").c_str(),
                       mfError | mfOKButton);
            return False;
        }
        compilerPath_ = directory / L"gcc.exe";
        return True;
    }

    void handleEvent(TEvent &event) override {
        if ((event.what == evKeyDown && event.keyDown.keyCode == kbEsc) ||
            (event.what == evCommand &&
             (event.message.command == cmCancel || event.message.command == cmClose))) {
            endModal(cmCancel);
            clearEvent(event);
            return;
        }
        TChDirDialog::handleEvent(event);
    }

private:
    std::filesystem::path &compilerPath_;
};

class ContextHelpFileDialog : public TFileDialog {
public:
    ContextHelpFileDialog(const char *wildCard, const char *title, const char *inputName,
                          ushort options, ushort historyId, ushort context)
        : TWindowInit(&TWindow::initFrame),
          TFileDialog(wildCard, title, inputName, options, static_cast<uchar>(historyId)) {
        helpCtx = context;
    }

    void handleEvent(TEvent &event) override {
        TFileDialog::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmHelp) {
            if (contextHelpHandler) contextHelpHandler(getHelpCtx());
            clearEvent(event);
        }
    }
};

class ContextHelpColorDialog : public TColorDialog {
public:
    ContextHelpColorDialog(TPalette *palette, TColorGroup *groups, ushort context)
        : TWindowInit(&TWindow::initFrame), TColorDialog(palette, groups) { helpCtx = context; }

    void handleEvent(TEvent &event) override {
        TColorDialog::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmHelp) {
            if (contextHelpHandler) contextHelpHandler(getHelpCtx());
            clearEvent(event);
        }
    }
};

class IDEStatusLine : public TStatusLine {
public:
    IDEStatusLine(const TRect &bounds, TStatusDef &defs) : TStatusLine(bounds, defs) {}

    void setMenuActive(bool active) {
        if (menuActive_ == active) return;
        menuActive_ = active;
        drawView();
    }

    void setPrefixMode(TFileEditor *editor, int mode) {
        if (mode && prefixEditor_ != editor) prefixPage_ = 0;
        prefixEditor_ = mode ? editor : nullptr;
        prefixMode_ = mode;
        drawView();
    }

    void setDiagnosticMessage(std::string text) {
        diagnosticMessage_ = std::move(text);
        drawView();
    }

    void advancePrefixPage(TFileEditor *editor) {
        if (prefixEditor_ != editor || !prefixMode_) return;
        const auto pages = makePages(commandsFor(prefixMode_), prefixFor(prefixMode_), availableHintWidth());
        if (pages.size() > 1)
            prefixPage_ = (prefixPage_ + 1) % static_cast<int>(pages.size());
        drawView();
    }

    const char *hint(ushort context) override {
        if (const char *text = contextHint(context); *text)
            return text;
        if (menuActive_ || (TProgram::application && TProgram::application->current != TProgram::deskTop))
            return "";
        const auto *window = TProgram::deskTop
            ? dynamic_cast<TEditWindow *>(TProgram::deskTop->current) : nullptr;
        if (!window || !editorSupportsPrefixKeys(window->editor))
            return "";

        if (prefixMode_ && prefixEditor_ == window->editor) {
            const auto pages = makePages(commandsFor(prefixMode_), prefixFor(prefixMode_), availableHintWidth());
            if (pages.empty()) return "";
            prefixPage_ %= static_cast<int>(pages.size());
            hintBuffer_ = pages[prefixPage_];
            return hintBuffer_.c_str();
        }
        return "~^Q~ Quick  ~^K~ Block";
    }

    void draw() override {
        if (menuActive_) {
            drawMenuHint(hint(helpCtx));
            return;
        }
        if (TProgram::application && TProgram::application->current != TProgram::deskTop) {
            TStatusLine::draw();
            return;
        }
        if (!diagnosticMessage_.empty()) {
            drawHint(diagnosticMessage_.c_str(), 0, size.x);
            return;
        }
        const auto *window = TProgram::deskTop
            ? dynamic_cast<TEditWindow *>(TProgram::deskTop->current) : nullptr;
        if (!window || !editorSupportsPrefixKeys(window->editor)) {
            TStatusLine::draw();
            return;
        }
        if (!prefixMode_ || prefixEditor_ != window->editor) {
            TStatusLine::draw();
            int column = 0;
            for (auto *item = items; item; item = item->next)
                if (item->text) column += cstrlen(item->text) + 2;
            if (column < size.x - 2)
                drawHint(hint(helpCtx), column + 2, size.x - column - 2);
            return;
        }
        drawHint(hint(helpCtx), 0, size.x);
    }

private:
    bool menuActive_ = false;
    std::string diagnosticMessage_;

    static const char *contextHint(ushort context) {
        switch (context) {
        case hcFileMenu: return "File management commands (Open, New, Save, etc.)";
        case hcEditMenu: return "Cut-and-paste editing commands";
        case hcSearchMenu: return "Text and error search commands";
        case hcRunMenu: return "Execute or single-step through a program";
        case hcCompileMenu: return "Compile to disk or memory";
        case hcDebugMenu: return "Evaluate expressions, modify data, set breakpoints and watches";
        case hcProjectMenu: return "Create and manage projects";
        case hcToolsMenu: return "Tracking commands and user installed tools";
        case hcOptionsMenu: return "Set defaults for compiler, editor, mouse, debugger, etc.";
        case hcWindowMenu: return "Open, arrange, and list windows";
        case hcHelpMenu: return "Get online help";
        case hcEnvironmentMenu: return "Specify environment settings";
        case hcEnvironmentPreferences: return "Specify desktop settings";
        case hcEnvironmentEditor: return "Specify editor settings";
        case hcEnvironmentMouse: return "Specify mouse settings";
        case hcEnvironmentStartup: return "Permanently change default startup options";
        case hcEnvironmentColors: return "Customize IDE colors for windows, menus, etc.";
        default: return "";
        }
    }

    void drawMenuHint(const char *text) {
        TDrawBuffer buffer;
        const TAttrPair color = getColor(0x0301);
        buffer.moveChar(0, ' ', color, size.x);
        buffer.moveCStr(1, "~F1~ Help", color, size.x - 1);
        if (size.x > 10) {
            buffer.moveStr(9, "|", color, size.x - 9);
            buffer.moveCStr(11, text, color, size.x - 11);
        }
        writeLine(0, 0, size.x, 1, buffer);
    }

    static int visibleHintLength(std::string_view text) {
        int length = 0;
        while (!text.empty()) {
            if (text.front() != '~') ++length;
            text.remove_prefix(1);
        }
        return length;
    }

    void drawHint(const char *text, int start, int width) {
        TDrawBuffer buffer;
        const TAttrPair normal = getColor(0x0301);
        buffer.moveChar(0, ' ', normal, width);
        buffer.moveCStr(0, text, normal, width);
        writeLine(start, 0, width, 1, buffer);
    }

    int availableHintWidth() const {
        if (prefixMode_) return size.x;
        int used = 0;
        for (auto *item = items; item; item = item->next)
            if (item->text) used += cstrlen(item->text) + 2;
        return std::max(0, size.x - used - 2);
    }

    static std::string_view prefixFor(int mode) {
        return mode == 1 ? "~^Q~" : "~^K~";
    }

    static std::vector<std::string_view> commandsFor(int mode) {
        if (mode == 1)
            return {"~A~:Rep", "~C~:Text", "~D~:EOL", "~E~:PgUp", "~F~:Find",
                    "~H~:DelB", "~L~:Len", "~M~:Macro", "~P~:Prev", "~R~:Top",
                    "~S~:BOL", "~X~:PgDn", "~Y~:DelE", "~[ / ]~:Match",
                    "~0-9~:Goto", "~Esc~:Exit"};
        return {"~B~:Start", "~K~:End", "~C~:Dup", "~H~:Hide", "~I~:Ind1",
                "~L~:Line", "~M~:Upper", "~O~:Lower", "~R~:Read", "~T~:Word",
                "~U~:Unind", "~V~:Move", "~W~:Write", "~Y~:Cut", "~0-9~:Mark",
                "~Tab~:Ind", "~Sh+B/C/E/H/K/L/M/O/P/T/V~:Rect", "~Sh+A~:Toggle",
                "~Ctrl+Ins~:Copy"};
    }

    static std::vector<std::string> makePages(const std::vector<std::string_view> &commands,
                                               std::string_view prefix, int width) {
        std::vector<std::string> pages;
        size_t first = 0;
        while (first < commands.size()) {
            std::string page(prefix);
            size_t end = first;
            while (end < commands.size()) {
                std::string candidate = page;
                candidate += ' ';
                candidate += commands[end];
                const bool finalCommand = end + 1 == commands.size();
                const int suffix = finalCommand ? 0 : 1 + visibleHintLength("~?~:More");
                if (visibleHintLength(candidate) + suffix > width) break;
                page = std::move(candidate);
                ++end;
            }
            if (end == first) {
                page += ' ';
                page += commands[end++];
            }
            if (end < commands.size()) page += " ~?~:More";
            pages.push_back(std::move(page));
            first = end;
        }
        return pages;
    }

    int prefixMode_ = 0;
    int prefixPage_ = 0;
    TFileEditor *prefixEditor_ = nullptr;
    std::string hintBuffer_;
};

class IDEMenuBar : public TMenuBar {
public:
    using TMenuBar::TMenuBar;

    void handleEvent(TEvent &event) override {
        const bool menuInput = event.what == evMouseDown ||
            (event.what == evCommand && event.message.command == cmMenu) ||
            (event.what == evKeyDown && (event.keyDown.controlKeyState & kbAltShift));
        auto *status = dynamic_cast<IDEStatusLine *>(TProgram::statusLine);
        if (menuInput && status) status->setMenuActive(true);
        TMenuBar::handleEvent(event);
        if (menuInput && status) {
            status->setMenuActive(false);
            status->update();
        }
    }
};

short nextWindowNumber() {
    bool used[10]{};
    if (TProgram::deskTop)
        for (TView *view = TProgram::deskTop->first(); view; view = view->nextView())
            if (auto *window = dynamic_cast<TWindow *>(view))
                if (window->number > 0 && window->number < 10)
                    used[window->number] = true;
    for (short number = 1; number < 10; ++number)
        if (!used[number]) return number;
    return wnNoNumber;
}

ushort execDialog(TDialog *dialog, void *data = nullptr) {
    TView *view = TProgram::application->validView(dialog);
    if (!view)
        return cmCancel;

    if (data)
        view->setData(data);
    const ushort result = TProgram::deskTop->execView(view);
    if (result != cmCancel && data)
        view->getData(data);
    TObject::destroy(view);
    return result;
}

TDialog *createFindDialog() {
    auto *dialog = new ContextHelpDialog(TRect(0, 0, 38, 12), "Find", hcFindDialog);
    dialog->options |= ofCentered;
    auto *input = new TInputLine(TRect(3, 3, 32, 4), maxFindStrLen - 1);
    dialog->insert(input);
    dialog->insert(new TLabel(TRect(2, 2, 15, 3), "~T~ext to find", input));
    dialog->insert(new THistory(TRect(32, 3, 35, 4), input, 10));
    dialog->insert(new TCheckBoxes(TRect(3, 5, 35, 7),
        new TSItem("~C~ase sensitive", new TSItem("~W~hole words only", nullptr))));
    dialog->insert(new TButton(TRect(14, 9, 24, 11), "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(26, 9, 36, 11), "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    return dialog;
}

TDialog *createReplaceDialog() {
    auto *dialog = new ContextHelpDialog(TRect(0, 0, 40, 16), "Replace", hcReplaceDialog);
    dialog->options |= ofCentered;
    auto *find = new TInputLine(TRect(3, 3, 34, 4), maxFindStrLen - 1);
    dialog->insert(find);
    dialog->insert(new TLabel(TRect(2, 2, 15, 3), "~T~ext to find", find));
    dialog->insert(new THistory(TRect(34, 3, 37, 4), find, 10));

    auto *replacement = new TInputLine(TRect(3, 6, 34, 7), maxReplaceStrLen - 1);
    dialog->insert(replacement);
    dialog->insert(new TLabel(TRect(2, 5, 12, 6), "~N~ew text", replacement));
    dialog->insert(new THistory(TRect(34, 6, 37, 7), replacement, 11));
    dialog->insert(new TCheckBoxes(TRect(3, 8, 37, 12),
        new TSItem("~C~ase sensitive",
        new TSItem("~W~hole words only",
        new TSItem("~P~rompt on replace",
        new TSItem("~R~eplace all", nullptr))))));
    dialog->insert(new TButton(TRect(17, 13, 27, 15), "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(28, 13, 38, 15), "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    return dialog;
}

TDialog *createSingleInputDialog(const char *title, const char *label, unsigned maxLength,
                                 ushort context) {
    auto *dialog = new ContextHelpDialog(TRect(0, 0, 58, 10), title, context);
    dialog->options |= ofCentered;
    auto *input = new TInputLine(TRect(3, 4, 55, 5), maxLength);
    dialog->insert(input);
    dialog->insert(new TLabel(TRect(3, 2, 54, 3), label, input));
    dialog->insert(new TButton(TRect(27, 7, 37, 9), "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(40, 7, 51, 9), "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    return dialog;
}

TDialog *createEditorDialog(TInputLine *&tabs, TInputLine *&extension,
                            TCheckBoxes *&options) {
    auto *dialog = new ContextHelpDialog(TRect(0, 0, 76, 20), "Editor Options", hcEditorDialog);
    dialog->options |= ofCentered;
    options = new TCheckBoxes(TRect(3, 3, 72, 10),
        new TSItem("Create backup ~f~iles",
        new TSItem("~I~nsert mode",
        new TSItem("~A~uto indent mode",
        new TSItem("~U~se tab characters",
        new TSItem("~O~ptimal fill",
        new TSItem("~B~ackspace unindents",
        new TSItem("~C~ursor through tabs",
        new TSItem("~G~roup Undo",
        new TSItem("~P~ersistent blocks",
        new TSItem("O~v~erwrite blocks",
        new TSItem("~S~yntax highlight",
        new TSItem("B~l~ock insert cursor",
        new TSItem("Find te~x~t at cursor", nullptr))))))))))))));
    options->helpCtx = hcEditorPersistentBlocks;
    options->setButtonState(((1u << 13) - 1) & ~((1u << 0) | (1u << 8)), False);
    dialog->insert(options);
    auto *highlight = new TInputLine(TRect(3, 13, 34, 14), 30);
    char highlightExtensions[] = "*.c;*.h;*.cc;*.cpp;*.hpp";
    highlight->setData(highlightExtensions);
    highlight->setState(sfDisabled, True);
    dialog->insert(highlight);
    dialog->insert(new TLabel(TRect(3, 12, 28, 13), "~H~ighlight extensions", highlight));
    tabs = new TInputLine(TRect(39, 13, 43, 14), 3);
    tabs->helpCtx = hcEditorTabSize;
    dialog->insert(tabs);
    dialog->insert(new TLabel(TRect(39, 12, 54, 13), "~T~ab size", tabs));
    extension = new TInputLine(TRect(3, 16, 18, 17), 15);
    dialog->insert(extension);
    dialog->insert(new TLabel(TRect(3, 15, 32, 16), "Default file e~x~tension", extension));
    dialog->insert(new TButton(TRect(48, 16, 58, 18), "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(61, 16, 72, 18), "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    return dialog;
}

TDialog *createCompilerDialog(TInputLine *&path) {
    auto *dialog = new CompilerDialog(TRect(0, 0, 68, 15));
    dialog->options |= ofCentered;
    auto *type = new TStaticText(TRect(3, 3, 64, 4), "Compiler type: GCC");
    dialog->insert(type);
    dialog->insert(new TStaticText(TRect(3, 5, 64, 6),
        "Path is optional; Browse selects a GCC toolchain folder."));
    path = new TInputLine(TRect(3, 8, 65, 9), MAX_PATH - 1);
    dialog->insert(path);
    dialog->insert(new TLabel(TRect(3, 7, 36, 8), "GCC folder or executable ~p~ath", path));
    dialog->insert(new TButton(TRect(3, 10, 14, 12), "~B~rowse...", cmCompilerBrowse, bfNormal));
    dialog->insert(new TButton(TRect(36, 12, 46, 14), "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(50, 12, 61, 14), "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    return dialog;
}

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (!length) return {};
    std::wstring value(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        value.data(), length);
    return value;
}

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) return {};
    std::string value(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        value.data(), length, nullptr, nullptr);
    return value;
}

std::vector<std::wstring> parseArguments(std::string_view text) {
    const std::wstring command = L"program " + utf8ToWide(text);
    int count = 0;
    LPWSTR *parts = CommandLineToArgvW(command.c_str(), &count);
    std::vector<std::wstring> result;
    if (parts) {
        for (int i = 1; i < count; ++i) result.emplace_back(parts[i]);
        LocalFree(parts);
    }
    return result;
}

std::string formatArguments(const std::vector<std::wstring> &arguments) {
    std::string result;
    for (const auto &argument : arguments) {
        if (!result.empty()) result += ' ';
        const auto value = wideToUtf8(argument);
        if (value.find_first_of(" \t\"") == std::string::npos) result += value;
        else result += '"' + value + '"';
    }
    return result;
}

bool isProjectFile(const std::filesystem::path &file, const std::filesystem::path &root) {
    std::error_code error;
    const auto relative = std::filesystem::relative(file, root, error);
    if (error || relative.empty()) return false;
    for (const auto &part : relative)
        if (part == L"..") return false;
    return true;
}

DesktopRect saveRect(const TRect &rect) {
    return {rect.a.x, rect.a.y, rect.b.x, rect.b.y};
}

TRect restoreRect(const DesktopRect &saved, const TRect &extent) {
    short width = static_cast<short>(std::max(1, saved.right - saved.left));
    short height = static_cast<short>(std::max(1, saved.bottom - saved.top));
    width = std::min<short>(width, extent.b.x - extent.a.x);
    height = std::min<short>(height, extent.b.y - extent.a.y);
    const short left = std::clamp<short>(static_cast<short>(saved.left), extent.a.x,
                                         static_cast<short>(extent.b.x - width));
    const short top = std::clamp<short>(static_cast<short>(saved.top), extent.a.y,
                                        static_cast<short>(extent.b.y - height));
    return TRect(left, top, static_cast<short>(left + width), static_cast<short>(top + height));
}

std::string identifierAtCursor(TEditor &editor) {
    auto identifierChar = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '#' || c == '.';
    };
    uint position = editor.curPtr;
    if (position >= editor.bufLen ||
        !identifierChar(static_cast<unsigned char>(editor.bufChar(position)))) {
        if (position == 0 ||
            !identifierChar(static_cast<unsigned char>(editor.bufChar(position - 1))))
            return {};
        --position;
    }
    uint first = position;
    while (first > 0 && identifierChar(static_cast<unsigned char>(editor.bufChar(first - 1))))
        --first;
    uint last = position + 1;
    while (last < editor.bufLen &&
           identifierChar(static_cast<unsigned char>(editor.bufChar(last))))
        ++last;
    const unsigned char leading = static_cast<unsigned char>(editor.bufChar(first));
    if ((leading >= '0' && leading <= '9') || first == last) return {};
    std::string word;
    word.reserve(last - first);
    for (uint i = first; i < last; ++i) word.push_back(editor.bufChar(i));
    return word;
}

ushort editDialog(int dialog, ...) {
    va_list args;
    va_start(args, dialog);
    char message[512]{};
    ushort result = cmCancel;

    switch (dialog) {
    case edOutOfMemory:
        result = messageBox("Not enough memory for this operation.", mfError | mfOKButton);
        break;
    case edReadError:
        std::snprintf(message, sizeof(message), "Error reading file %s.", va_arg(args, char *));
        result = messageBox(message, mfError | mfOKButton);
        break;
    case edWriteError:
        std::snprintf(message, sizeof(message), "Error writing file %s.", va_arg(args, char *));
        result = messageBox(message, mfError | mfOKButton);
        break;
    case edCreateError:
        std::snprintf(message, sizeof(message), "Error creating file %s.", va_arg(args, char *));
        result = messageBox(message, mfError | mfOKButton);
        break;
    case edSaveModify:
        std::snprintf(message, sizeof(message), "%s has been modified. Save?", va_arg(args, char *));
        result = messageBox(message, mfInformation | mfYesNoCancel);
        break;
    case edSaveUntitled:
        result = messageBox("Save untitled file?", mfInformation | mfYesNoCancel);
        break;
    case edSaveAs:
        result = execDialog(new ContextHelpFileDialog("*.*", "Save file as", "~N~ame", fdOKButton, 101,
                                                       hcFileMenu),
                            va_arg(args, char *));
        break;
    case edFind:
        result = execDialog(createFindDialog(), va_arg(args, char *));
        break;
    case edSearchFailed:
        result = messageBox("Search string not found.", mfError | mfOKButton);
        break;
    case edReplace:
        result = execDialog(createReplaceDialog(), va_arg(args, char *));
        break;
    case edReplacePrompt:
        result = messageBox("Replace this occurrence?", mfYesNoCancel | mfInformation);
        break;
    }

    va_end(args);
    return result;
}

class TurboIDEApp : public TApplication {
public:
    TurboIDEApp();
    ~TurboIDEApp() override;
    void handleEvent(TEvent &event) override;
    void idle() override;
    void shutDown() override;
    bool goToLocation(const std::filesystem::path &file, int line, int column);
    bool trackMessage(const std::filesystem::path &file, int line, int column,
                      size_t index);
    bool gotoMessage(size_t index);
    void clearMessages();
    void selectMessageIndex(size_t index);
    void setMessageStatus(const BuildMessage *message);
    void hideMessages();
    void dismissMessages(BuildMessagesWindow *window);
    void dismissBuildPopup();
    void projectWindowClosed(ProjectFilesWindow *window);
    void openProjectItem();
    void addProjectItem();
    void deleteProjectItem();
    void updateDebugWatches(DebugWatchesWindow *window);
    void dismissDebugWatches(DebugWatchesWindow *window);
    void dismissWatch(DebugWatchWindow *window);
    void addWatch();
    void evaluateWatches();
    void deleteWatch(short index);

    static TMenuBar *initMenuBar(TRect r);
    static TStatusLine *initStatusLine(TRect r);
    TPalette &getPalette() const override { return ideTheme().application; }

private:
    TEditWindow *openEditor(const char *fileName, const TRect *bounds = nullptr);
    void newEditor();
    void openFile();
    void newProject();
    void openProject();
    void closeProject();
    void showProjectWindow();
    void showWindowList();
    void syncEditMenuState();
    void goToLine();
    void compileCurrent(bool runAfterBuild = false, bool debugAfterBuild = false);
    void buildProject(bool runAfterBuild = false, bool debugAfterBuild = false);
    void debugProject();
    void showLastUserScreen();
    void showCommandPrompt();
    void runBuiltProgram(const std::filesystem::path &executable);
    void startDebuggee(const std::filesystem::path &executable);
    void toggleBreakpoint();
    void continueDebuggee(const char *command);
    void stopDebuggee();
    void applyDebugStop(const DebugStop &stop);
    void showDebugWatches();
    void showDebugLocals();
    std::vector<DebugBreakpoint> allBreakpoints() const;
    TEditWindow *currentEditorWindow() const;
    void restoreEditorAfterRun();
    bool saveBuildInputs(const BuildRequest &request);
    void startBuild(BuildRequest request, bool runAfterBuild = false,
                    std::vector<std::wstring> runArguments = {},
                    std::filesystem::path runDirectory = {},
                    bool debugAfterBuild = false);
    void showBuildResult(BuildResult result);
    void showMessages();
    void navigateMessage(bool forward);
    void editRunParameters();
    void editRunDirectory();
    void changeDirectory();
    void editEnvironment();
    void editCompiler();
    void editColors();
    bool activateProject(const std::filesystem::path &file, bool showWindow = true);
    void closeUnusedUntitledEditors();
    bool saveDesktopSession();
    void restoreDesktopSession();
    void showHelpContents(std::string_view word = {});
    void showHelpContext(ushort context);
    ushort activeHelpContext() const;
    void rememberProject();
    void showAbout();

    Project project_;
    ProjectFilesWindow *projectWindow_ = nullptr;
    bool hasProject_ = false;
    bool hasHelpBounds_ = false;
    DesktopRect helpBounds_;
    bool editMenuCommandsDisabled_ = false;
    bool buildRunning_ = false;
    std::thread buildThread_;
    std::mutex buildMutex_;
    std::optional<BuildResult> finishedBuild_;
    std::vector<BuildMessage> messages_;
    BuildMessagesWindow *messagesWindow_ = nullptr;
    BuildProgressWindow *buildPopup_ = nullptr;
    bool buildPopupSuccess_ = false;
    bool blinkSuccess_ = false;
    unsigned buildWarnings_ = 0;
    unsigned buildErrors_ = 0;
    std::chrono::steady_clock::time_point lastBlink_{};
    size_t messageIndex_ = 0;
    TView *buildReturnView_ = nullptr;
    HANDLE ideScreenBuffer_ = INVALID_HANDLE_VALUE;
    HANDLE consoleInput_ = INVALID_HANDLE_VALUE;
    bool ownsConsoleInput_ = false;
    HANDLE userScreenBuffer_ = INVALID_HANDLE_VALUE;
    bool runAfterBuild_ = false;
    bool debugAfterBuild_ = false;
    std::unique_ptr<GdbSession> debugger_;
    DebugWatchesWindow *debugWatchesWindow_ = nullptr;
    std::vector<DebugVariable> debugVariables_;
    std::vector<std::string> watchExpressions_;
    std::vector<DebugVariable> watchVariables_;
    DebugWatchWindow *watchWindow_ = nullptr;
    std::unordered_map<std::wstring, std::set<int>> breakpoints_;
    DebugStop currentDebugStop_;
    std::vector<std::wstring> runArguments_;
    std::filesystem::path runWorkingDirectory_;
    std::filesystem::path runReturnFile_;
    IDESettings settings_;
    std::vector<std::wstring> standaloneArguments_;
    std::filesystem::path standaloneRunDirectory_;
    std::filesystem::path startupProject_;
};

class ProjectFileList : public TListViewer {
public:
    ProjectFileList(const TRect &bounds, TScrollBar *vertical, TurboIDEApp *app,
                    const Project *project)
        : TListViewer(bounds, 1, nullptr, vertical), app_(app), project_(project) {
        options |= ofSelectable;
        setRange(static_cast<short>(project_->sources.size()));
    }
    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || static_cast<size_t>(item) >= project_->sources.size()) { *dest = 0; return; }
        const auto name = project_->sources[static_cast<size_t>(item)].filename().u8string();
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%c %s",
                      item == focused ? '>' : ' ', name.c_str());
    }
    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown) {
            if (event.keyDown.keyCode == kbEnter) { app_->openProjectItem(); clearEvent(event); return; }
            if (event.keyDown.keyCode == kbIns) { app_->addProjectItem(); clearEvent(event); return; }
            if (event.keyDown.keyCode == kbDel) { app_->deleteProjectItem(); clearEvent(event); return; }
        }
        TListViewer::handleEvent(event);
    }
    void selectItem(short item) override {
        if (item >= 0 && static_cast<size_t>(item) < project_->sources.size())
            app_->openProjectItem();
    }
    TPalette &getPalette() const override {
        static TPalette palette("\x01\x01\x02\x03\x04", 5);
        return palette;
    }
    short selected() const { return focused; }
private:
    TurboIDEApp *app_;
    const Project *project_;
};

class ProjectFilesWindow : public TWindow {
public:
    ProjectFilesWindow(const TRect &bounds, TurboIDEApp *app, const Project *project,
                       short windowNumber)
        : TWindowInit(&TWindow::initFrame), TWindow(bounds, "Project", windowNumber), app_(app) {
        helpCtx = hcProjectWindow;
        auto *bar = standardScrollBar(sbVertical | sbHandleKeyboard);
        list_ = new ProjectFileList(TRect(1, 1, size.x - 2, size.y - 1), bar, app, project);
        insert(list_);
    }
    short selected() const { return list_->selected(); }
    void selectFirst() {
        list_->focusItem(0);
        setCurrent(list_, TGroup::enterSelect);
        list_->setState(sfSelected | sfActive | sfFocused, True);
        list_->drawView();
    }
    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown) {
            list_->handleEvent(event);
            if (event.what == evNothing)
                return;
        }
        TWindow::handleEvent(event);
    }
    bool handleListKey(TEvent &event) {
        list_->handleEvent(event);
        return event.what == evNothing;
    }
    TPalette &getPalette() const override {
        static TPalette palette("\x10\x11\x12\x13\x14\x15\x16\x17\x10\x11\x12\x13", 12);
        return palette;
    }
    void close() override { app_->projectWindowClosed(this); TWindow::close(); }
private:
    TurboIDEApp *app_;
    ProjectFileList *list_;
};

class WindowList : public TListViewer {
public:
    WindowList(const TRect &bounds, TScrollBar *scrollBar, const std::vector<std::string> *titles)
        : TListViewer(bounds, 1, nullptr, scrollBar), titles_(titles) {
        setRange(static_cast<short>(titles_->size()));
    }
    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || static_cast<size_t>(item) >= titles_->size()) { *dest = 0; return; }
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", (*titles_)[item].c_str());
    }
    short selected() const { return focused; }
private:
    const std::vector<std::string> *titles_;
};

class WindowListDialog : public TDialog {
public:
    WindowListDialog(const std::vector<std::string> *titles, short *selection)
        : TWindowInit(&TWindow::initFrame), TDialog(TRect(0, 0, 52, 15), "List all"), selection_(selection) {
        options |= ofCentered;
        auto *scrollBar = new TScrollBar(TRect(47, 2, 48, 11));
        insert(scrollBar);
        list_ = new WindowList(TRect(2, 2, 47, 11), scrollBar, titles);
        insert(list_);
        insert(new TButton(TRect(25, 12, 35, 14), "O~K~", cmOK, bfDefault));
        insert(new TButton(TRect(37, 12, 47, 14), "Cancel", cmCancel, bfNormal));
        list_->focusItem(*selection_);
        list_->select();
    }
    void handleEvent(TEvent &event) override {
        TDialog::handleEvent(event);
        *selection_ = list_->selected();
        if (event.what == evBroadcast && event.message.command == cmListItemSelected) {
            endModal(cmOK);
            clearEvent(event);
        }
    }
private:
    WindowList *list_;
    short *selection_;
};

class BuildProgressWindow : public TDialog {
public:
    explicit BuildProgressWindow(const TRect &bounds, std::string mainFile,
                                 size_t totalLines, size_t mainFileLines)
        : TWindowInit(&TWindow::initFrame), TDialog(bounds, "Compiling"),
          mainFile_(std::move(mainFile)), totalLines_(totalLines),
          mainFileLines_(mainFileLines) {
        flags = 0;
    }

    void draw() override {
        TDialog::draw();
        char line[96];
        std::snprintf(line, sizeof(line), "Main file: %s", mainFile_.c_str());
        writeStr(2, 1, line, 1);
        std::snprintf(line, sizeof(line), "Compiling: %s", mainFile_.c_str());
        writeStr(2, 2, line, 1);
        writeStr(35, 4, "Total", 1);
        writeStr(49, 4, "File", 1);
        writeStr(4, 5, "Lines compiled:", 1);
        std::snprintf(line, sizeof(line), "%zu", totalLines_);
        writeStr(35, 5, line, 1);
        std::snprintf(line, sizeof(line), "%zu", mainFileLines_);
        writeStr(49, 5, line, 1);
        writeStr(19, 6, "Warnings:", 1);
        std::snprintf(line, sizeof(line), "%u", warnings_);
        writeStr(36, 6, line, 1);
        writeStr(49, 6, line, 1);
        writeStr(21, 7, "Errors:", 1);
        std::snprintf(line, sizeof(line), "%u", errors_);
        writeStr(36, 7, line, 1);
        writeStr(49, 7, line, 1);
        if (programBytes_ == 0) {
            writeStr(4, 10, "Program size: n/a", 1);
        } else {
            std::snprintf(line, sizeof(line), "Program size: %llu bytes",
                          static_cast<unsigned long long>(programBytes_));
            writeStr(4, 10, line, 1);
        }

        constexpr short statusY = 12;
        const TColorAttr statusColor = ideTheme().buildStatus;
        const short width = size.x - 2;
        std::vector<TScreenCell> status(static_cast<size_t>(width),
            TScreenCell(TScreenCharacter(' '), statusColor));
        const std::string left = appSuccess_ ? "Success -" : "Ctrl-Break to quit";
        const std::string right = "Press Any Key";
        const short groupWidth = static_cast<short>(left.size() + (appSuccess_ ? 1 + right.size() : 0));
        const short start = std::max<short>(0, (width - groupWidth) / 2);
        for (size_t i = 0; i < left.size(); ++i)
            status[static_cast<size_t>(start) + i] = TScreenCell(TScreenCharacter(left[i]), statusColor);
        if (!appSuccess_ || blink_)
            for (size_t i = 0; i < right.size(); ++i)
                status[static_cast<size_t>(start) + left.size() + 1 + i] =
                    TScreenCell(TScreenCharacter(right[i]), statusColor);
        writeBuf(1, statusY, width, 1, status.data());
    }

    void setSuccess(bool value, bool blink, unsigned warnings = 0, unsigned errors = 0,
                    std::uintmax_t programBytes = 0) {
        appSuccess_ = value;
        blink_ = blink;
        warnings_ = warnings;
        errors_ = errors;
        if (programBytes != 0 || !value)
            programBytes_ = programBytes;
        drawView();
    }

    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown) {
            if (appSuccess_) {
                event.what = evCommand;
                event.message.command = cmDismissBuildPopup;
                putEvent(event);
            }
            clearEvent(event);
            return;
        }
        TDialog::handleEvent(event);
    }

private:
    bool appSuccess_ = false;
    bool blink_ = false;
    unsigned warnings_ = 0;
    unsigned errors_ = 0;
    std::string mainFile_;
    size_t totalLines_ = 0;
    size_t mainFileLines_ = 0;
    std::uintmax_t programBytes_ = 0;
};

size_t countLines(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    size_t lines = 0;
    std::string line;
    while (std::getline(input, line))
        ++lines;
    return lines;
}

std::wstring normalizedPathKey(const std::filesystem::path &file) {
    const auto absolute = std::filesystem::absolute(file).lexically_normal();
    std::wstring result(32768, L'\0');
    const DWORD length = GetLongPathNameW(absolute.c_str(), result.data(),
                                           static_cast<DWORD>(result.size()));
    if (length && length < result.size())
        result.resize(length);
    else
        result = absolute.wstring();
    return result;
}

int editorCurrentLine(TFileEditor *editor) {
    if (!editor) return 0;
    int line = 1;
    const uint target = editor->lineStart(editor->curPtr);
    uint position = 0;
    while (position < target && position < editor->bufLen) {
        const uint next = editor->nextLine(position);
        if (next <= position) break;
        position = next;
        ++line;
    }
    return line;
}

class BuildMessageList : public TListViewer {
public:
    BuildMessageList(const TRect &bounds, TScrollBar *horizontal,
                     TScrollBar *vertical, TurboIDEApp *app,
                     const std::vector<BuildMessage> *messages)
        : TListViewer(bounds, 1, horizontal, vertical), app_(app), messages_(messages) {
        options |= ofSelectable;
        setRange(static_cast<short>(messages_->size()));
    }

    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || static_cast<size_t>(item) >= messages_->size()) {
            *dest = '\0';
            return;
        }
        const auto &message = (*messages_)[static_cast<size_t>(item)];
        const auto &text = message.display.empty() ? message.text : message.display;
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", text.c_str());
    }

    void popupMessageMenu(TPoint where) {
        popupMenu(where,
            *new TMenuItem("~C~lear", cmMessagesClear, kbNoKey) + newLine() +
            *new TMenuItem("~G~oto source", cmMessagesGotoSource, kbNoKey) +
            *new TMenuItem("~T~rack source", cmMessagesTrackSource, kbNoKey), app_);
    }

    void selectItem(short item) override {
        if (item >= 0 && static_cast<size_t>(item) < messages_->size()) {
            const auto &selected = (*messages_)[static_cast<size_t>(item)];
            app_->selectMessageIndex(static_cast<size_t>(item));
            if (selected.hasLocation)
                app_->trackMessage(selected.file, selected.line, selected.column,
                                   static_cast<size_t>(item));
        }
    }

    TPalette &getPalette() const override {
        return ideTheme().messagesList;
    }

    TColorAttr mapColor(uchar index) override {
        const TPalette &palette = getPalette();
        if (index == 0 || index > palette[0])
            return errorAttr;
        TColorAttr color = app_->mapColor(palette[index]);
        if (index == 3 || index == 4) {
            const TColorAttr menuSelected = app_->mapColor(menuSelectedPaletteIndex);
            color.setBackground(menuSelected.getBackground());
        }
        return color;
    }

    void handleEvent(TEvent &event) override {
        if (event.what == evMouseDown && (event.mouse.buttons & mbRightButton)) {
            popupMessageMenu(event.mouse.where);
            clearEvent(event);
            return;
        }
        if (event.what == evKeyDown && TKey(event.keyDown) == kbShiftF10) {
            TPoint where{};
            where.x = 0;
            where.y = size.y - 1;
            popupMessageMenu(makeGlobal(where));
            clearEvent(event);
            return;
        }
        if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter) {
            if (focused >= 0 && static_cast<size_t>(focused) < messages_->size()) {
                app_->gotoMessage(static_cast<size_t>(focused));
            }
            clearEvent(event);
            return;
        }
        const short previousFocus = focused;
        TListViewer::handleEvent(event);
        if (focused != previousFocus && focused >= 0 &&
            static_cast<size_t>(focused) < messages_->size())
            selectItem(focused);
        else if (focused < 0 || static_cast<size_t>(focused) >= messages_->size())
            app_->setMessageStatus(nullptr);
    }

    void updateMessages() {
        setRange(static_cast<short>(messages_->size()));
        size_t maxWidth = 0;
        for (const auto &message : *messages_) {
            const auto &text = message.display.empty() ? message.text : message.display;
            maxWidth = std::max(maxWidth, text.size());
        }
        if (hScrollBar)
            hScrollBar->setRange(0, std::max<int>(0, static_cast<int>(maxWidth) - size.x));
        drawView();
    }

    void focusMessage(size_t index) {
        if (index < messages_->size())
            focusItem(static_cast<short>(index));
    }

private:
    static constexpr uchar menuSelectedPaletteIndex = 5;
    TurboIDEApp *app_;
    const std::vector<BuildMessage> *messages_;
};

class BuildMessagesWindow : public TWindow {
public:
    BuildMessagesWindow(const TRect &bounds, TurboIDEApp *app,
                        const std::vector<BuildMessage> *messages, short windowNumber)
        : TWindowInit(&TWindow::initFrame),
          TWindow(bounds, "Messages", windowNumber), app_(app) {
        helpCtx = hcMessagesWindow;
        auto *horizontal = standardScrollBar(sbHorizontal | sbHandleKeyboard);
        auto *vertical = standardScrollBar(sbVertical | sbHandleKeyboard);
        horizontal->setStep(size.x - 2, 1);
        vertical->setStep(size.y - 2, 1);
        list_ = new BuildMessageList(TRect(1, 1, size.x - 1, size.y - 2),
                                     horizontal, vertical, app, messages);
        insert(list_);
        setCurrent(list_, TGroup::enterSelect);
    }

    void updateMessages() { list_->updateMessages(); }
    void focusMessage(size_t index) {
        list_->focusMessage(index);
        setCurrent(list_, TGroup::enterSelect);
        list_->setState(sfSelected | sfActive | sfFocused, True);
        list_->drawView();
    }

    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown) {
            list_->handleEvent(event);
            if (event.what == evNothing)
                return;
        }
        TWindow::handleEvent(event);
    }

    TPalette &getPalette() const override {
        return ideTheme().messagesWindow;
    }

    void close() override {
        app_->dismissMessages(this);
        TWindow::close();
    }

private:
    TurboIDEApp *app_;
    BuildMessageList *list_;
};

class DebugVariableList : public TListViewer {
public:
    DebugVariableList(const TRect &bounds, const std::vector<DebugVariable> *variables)
        : TListViewer(bounds, 1, nullptr, nullptr), variables_(variables) {}

    void getText(char *dest, short item, short maxLen) override {
        if (variables_->empty() && item == 0) {
            std::snprintf(dest, static_cast<size_t>(maxLen) + 1,
                          "%s", "No local variables at this location");
            return;
        }
        if (item < 0 || static_cast<size_t>(item) >= variables_->size()) {
            *dest = '\0';
            return;
        }
        const auto &variable = (*variables_)[static_cast<size_t>(item)];
        const std::string text = variable.name + " = " + variable.value +
            (variable.type.empty() ? "" : " : " + variable.type);
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", text.c_str());
    }

    void updateVariables() {
        setRange(static_cast<short>(std::max<size_t>(1, variables_->size())));
        drawView();
    }

    TPalette &getPalette() const override { return ideTheme().messagesList; }

private:
    const std::vector<DebugVariable> *variables_;
};

class DebugWatchesWindow : public TWindow {
public:
    DebugWatchesWindow(const TRect &bounds, TurboIDEApp *app,
                       const std::vector<DebugVariable> *variables, short windowNumber)
        : TWindowInit(&TWindow::initFrame),
          TWindow(bounds, "Locals", windowNumber), app_(app) {
        helpCtx = hcWatchWindow;
        list_ = new DebugVariableList(TRect(1, 1, size.x - 1, size.y - 1), variables);
        insert(list_);
        list_->updateVariables();
    }

    void updateVariables() { list_->updateVariables(); }

    void close() override {
        app_->dismissDebugWatches(this);
        TWindow::close();
    }

private:
    TurboIDEApp *app_;
    DebugVariableList *list_;
};

class DebugWatchList : public TListViewer {
public:
    DebugWatchList(const TRect &bounds, const std::vector<DebugVariable> *variables)
        : TListViewer(bounds, 1, nullptr, nullptr), variables_(variables) {}
    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || static_cast<size_t>(item) >= variables_->size()) {
            std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", "No watches");
            return;
        }
        const auto &watch = (*variables_)[static_cast<size_t>(item)];
        const std::string text = watch.name + " = " + watch.value;
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", text.c_str());
    }
    void refresh() { setRange(static_cast<short>(std::max<size_t>(1, variables_->size()))); drawView(); }
    TPalette &getPalette() const override { return ideTheme().messagesList; }
private:
    const std::vector<DebugVariable> *variables_;
};

class DebugWatchWindow : public TWindow {
public:
    DebugWatchWindow(const TRect &bounds, TurboIDEApp *app,
                     const std::vector<DebugVariable> *variables, short windowNumber)
        : TWindowInit(&TWindow::initFrame), TWindow(bounds, "Watches", windowNumber), app_(app) {
        helpCtx = hcWatchWindow;
        list_ = new DebugWatchList(TRect(1, 1, size.x - 1, size.y - 1), variables);
        insert(list_);
        list_->refresh();
    }
    void refresh() { list_->refresh(); }
    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown && event.keyDown.keyCode == kbDel) {
            app_->deleteWatch(list_->focused);
            clearEvent(event);
            return;
        }
        TWindow::handleEvent(event);
    }
    void close() override {
        app_->dismissWatch(this);
        TWindow::close();
    }
private:
    TurboIDEApp *app_;
    DebugWatchList *list_;
};

TurboIDEApp::TurboIDEApp()
    : TProgInit(&TurboIDEApp::initStatusLine,
                &TurboIDEApp::initMenuBar,
                &TurboIDEApp::initDeskTop) {
    disableCommand(cmNotReady);
    contextHelpHandler = [this](ushort context) { showHelpContext(context); };
    TEditor::editorDialog = editDialog;
    SetConsoleCtrlHandler(handleBuildControlEvent, TRUE);
    ideScreenBuffer_ = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    consoleInput_ = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inputMode = 0;
    if (!consoleInput_ || consoleInput_ == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(consoleInput_, &inputMode)) {
        consoleInput_ = CreateFileW(L"CONIN$", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        ownsConsoleInput_ = consoleInput_ != INVALID_HANDLE_VALUE;
    }
    if (ideScreenBuffer_ != INVALID_HANDLE_VALUE)
        userScreenBuffer_ = createUserScreenBuffer(ideScreenBuffer_);
    loadSettings(settings_);
    if (settings_.backupFiles)
        TEditor::editorFlags |= efBackupFiles;
    else
        TEditor::editorFlags &= ~efBackupFiles;
    setDefaultPersistentBlocks(settings_.persistentBlocks);
    TEditor::tabSize = settings_.tabSize;
    std::error_code directoryError;
    if (!settings_.currentDirectory.empty())
        std::filesystem::current_path(settings_.currentDirectory, directoryError);
    if (directoryError)
        directoryError.clear();
    settings_.currentDirectory = std::filesystem::current_path(directoryError);
    openEditor(nullptr);
    if (!settings_.lastProject.empty() &&
        _wcsicmp(settings_.lastProject.extension().c_str(), L".prj") == 0 &&
        std::filesystem::exists(settings_.lastProject))
        startupProject_ = settings_.lastProject;
    else if (!settings_.lastProject.empty()) {
        settings_.lastProject.clear();
        saveSettings(settings_);
    }
}

TurboIDEApp::~TurboIDEApp() {
    if (debugger_) {
        std::string ignored;
        debugger_->stop(ignored);
        debugger_.reset();
    }
    buildControlHandlerActive.store(false);
    SetConsoleCtrlHandler(handleBuildControlEvent, FALSE);
    if (buildThread_.joinable())
        buildThread_.join();
    if (ideScreenBuffer_ != INVALID_HANDLE_VALUE)
    SetConsoleActiveScreenBuffer(ideScreenBuffer_);
    if (userScreenBuffer_ != INVALID_HANDLE_VALUE)
        CloseHandle(userScreenBuffer_);
    if (ownsConsoleInput_ && consoleInput_ != INVALID_HANDLE_VALUE)
        CloseHandle(consoleInput_);
    if (ideScreenBuffer_ != INVALID_HANDLE_VALUE)
        CloseHandle(ideScreenBuffer_);
}

void TurboIDEApp::shutDown() {
    if (debugger_) {
        std::string ignored;
        debugger_->stop(ignored);
        debugger_.reset();
    }
    saveDesktopSession();
    rememberProject();
    saveSettings(settings_);
    TApplication::shutDown();
}

TEditWindow *TurboIDEApp::openEditor(const char *fileName, const TRect *savedBounds) {
    if (fileName) {
        const int pathLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                   fileName, -1, nullptr, 0);
        if (pathLength == 0) {
            messageBox("The file name is not valid UTF-8.", mfError | mfOKButton);
            return nullptr;
        }
        std::vector<wchar_t> widePath(static_cast<size_t>(pathLength));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1,
                            widePath.data(), pathLength);
        const auto requested = std::filesystem::absolute(std::filesystem::path(widePath.data())).lexically_normal();
        for (TView *view = deskTop->first(); view; view = view->nextView()) {
            auto *window = dynamic_cast<TEditWindow *>(view);
            if (!window || !window->editor->fileName[0]) continue;
            const auto opened = std::filesystem::absolute(
                std::filesystem::u8path(window->editor->fileName)).lexically_normal();
            if (_wcsicmp(opened.c_str(), requested.c_str()) == 0) {
                window->show();
                window->putInFrontOf(deskTop->first());
                deskTop->setCurrent(window, TGroup::enterSelect);
                window->editor->trackCursor(True);
                return window;
            }
        }
        HANDLE file = CreateFileW(widePath.data(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            messageBox("Cannot open the selected file.", mfError | mfOKButton);
            return nullptr;
        }
        CloseHandle(file);
        const auto path = std::filesystem::u8path(fileName);
        if (_wcsicmp(path.extension().c_str(), L".prj") == 0)
            messageBox("This is a TurboIDE project file. Use Project | Open project "
                       "to load it; it will now open as text.", mfInformation | mfOKButton);
    }

    const TRect bounds = savedBounds ? *savedBounds : deskTop->getExtent();
    TView *view = validView(new SyntaxEditWindow(bounds, fileName ? fileName : "",
                                                  nextWindowNumber()));
    if (view) {
        view->helpCtx = hcEditWindow;
        deskTop->insert(view);
        return dynamic_cast<TEditWindow *>(view);
    }
    return nullptr;
}

void TurboIDEApp::newEditor() {
    std::string extension = settings_.defaultExtension;
    if (extension.empty() || extension[0] != '.') extension.insert(extension.begin(), '.');
    const auto directory = settings_.currentDirectory;
    auto candidate = directory / (std::string("NONAME") + extension);
    bool primaryIsOpen = false;
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *window = dynamic_cast<TEditWindow *>(view);
        if (!window || !window->editor->fileName[0]) continue;
        const auto opened = std::filesystem::absolute(
            std::filesystem::u8path(window->editor->fileName)).lexically_normal();
        if (_wcsicmp(opened.c_str(), candidate.c_str()) == 0) {
            primaryIsOpen = true;
            break;
        }
    }
    if (primaryIsOpen) {
        bool found = false;
        for (unsigned i = 0; i < 100; ++i) {
            char suffix[3]{};
            std::snprintf(suffix, sizeof(suffix), "%02u", i);
            candidate = directory / (std::string("NONAME") + suffix + extension);
            if (!std::filesystem::exists(candidate)) { found = true; break; }
        }
        if (!found) { messageBox("All NONAME00 to NONAME99 files exist.", mfError | mfOKButton); return; }
    }
    if (std::filesystem::exists(candidate)) {
        const auto text = candidate.u8string();
        openEditor(text.c_str());
        return;
    }
    std::ofstream file(candidate, std::ios::binary);
    if (!file) {
        messageBox("Cannot create the new source file.", mfError | mfOKButton);
        return;
    }
    file.close();
    const auto text = candidate.u8string();
    openEditor(text.c_str());
}

void TurboIDEApp::openFile() {
    char fileName[MAXPATH] = "*.*";
    if (execDialog(new ContextHelpFileDialog("*.*", "Open file", "~N~ame", fdOpenButton, 100,
                                             hcFileMenu), fileName)
        == cmCancel)
        return;

    openEditor(fileName);
}

void TurboIDEApp::newProject() {
    char projectName[MAXPATH] = "*.prj";
    if (execDialog(new ContextHelpFileDialog("*.prj", "New project", "~N~ame", fdOKButton, 102,
                                             hcProjectMenu), projectName)
        == cmCancel)
        return;

    const auto projectFile = std::filesystem::absolute(std::filesystem::u8path(projectName)).lexically_normal();
    if (std::filesystem::exists(projectFile)) {
        if (messageBox("Project already exists. Open it instead?", mfYesNoCancel | mfConfirmation) == cmYes)
            activateProject(projectFile);
        return;
    }
    std::ofstream output(projectFile, std::ios::binary | std::ios::trunc);
    if (!output) {
        messageBox("Cannot create project file.", mfError | mfOKButton);
        return;
    }
    output.close();
    if (!output) {
        messageBox("Cannot write project file.", mfError | mfOKButton);
        return;
    }
    if (activateProject(projectFile, false)) {
        showProjectWindow();
        restoreDesktopSession();
    }
}

void TurboIDEApp::openProject() {
    char fileName[MAXPATH] = "*.prj";
    if (execDialog(new ContextHelpFileDialog("*.prj", "Open project", "~N~ame", fdOpenButton, 103,
                                             hcProjectMenu), fileName)
        == cmCancel)
        return;

    activateProject(std::filesystem::u8path(fileName));
}

bool TurboIDEApp::activateProject(const std::filesystem::path &file, bool showWindow) {
    if (_wcsicmp(file.extension().c_str(), L".prj") != 0) {
        messageBox("TurboIDE projects use the .prj extension.", mfError | mfOKButton);
        return false;
    }
    Project loaded;
    std::string error;
    if (!loadProject(file, loaded, error)) {
        messageBox(error.c_str(), mfError | mfOKButton);
        return false;
    }
    if (debugger_) stopDebuggee();
    saveDesktopSession();
    if (projectWindow_) projectWindow_->close();
    project_ = std::move(loaded);
    hasProject_ = true;
    breakpoints_.clear();
    watchExpressions_.clear();
    watchVariables_.clear();
    closeUnusedUntitledEditors();
    settings_.lastProject = project_.file;
    saveSettings(settings_);
    if (showWindow) {
        showProjectWindow();
        restoreDesktopSession();
        bool hasEditor = false;
        for (TView *view = deskTop->first(); view; view = view->nextView()) {
            auto *window = dynamic_cast<TEditWindow *>(view);
            if (!window || !window->editor->fileName[0]) continue;
            const auto openFile = std::filesystem::absolute(
                std::filesystem::u8path(window->editor->fileName)).lexically_normal();
            if (isProjectFile(openFile, project_.file.parent_path()) &&
                _wcsicmp(openFile.extension().c_str(), L".prj") != 0) {
                hasEditor = true;
                break;
            }
        }
        if (!hasEditor && !project_.sources.empty()) {
            const auto primary = project_.sources.front().u8string();
            openEditor(primary.c_str());
        }
    }
    return true;
}

void TurboIDEApp::closeUnusedUntitledEditors() {
    std::vector<TEditWindow *> unused;
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *window = dynamic_cast<TEditWindow *>(view);
        if (window && !window->editor->modified && !window->editor->fileName[0])
            unused.push_back(window);
    }
    for (auto *window : unused) {
        deskTop->remove(window);
        TObject::destroy(window);
    }
}

bool TurboIDEApp::saveDesktopSession() {
    if (!hasProject_ || project_.file.empty()) return false;
    DesktopSession session;
    if (projectWindow_) {
        session.hasProjectBounds = true;
        session.projectBounds = saveRect(projectWindow_->getBounds());
    }
    session.hasHelpBounds = hasHelpBounds_;
    session.helpBounds = helpBounds_;
    const auto root = project_.file.parent_path();
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *window = dynamic_cast<TEditWindow *>(view);
        if (!window || !window->editor->fileName[0]) continue;
        const auto file = std::filesystem::absolute(
            std::filesystem::u8path(window->editor->fileName)).lexically_normal();
        if (!isProjectFile(file, root) || _wcsicmp(file.extension().c_str(), L".prj") == 0)
            continue;
        EditorSession editor;
        editor.file = file;
        editor.bounds = saveRect(window->getBounds());
        uint position = 0;
        while (position < window->editor->curPtr && position < window->editor->bufLen) {
            position = window->editor->nextLine(position);
            ++editor.line;
        }
        editor.column = window->editor->charPos(window->editor->lineStart(window->editor->curPtr),
                                                 window->editor->curPtr) + 1;
        session.editors.push_back(std::move(editor));
        if (window == deskTop->current) session.activeFile = file;
    }
    for (const auto &entry : breakpoints_)
        for (int line : entry.second)
            session.breakpoints.emplace_back(std::filesystem::path(entry.first), line);
    session.watches = watchExpressions_;
    return ::saveDesktopSession(project_.file, session);
}

void TurboIDEApp::restoreDesktopSession() {
    if (!hasProject_) return;
    hasHelpBounds_ = false;
    DesktopSession session;
    if (!loadDesktopSession(project_.file, session)) return;
    for (const auto &breakpoint : session.breakpoints)
        breakpoints_[normalizedPathKey(std::filesystem::absolute(breakpoint.first).lexically_normal())]
            .insert(breakpoint.second);
    watchExpressions_ = std::move(session.watches);
    const TRect extent = deskTop->getExtent();
    hasHelpBounds_ = session.hasHelpBounds;
    helpBounds_ = session.helpBounds;
    if (projectWindow_ && session.hasProjectBounds) {
        auto bounds = restoreRect(session.projectBounds, extent);
        projectWindow_->locate(bounds);
    }

    TEditWindow *active = nullptr;
    for (const auto &saved : session.editors) {
        const auto file = std::filesystem::absolute(saved.file).lexically_normal();
        if (!std::filesystem::is_regular_file(file)) continue;
        TEditWindow *window = nullptr;
        for (TView *view = deskTop->first(); view; view = view->nextView()) {
            auto *candidate = dynamic_cast<TEditWindow *>(view);
            if (!candidate || !candidate->editor->fileName[0]) continue;
            const auto current = std::filesystem::absolute(
                std::filesystem::u8path(candidate->editor->fileName)).lexically_normal();
            if (_wcsicmp(current.c_str(), file.c_str()) == 0) {
                window = candidate;
                break;
            }
        }
        const auto bounds = restoreRect(saved.bounds, extent);
        if (!window) {
            const auto text = file.u8string();
            window = openEditor(text.c_str(), &bounds);
        } else {
            auto located = bounds;
            window->locate(located);
        }
        if (!window) continue;
        uint position = 0;
        for (int line = 1; line < saved.line && position < window->editor->bufLen; ++line)
            position = window->editor->nextLine(position);
        position = window->editor->lineStart(position);
        for (int column = 1; column < saved.column && position < window->editor->lineEnd(position); ++column)
            position = window->editor->nextChar(position);
        moveEditorCursor(window->editor, position);
        if (_wcsicmp(file.c_str(), session.activeFile.c_str()) == 0) active = window;
    }
    if (active) {
        active->putInFrontOf(deskTop->first());
        deskTop->setCurrent(active, TGroup::enterSelect);
        active->editor->trackCursor(True);
    }
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *editor = dynamic_cast<TEditWindow *>(view);
        if (!editor || !editor->editor->fileName[0]) continue;
        const auto path = normalizedPathKey(std::filesystem::u8path(editor->editor->fileName));
        std::vector<int> lines;
        const auto found = breakpoints_.find(path);
        if (found != breakpoints_.end()) lines.assign(found->second.begin(), found->second.end());
        setEditorDebugState(editor->editor, lines, 0);
    }
}

void TurboIDEApp::showHelpContents(std::string_view word) {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        messageBox("Could not locate the Help database.", mfError | mfOKButton);
        return;
    }
    const auto file = std::filesystem::path(executable).parent_path() / L"help" / L"tchelp.h32";
    DesktopRect restoredBounds;
    const DesktopRect* initialBounds = nullptr;
    if (hasHelpBounds_) {
        restoredBounds = saveRect(restoreRect(helpBounds_, deskTop->getExtent()));
        initialBounds = &restoredBounds;
    }
    DesktopRect finalBounds;
    const auto error = word.empty() ?
        openHelpDatabase(*this, file, 10030, initialBounds, &finalBounds) :
        openHelpDatabaseForWord(*this, file, word, 10031, initialBounds, &finalBounds);
    if (error) {
        messageBox(error->c_str(), mfError | mfOKButton);
        return;
    }
    hasHelpBounds_ = true;
    helpBounds_ = finalBounds;
    saveDesktopSession();
}

ushort TurboIDEApp::activeHelpContext() const {
    if (menuBar && menuBar->getState(sfActive))
        return menuBar->getHelpCtx();
    return deskTop ? deskTop->getHelpCtx() : hcNoContext;
}

void TurboIDEApp::showHelpContext(ushort context) {
    constexpr int contentsContext = 10030;
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        messageBox("Could not locate the Help database.", mfError | mfOKButton);
        return;
    }
    const auto file = std::filesystem::path(executable).parent_path() / L"help" / L"tchelp.h32";
    DesktopRect restoredBounds;
    const DesktopRect* initialBounds = nullptr;
    if (hasHelpBounds_) {
        restoredBounds = saveRect(restoreRect(helpBounds_, deskTop->getExtent()));
        initialBounds = &restoredBounds;
    }
    DesktopRect finalBounds;
    const int topic = context == hcNoContext ? contentsContext : context;
    auto error = openHelpDatabase(*this, file, topic, initialBounds, &finalBounds);
    if (error && topic != contentsContext)
        error = openHelpDatabase(*this, file, contentsContext, initialBounds, &finalBounds);
    if (error) {
        messageBox(error->c_str(), mfError | mfOKButton);
        return;
    }
    hasHelpBounds_ = true;
    helpBounds_ = finalBounds;
    saveDesktopSession();
}

void TurboIDEApp::rememberProject() {
    settings_.lastProject = hasProject_ ? project_.file : std::filesystem::path{};
}

void TurboIDEApp::showProjectWindow() {
    if (!hasProject_) return;
    if (!projectWindow_) {
        const TRect area = deskTop->getExtent();
        const short width = std::min<short>(36, area.b.x - area.a.x);
        const short height = std::min<short>(12, area.b.y - area.a.y);
        projectWindow_ = new ProjectFilesWindow(
            TRect(area.a.x + 1, area.a.y + 1, area.a.x + width, area.a.y + height),
            this, &project_, nextWindowNumber());
        deskTop->insert(projectWindow_);
    }
    projectWindow_->show();
    if (deskTop->first() != projectWindow_) projectWindow_->putInFrontOf(deskTop->first());
    deskTop->setCurrent(projectWindow_, TGroup::enterSelect);
    projectWindow_->selectFirst();
}

void TurboIDEApp::projectWindowClosed(ProjectFilesWindow *window) {
    if (projectWindow_ == window) projectWindow_ = nullptr;
}

void TurboIDEApp::showWindowList() {
    std::vector<TView *> windows;
    std::vector<std::string> titles;
    short selection = 0;
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *window = dynamic_cast<TWindow *>(view);
        if (!window) continue;
        if (window == deskTop->current)
            selection = static_cast<short>(windows.size());
        windows.push_back(window);
        std::string title = window->getTitle(40);
        if (window->number > 0)
            title += " (" + std::to_string(window->number) + ")";
        titles.push_back(std::move(title));
    }
    if (windows.empty()) return;
    if (execDialog(new WindowListDialog(&titles, &selection)) != cmOK ||
        selection < 0 || static_cast<size_t>(selection) >= windows.size())
        return;
    TView *target = windows[static_cast<size_t>(selection)];
    target->show();
    target->putInFrontOf(deskTop->first());
    deskTop->setCurrent(target, TGroup::enterSelect);
    if (target == projectWindow_)
        projectWindow_->selectFirst();
}

void TurboIDEApp::closeProject() {
    if (!hasProject_) return;
    if (hasProject_ && !saveDesktopSession())
        messageBox("Could not save the project desktop file.", mfError | mfOKButton);
    if (debugger_) stopDebuggee();
    if (projectWindow_) projectWindow_->close();
    project_ = {};
    hasProject_ = false;
    breakpoints_.clear();
    watchExpressions_.clear();
    watchVariables_.clear();
    debugVariables_.clear();
    for (TView *view = deskTop->first(); view; view = view->nextView())
        if (auto *editor = dynamic_cast<TEditWindow *>(view))
            setEditorDebugState(editor->editor, {}, 0);
    rememberProject();
    saveSettings(settings_);
}

void TurboIDEApp::openProjectItem() {
    if (!projectWindow_) return;
    const short selected = projectWindow_->selected();
    if (selected < 0 || static_cast<size_t>(selected) >= project_.sources.size()) return;
    const auto file = project_.sources[static_cast<size_t>(selected)].u8string();
    openEditor(file.c_str());
}

void TurboIDEApp::addProjectItem() {
    if (!hasProject_) return;
    char fileName[MAXPATH] = "*.*";
    if (execDialog(new ContextHelpFileDialog("*.*", "Add item to project", "~N~ame", fdOpenButton, 118,
                                             hcProjectMenu), fileName) == cmCancel)
        return;
    Project updated = project_;
    const auto path = std::filesystem::absolute(std::filesystem::u8path(fileName)).lexically_normal();
    const auto duplicate = std::any_of(updated.sources.begin(), updated.sources.end(), [&](const auto &item) {
        return _wcsicmp(item.c_str(), path.c_str()) == 0;
    });
    if (!duplicate) updated.sources.push_back(path);
    std::string error;
    if (!saveProject(updated, error)) { messageBox(error.c_str(), mfError | mfOKButton); return; }
    project_ = std::move(updated);
    if (projectWindow_) { projectWindow_->close(); projectWindow_ = nullptr; }
    showProjectWindow();
}

void TurboIDEApp::deleteProjectItem() {
    if (!projectWindow_) return;
    const short selected = projectWindow_->selected();
    if (selected < 0 || static_cast<size_t>(selected) >= project_.sources.size()) return;
    if (messageBox("Remove selected file from project?", mfYesNoCancel | mfConfirmation) != cmYes) return;
    Project updated = project_;
    updated.sources.erase(updated.sources.begin() + selected);
    std::string error;
    if (!saveProject(updated, error)) { messageBox(error.c_str(), mfError | mfOKButton); return; }
    project_ = std::move(updated);
    projectWindow_->close();
    showProjectWindow();
}

bool TurboIDEApp::saveBuildInputs(const BuildRequest &request) {
    std::vector<std::filesystem::path> targets = request.sources;
    if (hasProject_)
        targets.push_back(project_.file);
    std::vector<TFileEditor *> dirty;

    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *window = dynamic_cast<TEditWindow *>(view);
        if (!window || !window->editor->modified || !window->editor->fileName[0])
            continue;
        const auto file = std::filesystem::absolute(
            std::filesystem::u8path(window->editor->fileName)).lexically_normal();
        const bool needed = std::any_of(targets.begin(), targets.end(), [&](const auto &target) {
            return _wcsicmp(file.c_str(), std::filesystem::absolute(target).lexically_normal().c_str()) == 0;
        });
        if (needed)
            dirty.push_back(window->editor);
    }

    if (dirty.empty())
        return true;
    const ushort answer = messageBox("Save modified project/source files before building?",
                                     mfInformation | mfYesNoCancel);
    if (answer == cmCancel)
        return false;
    if (answer == cmNo)
        return true;
    bool saved = false;
    for (auto *editor : dirty) {
        if (!editor->save())
            return false;
        saved = true;
    }
    if (saved) saveDesktopSession();
    return true;
}

void TurboIDEApp::compileCurrent(bool runAfterBuild, bool debugAfterBuild) {
    auto *window = dynamic_cast<TEditWindow *>(deskTop->current);
    if (!window)
        for (TView *view = deskTop->first(); view; view = view->nextView())
            if ((window = dynamic_cast<TEditWindow *>(view)) && window->editor->fileName[0])
                break;
    if (!window || !window->editor->fileName[0]) {
        messageBox("Save the current C or C++ source file before compiling.", mfError | mfOKButton);
        return;
    }
    const auto source = std::filesystem::absolute(
        std::filesystem::u8path(window->editor->fileName)).lexically_normal();
    const auto extension = source.extension().wstring();
    if (_wcsicmp(extension.c_str(), L".c") != 0 &&
        _wcsicmp(extension.c_str(), L".cc") != 0 &&
        _wcsicmp(extension.c_str(), L".cpp") != 0 &&
        _wcsicmp(extension.c_str(), L".cxx") != 0) {
        messageBox("The current file is not a C or C++ source file.", mfError | mfOKButton);
        return;
    }
    BuildRequest request;
    request.workingDirectory = source.parent_path();
    request.sources.push_back(source);
    if (!saveBuildInputs(request))
        return;
    const auto runDirectory = standaloneRunDirectory_.empty()
        ? source.parent_path() : standaloneRunDirectory_;
    startBuild(std::move(request), runAfterBuild, standaloneArguments_, runDirectory,
               debugAfterBuild);
}

void TurboIDEApp::buildProject(bool runAfterBuild, bool debugAfterBuild) {
    if (!hasProject_) {
        compileCurrent(runAfterBuild, debugAfterBuild);
        return;
    }

    BuildRequest saveProject;
    saveProject.workingDirectory = project_.file.parent_path();
    if (!saveBuildInputs(saveProject))
        return;

    Project loaded;
    std::string error;
    if (!loadProject(project_.file, loaded, error)) {
        messageBox(error.c_str(), mfError | mfOKButton);
        return;
    }
    project_ = std::move(loaded);

    BuildRequest request;
    request.workingDirectory = project_.file.parent_path();
    for (const auto &item : project_.sources) {
        const auto extension = item.extension().wstring();
        if (_wcsicmp(extension.c_str(), L".c") == 0 ||
            _wcsicmp(extension.c_str(), L".cc") == 0 ||
            _wcsicmp(extension.c_str(), L".cpp") == 0 ||
            _wcsicmp(extension.c_str(), L".cxx") == 0)
            request.sources.push_back(item);
    }
    if (request.sources.empty()) {
        messageBox("Project has no compilable source files.", mfError | mfOKButton);
        return;
    }
    request.includeDirs = project_.includeDirs;
    request.includeDirs.push_back(project_.file.parent_path());
    request.defines = project_.defines;
    request.libraries = project_.libraries;
    if (!saveBuildInputs(request))
        return;
    const auto runDirectory = project_.runDirectory.empty()
        ? request.workingDirectory : project_.runDirectory;
    startBuild(std::move(request), runAfterBuild, project_.arguments, runDirectory,
               debugAfterBuild);
}

void TurboIDEApp::debugProject() {
    if (debugger_ && debugger_->active()) {
        messageBox("Stop the active debugging session before starting another.",
                   mfInformation | mfOKButton);
        return;
    }
    buildProject(false, true);
}

void TurboIDEApp::editRunParameters() {
    const auto &current = hasProject_ ? project_.arguments : standaloneArguments_;
    char text[256]{};
    const auto formatted = formatArguments(current);
    std::snprintf(text, sizeof(text), "%s", formatted.c_str());
    if (execDialog(createSingleInputDialog("Run parameters", "~P~arameters", 255,
                                           hcRunParametersDialog), text) != cmOK)
        return;
    const auto parsed = parseArguments(text);
    if (hasProject_) {
        Project updated = project_;
        updated.arguments = parsed;
        std::string error;
        if (!saveProject(updated, error)) {
            messageBox(error.c_str(), mfError | mfOKButton);
            return;
        }
        project_ = std::move(updated);
    } else {
        standaloneArguments_ = parsed;
    }
}

void TurboIDEApp::editRunDirectory() {
    const auto current = hasProject_ ? project_.runDirectory : standaloneRunDirectory_;
    char text[MAXPATH]{};
    const auto display = current.empty() ? std::string{} : current.u8string();
    std::snprintf(text, sizeof(text), "%s", display.c_str());
    if (execDialog(createSingleInputDialog("Run directory", "~D~irectory (empty = source/project directory)",
                                           MAXPATH - 1, hcRunMenu), text) != cmOK)
        return;

    std::filesystem::path directory;
    if (text[0]) {
        directory = std::filesystem::absolute(std::filesystem::u8path(text)).lexically_normal();
        if (!std::filesystem::is_directory(directory)) {
            messageBox("The run directory does not exist.", mfError | mfOKButton);
            return;
        }
    }
    if (hasProject_) {
        Project updated = project_;
        updated.runDirectory = directory;
        std::string error;
        if (!saveProject(updated, error)) {
            messageBox(error.c_str(), mfError | mfOKButton);
            return;
        }
        project_ = std::move(updated);
    } else {
        standaloneRunDirectory_ = directory;
    }
}

void TurboIDEApp::changeDirectory() {
    if (hasProject_ && !saveDesktopSession())
        messageBox("Could not save the project desktop file.", mfError | mfOKButton);
    auto *dialog = new TChDirDialog(cdNormal, 208);
    if (deskTop->size.y > 26)
        dialog->growTo(dialog->size.x, deskTop->size.y - 6);
    if (deskTop->size.x > 60)
        dialog->growTo(std::min<short>(102, deskTop->size.x - (60 - dialog->size.x)),
                       dialog->size.y);
    if (execDialog(dialog) != cmOK)
        return;
    std::error_code error;
    settings_.currentDirectory = std::filesystem::current_path(error);
    if (error) {
        messageBox("Cannot determine the current directory.", mfError | mfOKButton);
        return;
    }
    saveSettings(settings_);
}

void TurboIDEApp::editEnvironment() {
    TInputLine *tabs = nullptr;
    TInputLine *extensionInput = nullptr;
    TCheckBoxes *options = nullptr;
    TDialog *dialog = createEditorDialog(tabs, extensionInput, options);
    char tabValue[4]{};
    char extensionValue[16]{};
    ushort optionsValue = (settings_.backupFiles ? 1 : 0) |
        (settings_.persistentBlocks ? 1 << 8 : 0);
    std::snprintf(tabValue, sizeof(tabValue), "%d", settings_.tabSize);
    std::snprintf(extensionValue, sizeof(extensionValue), "%s", settings_.defaultExtension.c_str());
    tabs->setData(tabValue);
    extensionInput->setData(extensionValue);
    options->setData(&optionsValue);
    TView *validDialog = TProgram::application->validView(dialog);
    if (!validDialog)
        return;
    const ushort result = TProgram::deskTop->execView(validDialog);
    if (result != cmCancel) {
        tabs->getData(tabValue);
        extensionInput->getData(extensionValue);
        options->getData(&optionsValue);
    }
    TObject::destroy(validDialog);
    if (result == cmCancel)
        return;

    int tabSize = 0;
    try { tabSize = std::stoi(tabValue); } catch (...) {}
    if (tabSize < 1 || tabSize > 32) {
        messageBox("Tab size must be between 1 and 32.", mfError | mfOKButton);
        return;
    }
    std::string extension = extensionValue;
    if (extension.empty() || extension.size() > 15 ||
        extension.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        messageBox("Enter a valid file extension, for example .c.", mfError | mfOKButton);
        return;
    }
    if (extension[0] != '.') extension.insert(extension.begin(), '.');
    settings_.tabSize = tabSize;
    settings_.backupFiles = (optionsValue & 1) != 0;
    settings_.persistentBlocks = (optionsValue & (1 << 8)) != 0;
    settings_.defaultExtension = std::move(extension);
    if (settings_.backupFiles)
        TEditor::editorFlags |= efBackupFiles;
    else
        TEditor::editorFlags &= ~efBackupFiles;
    setDefaultPersistentBlocks(settings_.persistentBlocks);
    TEditor::tabSize = tabSize;
    saveSettings(settings_);
    for (TView *view = deskTop->first(); view; view = view->nextView())
        if (auto *window = dynamic_cast<TEditWindow *>(view))
            setEditorPersistentBlocks(window->editor, settings_.persistentBlocks);
}

void TurboIDEApp::editCompiler() {
    char value[MAX_PATH]{};
    const auto configured = settings_.compilerPath.u8string();
    std::snprintf(value, sizeof(value), "%s", configured.c_str());
    ushort result = cmCancel;
    do {
        TInputLine *path = nullptr;
        TDialog *dialog = createCompilerDialog(path);
        path->setData(value);
        TView *view = TProgram::application->validView(dialog);
        if (!view)
            return;
        result = TProgram::deskTop->execView(view);
        if (result != cmCancel)
            path->getData(value);
        TObject::destroy(view);
        if (result == cmCompilerBrowse) {
            std::error_code directoryError;
            const auto originalDirectory = std::filesystem::current_path(directoryError);
            std::filesystem::path compiler;
            const ushort directoryResult = execDialog(new ToolchainDirectoryDialog(compiler));
            if (!originalDirectory.empty())
                std::filesystem::current_path(originalDirectory, directoryError);
            if (directoryResult == cmOK && !directoryError)
                std::snprintf(value, sizeof(value), "%s", compiler.u8string().c_str());
        }
    } while (result == cmCompilerBrowse);
    if (result == cmCancel)
        return;

    std::filesystem::path compiler = value[0] ? std::filesystem::u8path(value) :
                                                std::filesystem::path{};
    std::error_code error;
    if (!compiler.empty()) {
        compiler = std::filesystem::absolute(compiler, error).lexically_normal();
        if (!error && std::filesystem::is_directory(compiler, error))
            compiler /= L"gcc.exe";
    }
    if (error || (!compiler.empty() && !std::filesystem::is_regular_file(compiler, error))) {
        messageBox("Select a GCC toolchain folder, an existing gcc.exe, or leave the path empty.",
                   mfError | mfOKButton);
        return;
    }
    if (!compiler.empty()) {
        std::string missing;
        for (const wchar_t *tool : {L"gcc.exe", L"g++.exe", L"gdb.exe"})
            if (!std::filesystem::is_regular_file(compiler.parent_path() / tool, error)) {
                if (!missing.empty()) missing += ", ";
                missing += std::filesystem::path(tool).string();
            }
        if (!missing.empty()) {
            const std::string message = "The selected toolchain folder is missing: " + missing + ".";
            messageBox(message.c_str(), mfError | mfOKButton);
            return;
        }
    }
    settings_.compilerType = "gcc";
    settings_.compilerPath = compiler;
    saveSettings(settings_);
}

void TurboIDEApp::editColors() {
    TColorGroup &groups =
        *new TColorGroup("Desktop") +
            *new TColorItem("Color", 1) +
        *new TColorGroup("Menus") +
            *new TColorItem("Normal", 2) +
            *new TColorItem("Disabled", 3) +
            *new TColorItem("Shortcut", 4) +
            *new TColorItem("Selected", 5) +
            *new TColorItem("Selected disabled", 6) +
            *new TColorItem("Shortcut selected", 7) +
        *new TColorGroup("Windows") +
            *new TColorItem("Frame passive", 8) +
            *new TColorItem("Frame active", 9) +
            *new TColorItem("Frame icons", 10) +
            *new TColorItem("Scroll bar page", 11) +
            *new TColorItem("Scroll bar icons", 12) +
            *new TColorItem("Text", 13) +
        *new TColorGroup("Dialogs") +
            *new TColorItem("Frame/background", 33) +
            *new TColorItem("Frame icons", 34) +
            *new TColorItem("Static text", 37) +
            *new TColorItem("Button normal", 41) +
            *new TColorItem("Button selected", 43) +
            *new TColorItem("Input normal", 50) +
            *new TColorItem("Input selected", 51) +
            *new TColorItem("List normal", 57) +
            *new TColorItem("List selected", 59);
    auto *dialog = new ContextHelpColorDialog(&ideTheme().application, &groups,
                                               hcEnvironmentColors);
    TView *validDialog = TProgram::application->validView(dialog);
    if (!validDialog)
        return;
    const ushort result = deskTop->execView(validDialog);
    if (result == cmOK) {
        ideTheme().application = *dialog->pal;
        setScreenMode(TScreen::screenMode);
    }
    TObject::destroy(validDialog);
}

void TurboIDEApp::startBuild(BuildRequest request, bool runAfterBuild,
                            std::vector<std::wstring> runArguments,
                            std::filesystem::path runDirectory,
                            bool debugAfterBuild) {
    if (buildRunning_) {
        messageBox("A build is already running.", mfInformation | mfOKButton);
        return;
    }
    if (buildThread_.joinable())
        buildThread_.join();
    request.compilerPath = settings_.compilerPath;
    buildRunning_ = true;
    buildReturnView_ = deskTop->current;
    buildCancelRequested.store(false);
    buildControlHandlerActive.store(true);
    runAfterBuild_ = runAfterBuild;
    debugAfterBuild_ = debugAfterBuild;
    runArguments_ = std::move(runArguments);
    runWorkingDirectory_ = runDirectory.empty() ? request.workingDirectory : std::move(runDirectory);
    runReturnFile_.clear();
    if ((runAfterBuild || debugAfterBuild) && !request.sources.empty())
        runReturnFile_ = std::filesystem::absolute(request.sources.front()).lexically_normal();
    clearMessages();
    messages_.push_back({"Building with GCC...", {}, 0, 0, false});
    messageIndex_ = messages_.size();
    if (messagesWindow_)
        messagesWindow_->updateMessages();
    else {
        const TRect extent = deskTop->getExtent();
        const short top = std::max<short>(1, extent.b.y - 7);
        messagesWindow_ = new BuildMessagesWindow(
            TRect(extent.a.x, top, extent.b.x, extent.b.y), this, &messages_,
            nextWindowNumber());
        deskTop->insert(messagesWindow_);
    }
    messagesWindow_->hide();
    const TRect extent = deskTop->getExtent();
    constexpr short popupWidth = 60;
    constexpr short popupHeight = 14;
    const short left = std::max<short>(0, (extent.b.x - popupWidth) / 2);
    const short top = std::max<short>(0, (extent.b.y - popupHeight) / 2);
    buildPopupSuccess_ = false;
    blinkSuccess_ = false;
    buildWarnings_ = 0;
    buildErrors_ = 0;
    lastBlink_ = std::chrono::steady_clock::now();
    const std::string mainFile = request.sources.empty() ? "<none>" :
        request.sources.front().filename().u8string();
    size_t totalLines = 0;
    size_t mainFileLines = 0;
    for (size_t i = 0; i < request.sources.size(); ++i) {
        const size_t sourceLines = countLines(request.sources[i]);
        totalLines += sourceLines;
        if (i == 0)
            mainFileLines = sourceLines;
    }
    buildPopup_ = new BuildProgressWindow(
        TRect(left, top, left + popupWidth, top + popupHeight), mainFile,
        totalLines, mainFileLines);
    deskTop->insert(buildPopup_);
    deskTop->setCurrent(buildPopup_, TGroup::enterSelect);

    try {
        buildThread_ = std::thread([this, request = std::move(request)]() mutable {
            BuildResult result;
            try {
                result = runBuild(request, buildCancelRequested);
            } catch (const std::exception &exception) {
                result.error = exception.what();
            } catch (...) {
                result.error = "Unexpected error while building.";
            }
            buildControlHandlerActive.store(false);
            std::lock_guard<std::mutex> lock(buildMutex_);
            finishedBuild_ = std::move(result);
        });
    } catch (const std::exception &exception) {
        buildRunning_ = false;
        buildControlHandlerActive.store(false);
        BuildResult result;
        result.error = exception.what();
        showBuildResult(std::move(result));
    }
}

void TurboIDEApp::showBuildResult(BuildResult result) {
    const bool succeeded = result.succeeded;
    const auto executable = result.executable;
    messages_ = std::move(result.messages);
    if (!result.error.empty())
        messages_.push_back({result.error, {}, 0, 0, false, {}, BuildMessageKind::error});
    if (messages_.empty())
        messages_.push_back({result.succeeded ? "Build succeeded." : "Build failed.", {}, 0, 0, false});
    if (result.succeeded)
        messages_.push_back({"Build succeeded: " + result.executable.u8string(), {}, 0, 0, false});
    else if (result.started)
        messages_.push_back({"Build failed (exit code " + std::to_string(result.exitCode) + ").", {}, 0, 0, false});

    messageIndex_ = messages_.size();
    if (!succeeded) {
        const auto firstError = std::find_if(messages_.begin(), messages_.end(), [](const BuildMessage &message) {
            return message.kind == BuildMessageKind::error || message.kind == BuildMessageKind::fatal;
        });
        const auto firstLocated = std::find_if(messages_.begin(), messages_.end(), [](const BuildMessage &message) {
            return message.hasLocation;
        });
        const auto selected = firstError != messages_.end() ? firstError : firstLocated;
        if (selected != messages_.end())
            messageIndex_ = static_cast<size_t>(std::distance(messages_.begin(), selected));
    }
    setMessageStatus(nullptr);

    if (messagesWindow_)
        messagesWindow_->updateMessages();
    else {
        const TRect extent = deskTop->getExtent();
        const short top = std::max<short>(1, extent.b.y - 7);
        messagesWindow_ = new BuildMessagesWindow(
            TRect(extent.a.x, top, extent.b.x, extent.b.y), this, &messages_,
            nextWindowNumber());
        deskTop->insert(messagesWindow_);
    }
    const bool runAfterBuild = runAfterBuild_;
    const bool debugAfterBuild = debugAfterBuild_;
    runAfterBuild_ = false;
    debugAfterBuild_ = false;
    if (buildPopup_) {
        if (succeeded && !runAfterBuild && !debugAfterBuild) {
            buildPopupSuccess_ = true;
            blinkSuccess_ = true;
            unsigned warnings = 0, errors = 0;
            for (const auto &message : messages_) {
                warnings += message.text.find("warning:") != std::string::npos;
                errors += message.text.find("error:") != std::string::npos;
            }
            buildWarnings_ = warnings;
            buildErrors_ = errors;
            std::error_code sizeError;
            const auto programBytes = std::filesystem::file_size(executable, sizeError);
            buildPopup_->setSuccess(true, blinkSuccess_, buildWarnings_, buildErrors_,
                                    sizeError ? 0 : programBytes);
            lastBlink_ = std::chrono::steady_clock::now();
            deskTop->setCurrent(buildPopup_, TGroup::enterSelect);
        } else {
            dismissBuildPopup();
        }
    }
    if (succeeded)
        messagesWindow_->hide();
    if (runAfterBuild && succeeded) {
        runBuiltProgram(executable);
        restoreEditorAfterRun();
    } else if (debugAfterBuild && succeeded) {
        startDebuggee(executable);
    } else if (!succeeded) {
        showMessages();
    }
}

void TurboIDEApp::runBuiltProgram(const std::filesystem::path &executable) {
    if (userScreenBuffer_ == INVALID_HANDLE_VALUE)
        userScreenBuffer_ = createUserScreenBuffer(ideScreenBuffer_);
    RunResult result = runProgram({executable, runWorkingDirectory_, runArguments_},
                                 userScreenBuffer_, ideScreenBuffer_, consoleInput_);
    if (!result.error.empty())
        messages_.push_back({result.error, {}, 0, 0, false});
    else if (result.started)
        messages_.push_back({"Program exited with code " + std::to_string(result.exitCode) +
                             ". Press Alt+F5 to view its screen.", {}, 0, 0, false});
    if (messagesWindow_)
        messagesWindow_->updateMessages();
}

TEditWindow *TurboIDEApp::currentEditorWindow() const {
    return dynamic_cast<TEditWindow *>(deskTop->current);
}

void TurboIDEApp::syncEditMenuState() {
    const auto *window = currentEditorWindow();
    const bool hasEditor = window && window->editor;
    if (editMenuCommandsDisabled_ == hasEditor) {
        editMenuCommandsDisabled_ = !hasEditor;
        const ushort editCommands[] = {
            cmUndo, cmRedo, cmCut, cmCopy, cmPaste, cmPageDown, cmCharRight, cmLineUp,
            cmWordRight, cmSearchAgain, cmPageUp, cmCharLeft, cmLineDown,
            cmWordLeft, cmLineStart, cmLineEnd, cmTextStart, cmTextEnd,
            cmDelChar, cmBackSpace, cmDelWord, cmDelWordLeft, cmDelLine,
            cmDelStart, cmDelEnd, cmClear, cmNewLine, cmInsMode, cmIndentMode,
            cmSelectAll, cmStartSelect
        };
        for (ushort command : editCommands) {
            if (hasEditor) enableCommand(command);
            else disableCommand(command);
        }
    }
    const ushort featureCommands[] = {
        cmExpandPmacro, cmMatchBracket, cmRecordMacro, cmStopMacro, cmPlayMacro,
        cmMenuBlockStart,
        cmChoosePmacro, cmMenuBlockEnd, cmMenuSelectLine, cmMenuSelectWord,
        cmMenuReplaceSelect, cmMenuHideBlock, cmMenuCopyBlock,
        cmMenuInvertCase, cmMenuAlternateCase,
        cmMenuIndentBlock, cmMenuUnindentBlock, cmMenuUpperCase, cmMenuLowerCase,
        cmMenuReadBlock, cmMenuMoveBlock, cmMenuWriteBlock, cmMenuRectStart,
        cmMenuRectEnd, cmMenuRectCopy, cmMenuRectDelete,
        cmMenuRectClear, cmMenuRectHide, cmMenuRectMove, cmMenuRectPaste, cmMenuRectCut,
        cmMenuRectToggleMovePaste, cmMenuRectDuplicate
    };
    const bool supportsSyntaxCommands = hasEditor && editorSupportsPrefixKeys(window->editor);
    if (hasEditor) {
        enableCommand(cmGoToLine);
        enableCommand(cmMenuInsertTab);
    } else {
        disableCommand(cmGoToLine);
        disableCommand(cmMenuInsertTab);
    }
    for (ushort command : featureCommands) {
        if (supportsSyntaxCommands) enableCommand(command);
        else disableCommand(command);
    }
    const bool persistentBlocks = supportsSyntaxCommands && editorPersistentBlocks(window->editor);
    const bool hasSelection = hasEditor && window->editor->hasSelection();
    if (persistentBlocks && hasSelection) {
        enableCommand(cmMenuCopyBlock);
        enableCommand(cmMenuMoveBlock);
    } else {
        disableCommand(cmMenuCopyBlock);
        disableCommand(cmMenuMoveBlock);
    }
    const ushort selectionCommands[] = {
        cmMenuReplaceSelect, cmMenuHideBlock, cmMenuIndentBlock, cmMenuUnindentBlock,
        cmMenuUpperCase, cmMenuLowerCase, cmMenuInvertCase, cmMenuAlternateCase,
        cmMenuWriteBlock
    };
    for (ushort command : selectionCommands) {
        if (supportsSyntaxCommands && hasSelection) enableCommand(command);
        else disableCommand(command);
    }
}

void TurboIDEApp::goToLine() {
    auto *window = currentEditorWindow();
    if (!window || !window->editor) {
        messageBox("Open a source file first.", mfInformation | mfOKButton);
        return;
    }
    char input[16] = {};
    if (execDialog(createSingleInputDialog("Go to line", "~L~ine number", 10,
                                           hcGoToLineDialog), input) != cmOK)
        return;
    char *end = nullptr;
    const long requested = std::strtol(input, &end, 10);
    if (end == input || *end || requested < 1) {
        messageBox("Enter a positive line number.", mfError | mfOKButton);
        return;
    }
    TFileEditor *editor = window->editor;
    uint position = 0;
    for (long line = 1; line < requested && position < editor->bufLen; ++line)
        position = editor->nextLine(position);
    moveEditorCursor(editor, editor->lineStart(position));
    editor->trackCursor(True);
}

std::vector<DebugBreakpoint> TurboIDEApp::allBreakpoints() const {
    std::vector<DebugBreakpoint> result;
    for (const auto &entry : breakpoints_)
        for (int line : entry.second)
            result.push_back({std::filesystem::path(entry.first), line});
    return result;
}

void TurboIDEApp::toggleBreakpoint() {
    TEditWindow *window = currentEditorWindow();
    if (!window || !window->editor->fileName[0]) {
        messageBox("Open a source file and place the cursor on a line first.",
                   mfInformation | mfOKButton);
        return;
    }
    const auto file = std::filesystem::absolute(
        std::filesystem::u8path(window->editor->fileName)).lexically_normal();
    auto &lines = breakpoints_[normalizedPathKey(file)];
    const int line = editorCurrentLine(window->editor);
    const bool adding = lines.insert(line).second;
    if (!adding) lines.erase(line);
    if (debugger_ && debugger_->active()) {
        if (debugger_->running()) {
            if (adding) lines.erase(line); else lines.insert(line);
            messageBox("Pause at a breakpoint before changing breakpoints.",
                       mfInformation | mfOKButton);
            return;
        }
        std::string error;
        if (!debugger_->setBreakpoints(allBreakpoints(), error)) {
            if (adding) lines.erase(line); else lines.insert(line);
            messageBox(error.c_str(), mfError | mfOKButton);
            return;
        }
    }
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *editor = dynamic_cast<TEditWindow *>(view);
        if (!editor || !editor->editor->fileName[0]) continue;
        const auto editorPath = normalizedPathKey(
            std::filesystem::u8path(editor->editor->fileName));
        std::vector<int> editorBreakpoints;
        const auto found = breakpoints_.find(editorPath);
        if (found != breakpoints_.end())
            editorBreakpoints.assign(found->second.begin(), found->second.end());
        int executionLine = 0;
        if (debugger_ && !currentDebugStop_.file.empty() &&
            _wcsicmp(editorPath.c_str(), normalizedPathKey(currentDebugStop_.file).c_str()) == 0)
            executionLine = currentDebugStop_.line;
        setEditorDebugState(editor->editor, editorBreakpoints, executionLine);
    }
    if (hasProject_ && !saveDesktopSession())
        messageBox("Could not save the project desktop file.", mfError | mfOKButton);
}

void TurboIDEApp::startDebuggee(const std::filesystem::path &executable) {
    if (userScreenBuffer_ == INVALID_HANDLE_VALUE)
        userScreenBuffer_ = createUserScreenBuffer(ideScreenBuffer_);
    if (userScreenBuffer_ == INVALID_HANDLE_VALUE) {
        messageBox("Cannot create the user screen buffer.", mfError | mfOKButton);
        return;
    }
    clearMessages();
    messages_.push_back({"Starting GDB...", {}, 0, 0, false});
    messageIndex_ = messages_.size();
    if (messagesWindow_) messagesWindow_->updateMessages();
    auto session = std::make_unique<GdbSession>();
    DebugStop firstStop;
    std::string error;
    const auto gdbPath = settings_.compilerPath.empty() ? std::filesystem::path{} :
                         settings_.compilerPath.parent_path() / L"gdb.exe";
    if (!session->start(executable, runWorkingDirectory_, gdbPath, runArguments_, allBreakpoints(),
                        userScreenBuffer_, consoleInput_, firstStop, error)) {
        SetConsoleActiveScreenBuffer(ideScreenBuffer_);
        session.reset();
        messages_.push_back({error, {}, 0, 0, false});
        if (messagesWindow_) messagesWindow_->updateMessages();
        messageBox(error.c_str(), mfError | mfOKButton);
        return;
    }
    debugger_ = std::move(session);
    SetConsoleActiveScreenBuffer(ideScreenBuffer_);
    applyDebugStop(firstStop);
}

void TurboIDEApp::showDebugWatches() {
    if (!watchWindow_) {
        const TRect extent = deskTop->getExtent();
        const short width = std::max<short>(28, std::min<short>(44, extent.b.x / 2));
        const short height = std::max<short>(7, std::min<short>(12, extent.b.y / 2));
        watchWindow_ = new DebugWatchWindow(
            TRect(extent.b.x - width, 1, extent.b.x, 1 + height), this,
            &watchVariables_, nextWindowNumber());
        deskTop->insert(watchWindow_);
    } else {
        watchWindow_->refresh();
        deskTop->setCurrent(watchWindow_, TGroup::enterSelect);
    }
}

void TurboIDEApp::showDebugLocals() {
    if (!debugWatchesWindow_) {
        const TRect extent = deskTop->getExtent();
        const short width = std::max<short>(28, std::min<short>(44, extent.b.x / 2));
        const short height = std::max<short>(7, std::min<short>(12, extent.b.y / 2));
        debugWatchesWindow_ = new DebugWatchesWindow(
            TRect(extent.b.x - width, 1, extent.b.x, 1 + height), this,
            &debugVariables_, nextWindowNumber());
        deskTop->insert(debugWatchesWindow_);
    } else {
        debugWatchesWindow_->updateVariables();
        deskTop->setCurrent(debugWatchesWindow_, TGroup::enterSelect);
    }
}

void TurboIDEApp::updateDebugWatches(DebugWatchesWindow *window) {
    if (debugWatchesWindow_ == window)
        window->updateVariables();
}

void TurboIDEApp::dismissDebugWatches(DebugWatchesWindow *window) {
    if (debugWatchesWindow_ == window)
        debugWatchesWindow_ = nullptr;
}

void TurboIDEApp::dismissWatch(DebugWatchWindow *window) {
    if (watchWindow_ == window) watchWindow_ = nullptr;
}

void TurboIDEApp::addWatch() {
    char expression[256]{};
    if (execDialog(createSingleInputDialog("Add Watch", "~E~xpression", 250,
                                           hcAddWatchDialog), expression) != cmOK)
        return;
    const std::string text(expression);
    if (text.empty() || text.find_first_of("\r\n") != std::string::npos) return;
    if (std::find(watchExpressions_.begin(), watchExpressions_.end(), text) != watchExpressions_.end()) {
        messageBox("That watch already exists.", mfInformation | mfOKButton);
        return;
    }
    watchExpressions_.push_back(text);
    if (debugger_ && debugger_->active() && !debugger_->running())
        evaluateWatches();
    else
        watchVariables_.push_back({text, "not evaluated", {}});
    if (watchWindow_) watchWindow_->refresh(); else showDebugWatches();
    if (hasProject_ && !saveDesktopSession())
        messageBox("Could not save the project desktop file.", mfError | mfOKButton);
}

void TurboIDEApp::evaluateWatches() {
    if (!debugger_ || !debugger_->active() || debugger_->running()) {
        messageBox("Watches can only be evaluated while debugging is stopped.",
                   mfInformation | mfOKButton);
        return;
    }
    watchVariables_.clear();
    for (const auto &expression : watchExpressions_) {
        DebugVariable watch;
        watch.name = expression;
        std::string error;
        if (!debugger_->evaluate(expression, watch.value, error)) {
            watch.value = "<error: " + error + ">";
            const bool safetyModeFailed = error.rfind("Could not restore GDB's write permissions:", 0) == 0;
            watchVariables_.push_back(std::move(watch));
            if (safetyModeFailed) {
                messages_.push_back({error, {}, 0, 0, false});
                stopDebuggee();
                if (!watchVariables_.empty())
                    watchVariables_.back().value = "<error: " + error + ">";
                if (watchWindow_) watchWindow_->refresh();
                break;
            }
            continue;
        }
        watchVariables_.push_back(std::move(watch));
    }
    if (watchWindow_) watchWindow_->refresh();
}

void TurboIDEApp::deleteWatch(short index) {
    if (index < 0 || static_cast<size_t>(index) >= watchExpressions_.size()) return;
    watchExpressions_.erase(watchExpressions_.begin() + index);
    if (static_cast<size_t>(index) < watchVariables_.size())
        watchVariables_.erase(watchVariables_.begin() + index);
    if (watchWindow_) watchWindow_->refresh();
    if (hasProject_ && !saveDesktopSession())
        messageBox("Could not save the project desktop file.", mfError | mfOKButton);
}

void TurboIDEApp::applyDebugStop(const DebugStop &stop) {
    currentDebugStop_ = stop;
    if (stop.exited) {
        const std::string message = stop.reason == "exited-signalled" ?
            "Debuggee terminated by signal " + (stop.signalName.empty() ? "unknown" : stop.signalName) +
                (stop.signalMeaning.empty() ? "." : " (" + stop.signalMeaning + ").") :
            stop.hasExitCode && stop.exitCode != 0 ?
                "Debuggee exited with code " + std::to_string(stop.exitCode) + "." :
            stop.reason == "exited-normally" ? "Debuggee exited normally (code 0)." :
            stop.hasExitCode ? "Debuggee exited with code " + std::to_string(stop.exitCode) + "." :
            "Debuggee stopped: " + stop.reason + ".";
        messages_.push_back({message, {}, 0, 0, false});
        debugger_.reset();
        debugVariables_.clear();
        for (auto &watch : watchVariables_) watch.value = "<program exited>";
        currentDebugStop_ = {};
        for (TView *view = deskTop->first(); view; view = view->nextView()) {
            if (auto *editor = dynamic_cast<TEditWindow *>(view))
                setEditorDebugState(editor->editor, {}, 0);
        }
        if (debugWatchesWindow_) debugWatchesWindow_->updateVariables();
        if (watchWindow_) watchWindow_->refresh();
        if (messagesWindow_) messagesWindow_->updateMessages();
        return;
    }

    if (!stop.file.empty() && stop.line > 0) {
        if (!goToLocation(stop.file, stop.line, 1))
            messages_.push_back({"GDB stopped at an unavailable source location: " +
                                 stop.file.u8string() + ":" + std::to_string(stop.line), {}, 0, 0, false});
        if (auto *window = currentEditorWindow())
            setEditorDiagnostic(window->editor, 0);
    } else if (stop.reason != "breakpoint-hit" && stop.reason != "end-stepping-range") {
        messages_.push_back({"GDB stop has no usable source location (" + stop.reason + ").", {}, 0, 0, false});
    }
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *editor = dynamic_cast<TEditWindow *>(view);
        if (!editor || !editor->editor->fileName[0]) continue;
        const auto editorPath = normalizedPathKey(
            std::filesystem::u8path(editor->editor->fileName));
        std::vector<int> editorBreakpoints;
        const auto found = breakpoints_.find(editorPath);
        if (found != breakpoints_.end())
            editorBreakpoints.assign(found->second.begin(), found->second.end());
        const bool isCurrent = !stop.file.empty() && stop.line > 0 &&
            _wcsicmp(editorPath.c_str(), normalizedPathKey(stop.file).c_str()) == 0;
        setEditorDebugState(editor->editor, editorBreakpoints, isCurrent ? stop.line : 0);
    }

    std::string error;
    if (debugger_) debugVariables_ = debugger_->locals(error);
    if (!error.empty())
        messages_.push_back({"GDB locals: " + error, {}, 0, 0, false});
    if (debugWatchesWindow_) debugWatchesWindow_->updateVariables();
    else showDebugLocals();
    evaluateWatches();
    if (!watchExpressions_.empty() && !watchWindow_) showDebugWatches();
    const std::string location = stop.file.empty() ? std::string{} :
        " at " + stop.file.filename().u8string() + ":" + std::to_string(stop.line);
    messages_.push_back({"Stopped: " + stop.reason + location, {}, 0, 0, false});
    if (messagesWindow_) messagesWindow_->updateMessages();
}

void TurboIDEApp::continueDebuggee(const char *command) {
    if (!debugger_ || !debugger_->active() || debugger_->running()) return;
    SetConsoleActiveScreenBuffer(userScreenBuffer_);
    DebugStop stop;
    std::string error;
    bool ok = false;
    if (std::strcmp(command, "continue") == 0)
        ok = debugger_->resume(stop, error);
    else if (std::strcmp(command, "step") == 0)
        ok = debugger_->stepInto(stop, error);
    else
        ok = debugger_->stepOver(stop, error);
    SetConsoleActiveScreenBuffer(ideScreenBuffer_);
    if (!ok) {
        stopDebuggee();
        messageBox(error.c_str(), mfError | mfOKButton);
        return;
    }
    applyDebugStop(stop);
}

void TurboIDEApp::stopDebuggee() {
    if (!debugger_) return;
    SetConsoleActiveScreenBuffer(ideScreenBuffer_);
    std::string error;
    debugger_->stop(error);
    debugger_.reset();
    currentDebugStop_ = {};
    debugVariables_.clear();
    for (auto &watch : watchVariables_) watch.value = "<not evaluated>";
    for (TView *view = deskTop->first(); view; view = view->nextView())
        if (auto *editor = dynamic_cast<TEditWindow *>(view))
            setEditorDebugState(editor->editor, {}, 0);
    if (debugWatchesWindow_) debugWatchesWindow_->updateVariables();
    if (watchWindow_) watchWindow_->refresh();
    messages_.push_back({error.empty() ? "Debugging stopped." : "Debugging stopped: " + error,
                         {}, 0, 0, false});
    if (messagesWindow_) messagesWindow_->updateMessages();
}

void TurboIDEApp::showLastUserScreen() {
    if (userScreenBuffer_ == INVALID_HANDLE_VALUE) {
        userScreenBuffer_ = createUserScreenBuffer(ideScreenBuffer_);
        if (userScreenBuffer_ == INVALID_HANDLE_VALUE) {
            messageBox("Cannot create the user screen buffer.", mfError | mfOKButton);
            return;
        }
    }
    std::string error;
    if (!showUserScreen(userScreenBuffer_, ideScreenBuffer_, consoleInput_, !debugger_,
                        runWorkingDirectory_, error))
        messageBox("Cannot display or restore the user screen.", mfError | mfOKButton);
    if (!error.empty())
        messageBox(error.c_str(), mfError | mfOKButton);
}

void TurboIDEApp::showCommandPrompt() {
    if (debugger_) {
        messageBox("Close the debugging session before starting a command prompt.",
                   mfInformation | mfOKButton);
        return;
    }
    if (userScreenBuffer_ == INVALID_HANDLE_VALUE)
        userScreenBuffer_ = createUserScreenBuffer(ideScreenBuffer_);
    std::string error;
    if (!runCommandPrompt(userScreenBuffer_, ideScreenBuffer_, consoleInput_,
                          runWorkingDirectory_, error))
        messageBox(error.empty() ? "Cannot start the command prompt." : error.c_str(),
                   mfError | mfOKButton);
}

void TurboIDEApp::restoreEditorAfterRun() {
    TEditWindow *target = nullptr;
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *candidate = dynamic_cast<TEditWindow *>(view);
        if (!candidate || !candidate->editor->fileName[0])
            continue;
        if (!target)
            target = candidate;
        if (!runReturnFile_.empty()) {
            const auto file = std::filesystem::absolute(
                std::filesystem::u8path(candidate->editor->fileName)).lexically_normal();
            if (_wcsicmp(file.c_str(), runReturnFile_.c_str()) == 0) {
                target = candidate;
                break;
            }
        }
    }
    runReturnFile_.clear();
    if (target) {
        if (deskTop->first() != target)
            target->putInFrontOf(deskTop->first());
        deskTop->setCurrent(target, TGroup::enterSelect);
    }
}

void TurboIDEApp::idle() {
    TApplication::idle();
    syncEditMenuState();
    if (!startupProject_.empty()) {
        const auto project = std::move(startupProject_);
        activateProject(project);
    }
    std::optional<BuildResult> finished;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        if (finishedBuild_) {
            finished = std::move(finishedBuild_);
            finishedBuild_.reset();
        }
    }
    if (finished) {
        if (buildThread_.joinable())
            buildThread_.join();
        buildRunning_ = false;
        showBuildResult(std::move(*finished));
    }
    if (buildPopup_ && buildPopupSuccess_ &&
        std::chrono::steady_clock::now() - lastBlink_ >= std::chrono::milliseconds(500)) {
        lastBlink_ = std::chrono::steady_clock::now();
        blinkSuccess_ = !blinkSuccess_;
        buildPopup_->setSuccess(true, blinkSuccess_, buildWarnings_, buildErrors_);
    }
}

void TurboIDEApp::dismissBuildPopup() {
    if (!buildPopup_)
        return;
    BuildProgressWindow *window = buildPopup_;
    buildPopup_ = nullptr;
    deskTop->remove(window);
    TObject::destroy(window);
    if (buildReturnView_ && buildReturnView_->owner == deskTop) {
        if (deskTop->first() != buildReturnView_)
            buildReturnView_->putInFrontOf(deskTop->first());
        deskTop->setCurrent(buildReturnView_, TGroup::enterSelect);
    }
    buildReturnView_ = nullptr;
}

void TurboIDEApp::dismissMessages(BuildMessagesWindow *window) {
    if (messagesWindow_ == window) {
        messagesWindow_ = nullptr;
        setMessageStatus(nullptr);
    }
}

void TurboIDEApp::showMessages() {
    if (!messagesWindow_) {
        const TRect extent = deskTop->getExtent();
        const short top = std::max<short>(1, extent.b.y - 7);
        messagesWindow_ = new BuildMessagesWindow(
            TRect(extent.a.x, top, extent.b.x, extent.b.y), this, &messages_,
            nextWindowNumber());
        deskTop->insert(messagesWindow_);
    }
    messagesWindow_->updateMessages();
    messagesWindow_->show();
    if (messageIndex_ < messages_.size())
        messagesWindow_->focusMessage(messageIndex_);
    else if (!messages_.empty())
        messagesWindow_->focusMessage(0);
    if (deskTop->first() != messagesWindow_)
        messagesWindow_->putInFrontOf(deskTop->first());
    deskTop->setCurrent(messagesWindow_, TGroup::enterSelect);
}

void TurboIDEApp::hideMessages() {
    if (messagesWindow_)
        messagesWindow_->hide();
}

bool TurboIDEApp::trackMessage(const std::filesystem::path &file, int line, int column,
                               size_t index) {
    messageIndex_ = index;
    if (!goToLocation(file, line, column))
        return false;
    setMessageStatus(index < messages_.size() ? &messages_[index] : nullptr);
    if (messagesWindow_) {
        messagesWindow_->show();
        if (deskTop->first() != messagesWindow_)
            messagesWindow_->putInFrontOf(deskTop->first());
        deskTop->setCurrent(messagesWindow_, TGroup::enterSelect);
    }
    return true;
}

bool TurboIDEApp::gotoMessage(size_t index) {
    if (index >= messages_.size() || !messages_[index].hasLocation)
        return false;
    auto &selected = messages_[index];
    bool opened = goToLocation(selected.file, selected.line, selected.column);
    if (!opened && !std::filesystem::exists(selected.file)) {
        char fileName[MAXPATH]{};
        const auto suggested = selected.file.filename().u8string();
        std::snprintf(fileName, sizeof(fileName), "%s", suggested.c_str());
        if (execDialog(new ContextHelpFileDialog("*.*", "Locate source file", "~N~ame",
                                                  fdOpenButton, 100, hcMessagesWindow), fileName) == cmCancel)
            return false;
        selected.file = std::filesystem::u8path(fileName);
        opened = goToLocation(selected.file, selected.line, selected.column);
    }
    if (!opened)
        return false;
    messageIndex_ = index;
    setMessageStatus(&selected);
    hideMessages();
    return true;
}

void TurboIDEApp::clearMessages() {
    messages_.clear();
    messageIndex_ = 0;
    for (TView *view = deskTop->first(); view; view = view->nextView())
        if (auto *window = dynamic_cast<TEditWindow *>(view))
            setEditorDiagnostic(window->editor, 0);
    setMessageStatus(nullptr);
    if (messagesWindow_)
        messagesWindow_->updateMessages();
}

void TurboIDEApp::setMessageStatus(const BuildMessage *message) {
    auto *status = dynamic_cast<IDEStatusLine *>(TProgram::statusLine);
    if (status)
        status->setDiagnosticMessage(message && message->hasLocation ? message->text : std::string{});
}

void TurboIDEApp::selectMessageIndex(size_t index) {
    messageIndex_ = index;
    setMessageStatus(index < messages_.size() ? &messages_[index] : nullptr);
}

void TurboIDEApp::navigateMessage(bool forward) {
    if (messages_.empty())
        return;
    const size_t count = messages_.size();
    size_t current = messageIndex_ < count ? messageIndex_ : (forward ? count - 1 : 0);
    for (size_t attempt = 0; attempt < count; ++attempt) {
        current = forward ? (current + 1) % count : (current + count - 1) % count;
        messageIndex_ = current;
        const auto &message = messages_[messageIndex_];
        if (message.hasLocation) {
            if (messagesWindow_)
                messagesWindow_->focusMessage(messageIndex_);
            if (goToLocation(message.file, message.line, message.column)) {
                setMessageStatus(&message);
                return;
            }
        }
    }
    showMessages();
}

bool TurboIDEApp::goToLocation(const std::filesystem::path &file, int line, int column) {
    const auto target = std::filesystem::path(normalizedPathKey(file));
    TEditWindow *editorWindow = nullptr;
    for (TView *view = deskTop->first(); view; view = view->nextView()) {
        auto *candidate = dynamic_cast<TEditWindow *>(view);
        if (!candidate || !candidate->editor->fileName[0])
            continue;
        const auto current = std::filesystem::path(normalizedPathKey(
            std::filesystem::u8path(candidate->editor->fileName)));
        if (_wcsicmp(current.c_str(), target.c_str()) == 0) {
            editorWindow = candidate;
            break;
        }
    }

    if (!editorWindow) {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(target, fileError))
            return false;
        const auto utf8 = target.u8string();
        openEditor(utf8.c_str());
        for (TView *view = deskTop->first(); view; view = view->nextView()) {
            auto *candidate = dynamic_cast<TEditWindow *>(view);
            if (!candidate || !candidate->editor->fileName[0])
                continue;
            const auto current = std::filesystem::path(normalizedPathKey(
                std::filesystem::u8path(candidate->editor->fileName)));
            if (_wcsicmp(current.c_str(), target.c_str()) == 0) {
                editorWindow = candidate;
                break;
            }
        }
    }
    if (!editorWindow)
        return false;

    for (TView *view = deskTop->first(); view; view = view->nextView())
        if (auto *window = dynamic_cast<TEditWindow *>(view))
            setEditorDiagnostic(window->editor, 0);
    deskTop->setCurrent(editorWindow, TGroup::enterSelect);
    uint position = 0;
    for (int currentLine = 1; currentLine < line && position < editorWindow->editor->bufLen; ++currentLine)
        position = editorWindow->editor->nextLine(position);
    position = editorWindow->editor->lineStart(position);
    if (column > 1)
        position = std::min<uint>(editorWindow->editor->bufLen, position + static_cast<uint>(column - 1));
    moveEditorCursor(editorWindow->editor, position);
    editorWindow->editor->trackCursor(True);
    setEditorDiagnostic(editorWindow->editor, line);
    return true;
}

void TurboIDEApp::handleEvent(TEvent &event) {
    syncEditMenuState();
    if (event.what == evKeyDown && (!TProgram::application ||
        TProgram::application->current == TProgram::deskTop)) {
        if (event.keyDown.keyCode == kbCtrlF1) {
            if (auto *window = currentEditorWindow()) {
                const std::string word = identifierAtCursor(*window->editor);
                clearEvent(event);
                if (word.empty())
                    messageBox("Place the cursor on an identifier for Help.", mfInformation | mfOKButton);
                else
                    showHelpContents(word);
                return;
            }
        } else if (event.keyDown.keyCode == kbCtrlJ) {
            if (currentEditorWindow()) {
                clearEvent(event);
                goToLine();
                return;
            }
        } else if (event.keyDown.keyCode == kbCtrlP) {
            if (auto *window = currentEditorWindow(); window &&
                editorSupportsPrefixKeys(window->editor)) {
                runEditorFeature(window->editor, cmExpandPmacro);
                clearEvent(event);
                return;
            }
        }
    }
    if (event.what == evKeyDown && TKey(event.keyDown) == kbAltBack &&
        (!TProgram::application || TProgram::application->current == TProgram::deskTop)) {
        if (auto *window = currentEditorWindow(); window && window->editor) {
            TEvent undo{};
            undo.what = evCommand;
            undo.message.command = cmUndo;
            window->editor->handleEvent(undo);
            clearEvent(event);
            return;
        }
    }
    if (event.what == evKeyDown && projectWindow_ && deskTop->current == projectWindow_ &&
        projectWindow_->handleListKey(event))
        return;
    const bool saveCommand = event.what == evCommand &&
        (event.message.command == cmSave || event.message.command == cmSaveAs ||
         event.message.command == cmSaveAll);
    TApplication::handleEvent(event);
    if (saveCommand) saveDesktopSession();
    if (event.what != evCommand)
        return;

    switch (event.message.command) {
    case cmNew:
        newEditor();
        break;
    case cmOpen:
        openFile();
        break;
    case cmNewProject:
        newProject();
        break;
    case cmOpenProject:
        openProject();
        break;
    case cmProjectWindow:
        showProjectWindow();
        break;
    case cmWindowList:
        showWindowList();
        break;
    case cmProjectAdd:
        addProjectItem();
        break;
    case cmProjectDelete:
        deleteProjectItem();
        break;
    case cmProjectClose:
        closeProject();
        break;
    case cmCompile:
        compileCurrent();
        break;
    case cmBuild:
        buildProject();
        break;
    case cmRun:
        buildProject(true);
        break;
    case cmDebugStart:
        debugProject();
        break;
    case cmDebugContinue:
        continueDebuggee("continue");
        break;
    case cmDebugStepInto:
        continueDebuggee("step");
        break;
    case cmDebugStepOver:
        continueDebuggee("next");
        break;
    case cmDebugToggleBreakpoint:
        toggleBreakpoint();
        break;
    case cmDebugStop:
        stopDebuggee();
        break;
    case cmShowWatches:
        showDebugWatches();
        break;
    case cmShowLocals:
        showDebugLocals();
        break;
    case cmAddWatch:
        addWatch();
        break;
    case cmEvaluateWatches:
        evaluateWatches();
        break;
    case cmUserScreen:
        showLastUserScreen();
        break;
    case cmCommandPrompt:
        showCommandPrompt();
        break;
    case cmRunParameters:
        editRunParameters();
        break;
    case cmRunDirectory:
        editRunDirectory();
        break;
    case cmChangeDirectory:
        changeDirectory();
        break;
    case cmEnvironment:
        editEnvironment();
        break;
    case cmCompiler:
        editCompiler();
        break;
    case cmColors:
        editColors();
        break;
    case cmExpandPmacro:
    case cmChoosePmacro:
    case cmMatchBracket:
    case cmRecordMacro:
    case cmStopMacro:
    case cmPlayMacro:
    case cmMenuBlockEnd:
    case cmMenuBlockStart:
    case cmMenuReplaceSelect:
    case cmMenuHideBlock:
    case cmMenuCopyBlock:
    case cmMenuInvertCase:
    case cmMenuAlternateCase:
    case cmMenuSelectLine:
    case cmMenuSelectWord:
    case cmMenuIndentBlock:
    case cmMenuUnindentBlock:
    case cmMenuUpperCase:
    case cmMenuLowerCase:
    case cmMenuReadBlock:
    case cmMenuMoveBlock:
    case cmMenuWriteBlock:
    case cmMenuRectStart:
    case cmMenuRectEnd:
    case cmMenuRectCopy:
    case cmMenuRectDelete:
    case cmMenuRectClear:
    case cmMenuRectHide:
    case cmMenuRectMove:
    case cmMenuRectPaste:
    case cmMenuRectCut:
    case cmMenuRectToggleMovePaste:
    case cmMenuRectDuplicate: {
        TEditWindow *window = currentEditorWindow();
        if (!window || !runEditorFeature(window->editor, event.message.command))
            messageBox("This command requires an open editor.",
                       mfInformation | mfOKButton);
        break;
    }
    case cmMenuInsertTab: {
        if (auto *window = currentEditorWindow(); window && window->editor) {
            TEvent tab{};
            tab.what = evKeyDown;
            tab.keyDown.keyCode = kbTab;
            tab.keyDown.charScan.charCode = '\t';
            window->editor->handleEvent(tab);
        }
        break;
    }
    case cmGoToLine:
        goToLine();
        break;
    case cmShowMessages:
    case cmCompilerMessages:
        showMessages();
        break;
    case cmMessagesClear:
        clearMessages();
        break;
    case cmMessagesGotoSource:
        if (messageIndex_ < messages_.size())
            gotoMessage(messageIndex_);
        break;
    case cmMessagesTrackSource:
        if (messageIndex_ < messages_.size() && messages_[messageIndex_].hasLocation) {
            const auto &message = messages_[messageIndex_];
            trackMessage(message.file, message.line, message.column, messageIndex_);
        }
        break;
    case cmNextMessage:
        navigateMessage(true);
        break;
    case cmPrevMessage:
        navigateMessage(false);
        break;
    case cmDismissBuildPopup:
        dismissBuildPopup();
        break;
    case cmAbout:
        showAbout();
        break;
    case cmHelp:
        showHelpContext(activeHelpContext());
        break;
    case cmHelpContents:
        showHelpContents();
        break;
    case cmHelpIndex:
        showHelpContext(10031);
        break;
    default:
        return;
    }
    clearEvent(event);
}

TMenuBar *TurboIDEApp::initMenuBar(TRect r) {
    r.b.y = r.a.y + 1;
    return new IDEMenuBar(r,
        *new TSubMenu("~F~ile", kbAltF, hcFileMenu) +
            *new TMenuItem("~N~ew", cmNew, kbCtrlN, hcNoContext, "Ctrl-N") +
            *new TMenuItem("~O~pen...", cmOpen, kbF3, hcNoContext, "F3") +
            *new TMenuItem("~S~ave", cmSave, kbF2, hcNoContext, "F2") +
            *new TMenuItem("Save ~a~s...", cmSaveAs, TKey(kbNoKey)) + newLine() +
            *new TMenuItem("~C~hange dir...", cmChangeDirectory, TKey(kbNoKey)) + newLine() +
            *new TMenuItem("E~x~it", cmQuit, kbAltX, hcNoContext, "Alt-X") +
        *new TSubMenu("~E~dit", kbAltE, hcEditMenu) +
            *new TMenuItem("~U~ndo", cmUndo, kbAltBack, hcNoContext, "Alt-Backspace / Ctrl-U") +
            *new TMenuItem("~R~edo", cmRedo,
                TKey(kbBack, kbAltShift | kbShift), hcNoContext, "Alt+Shift+Backspace") + newLine() +
            *new TMenuItem("Cu~t~", cmCut, kbShiftDel, hcNoContext, "Shift-Del") +
            *new TMenuItem("~C~opy", cmCopy, kbCtrlIns, hcNoContext, "Ctrl-Ins") +
            *new TMenuItem("~P~aste", cmPaste, kbShiftIns, hcNoContext, "Shift-Ins") + newLine() +
            *new TMenuItem("~N~avigation", kbNoKey,
                new TMenu(*new TMenuItem("Page down", cmPageDown, kbNoKey, hcNoContext, "Ctrl-C") +
                          *new TMenuItem("Character right", cmCharRight, kbNoKey, hcNoContext, "Ctrl-D") +
                          *new TMenuItem("Line up", cmLineUp, kbNoKey, hcNoContext, "Ctrl-E") +
                          *new TMenuItem("Word right", cmWordRight, kbNoKey, hcNoContext, "Ctrl-F") +
                          *new TMenuItem("Search again", cmSearchAgain, kbNoKey, hcNoContext, "Ctrl-L") +
                          *new TMenuItem("Page up", cmPageUp, kbNoKey, hcNoContext, "Ctrl-R") +
                          *new TMenuItem("Character left", cmCharLeft, kbNoKey, hcNoContext, "Ctrl-S") +
                          *new TMenuItem("Line down", cmLineDown, kbNoKey, hcNoContext, "Ctrl-X") + newLine() +
                          *new TMenuItem("Word left", cmWordLeft, kbNoKey, hcNoContext, "Ctrl-Left") +
                          *new TMenuItem("Go to line...", cmGoToLine, kbNoKey, hcNoContext, "Ctrl-J") +
                          *new TMenuItem("Match bracket", cmMatchBracket, kbNoKey, hcNoContext,
                                         "Alt+[ / Alt+]"))) +
            *new TMenuItem("~D~elete", kbNoKey,
                new TMenu(*new TMenuItem("Character", cmDelChar, kbNoKey, hcNoContext, "Ctrl-G") +
                          *new TMenuItem("Previous character", cmBackSpace, kbNoKey, hcNoContext, "Ctrl-H") +
                          *new TMenuItem("Word", cmDelWord, kbNoKey, hcNoContext, "Ctrl-T") +
                          *new TMenuItem("Previous word", cmDelWordLeft, kbNoKey, hcNoContext, "Ctrl-Backspace") +
                          *new TMenuItem("Line", cmDelLine, kbNoKey, hcNoContext, "Ctrl-Y") +
                          *new TMenuItem("To line start", cmDelStart, kbNoKey, hcNoContext,
                                         "Ctrl+Shift+Backspace") +
                          *new TMenuItem("To line end", cmDelEnd, kbNoKey, hcNoContext, "Ctrl+Shift+Y") +
                          *new TMenuItem("Selection", cmClear, kbNoKey, hcNoContext, "Ctrl-Del"))) +
            *new TMenuItem("~I~nsert", kbNoKey,
                new TMenu(*new TMenuItem("New line", cmNewLine, kbNoKey, hcNoContext, "Ctrl-M") +
                          *new TMenuItem("Toggle insert/overwrite", cmInsMode, kbNoKey, hcNoContext, "Ctrl-V") +
                          *new TMenuItem("Toggle auto indent", cmIndentMode, kbNoKey, hcNoContext, "Ctrl-O") + newLine() +
                          *new TMenuItem("Expand snippet", cmExpandPmacro, kbNoKey, hcNoContext, "Ctrl-P") +
                          *new TMenuItem("Choose snippet...", cmChoosePmacro, TKey(kbNoKey)))) +
            *new TMenuItem("Se~l~ection", kbNoKey,
                new TMenu(*new TMenuItem("Select all", cmSelectAll, kbNoKey, hcNoContext, "Ctrl-A") + newLine() +
                          *new TMenuItem("Start block", cmMenuBlockStart, kbNoKey, hcNoContext, "Ctrl+Shift+B") +
                          *new TMenuItem("End block", cmMenuBlockEnd, kbNoKey, hcNoContext, "Ctrl+Shift+K") +
                          *new TMenuItem("Copy block to clipboard", cmCopy, kbNoKey, hcNoContext, "Ctrl+Shift+C") +
                          *new TMenuItem("Hide/show block", cmMenuHideBlock, kbNoKey, hcNoContext, "Ctrl+Shift+H") +
                          *new TMenuItem("Cut block", cmCut, kbNoKey, hcNoContext, "Ctrl+Shift+X") +
                          *new TMenuItem("Duplicate block", cmMenuCopyBlock, kbNoKey, hcNoContext, "Ctrl-K, C") +
                          *new TMenuItem("Select line", cmMenuSelectLine, kbNoKey, hcNoContext, "Ctrl+Shift+L") +
                          *new TMenuItem("Select word", cmMenuSelectWord, kbNoKey, hcNoContext, "Ctrl+Shift+T") + newLine() +
                          *new TMenuItem("Indent block", cmMenuIndentBlock, kbNoKey, hcNoContext, "Ctrl+Shift+I") +
                          *new TMenuItem("Unindent block", cmMenuUnindentBlock, kbNoKey, hcNoContext, "Ctrl+Shift+U") +
                          *new TMenuItem("Uppercase selection", cmMenuUpperCase, kbNoKey, hcNoContext, "Ctrl+Shift+M") +
                          *new TMenuItem("Lowercase selection", cmMenuLowerCase, kbNoKey, hcNoContext, "Ctrl+Shift+O") +
                          *new TMenuItem("Invert case", cmMenuInvertCase, TKey(kbNoKey)) +
                          *new TMenuItem("Replace block from clipboard", cmMenuReplaceSelect, kbNoKey,
                                         hcNoContext, "Ctrl+Shift+Ins") +
                          *new TMenuItem("Read block...", cmMenuReadBlock, kbNoKey, hcNoContext, "Ctrl+Shift+R") +
                          *new TMenuItem("Write block...", cmMenuWriteBlock, kbNoKey, hcNoContext, "Ctrl+Shift+W"))) +
        *new TSubMenu("~S~earch", kbAltS, hcSearchMenu) +
            *new TMenuItem("~F~ind...", cmFind, TKey(kbNoKey)) +
            *new TMenuItem("~R~eplace...", cmReplace, TKey(kbNoKey)) +
            *new TMenuItem("Search ~a~gain", cmSearchAgain, TKey(kbNoKey)) +
        *new TSubMenu("~R~un", kbAltR, hcRunMenu) +
            *new TMenuItem("~R~un", cmRun, kbCtrlF9, hcNoContext, "Ctrl-F9") +
            *new TMenuItem("User screen", cmUserScreen, kbAltF5, hcNoContext, "Alt-F5") +
            *new TMenuItem("Command prompt...", cmCommandPrompt, TKey(kbNoKey)) +
            *new TMenuItem("Run ~d~irectory...", cmRunDirectory, TKey(kbNoKey)) +
            *new TMenuItem("~P~arameters...", cmRunParameters, TKey(kbNoKey)) +
        *new TSubMenu("~C~ompile", kbAltC, hcCompileMenu) +
            *new TMenuItem("~C~ompile", cmCompile, kbAltF9, hcNoContext, "Alt-F9") +
            *new TMenuItem("~M~ake", cmBuild, kbF9, hcNoContext, "F9") +
            *new TMenuItem("Compiler ~m~essages", cmCompilerMessages, kbF12, hcNoContext, "F12") +
        *new TSubMenu("~D~ebug", kbAltD, hcDebugMenu) +
            *new TMenuItem("~S~tart debugging", cmDebugStart, TKey(kbNoKey)) +
            *new TMenuItem("~C~ontinue", cmDebugContinue, kbF4, hcNoContext, "F4") +
            *new TMenuItem("~T~race into", cmDebugStepInto, kbF7, hcNoContext, "F7") +
            *new TMenuItem("Step ~o~ver", cmDebugStepOver, kbF8, hcNoContext, "F8") +
            *new TMenuItem("Toggle ~b~reakpoint", cmDebugToggleBreakpoint, kbCtrlF8, hcNoContext, "Ctrl-F8") +
            *new TMenuItem("~S~top debugging", cmDebugStop, kbCtrlF2, hcNoContext, "Ctrl-F2") +
            *new TMenuItem("Show ~l~ocals", cmShowLocals, TKey(kbNoKey)) +
            *new TMenuItem("Show ~w~atches", cmShowWatches, TKey(kbNoKey)) +
            *new TMenuItem("~A~dd watch...", cmAddWatch, kbCtrlF7, hcNoContext, "Ctrl-F7") +
            *new TMenuItem("~E~valuate watches", cmEvaluateWatches, kbCtrlF4, hcNoContext, "Ctrl-F4") +
        *new TSubMenu("~P~roject", kbAltP, hcProjectMenu) +
            *new TMenuItem("New project...", cmNewProject, TKey(kbNoKey)) +
            *new TMenuItem("~O~pen project...", cmOpenProject, TKey(kbNoKey)) +
            *new TMenuItem("~C~lose project", cmProjectClose, TKey(kbNoKey)) + newLine() +
            *new TMenuItem("~A~dd item...", cmProjectAdd, TKey(kbNoKey)) +
            *new TMenuItem("~D~elete item", cmProjectDelete, TKey(kbNoKey)) +
        *new TSubMenu("~T~ools", kbAltT, hcToolsMenu) +
            *new TMenuItem("Mac~r~os", kbNoKey,
                new TMenu(*new TMenuItem("~R~ecord macro", cmRecordMacro, kbShiftF10,
                                         hcNoContext, "Shift-F10") +
                          *new TMenuItem("~S~top recording", cmStopMacro, kbAltF10,
                                         hcNoContext, "Alt-F10") +
                          *new TMenuItem("~P~lay macro", cmPlayMacro, kbCtrlF10,
                                         hcNoContext, "Ctrl-F10"))) + newLine() +
            *new TMenuItem("~M~essages", cmShowMessages, kbShiftF11, hcNoContext, "Shift-F11") +
            *new TMenuItem("Goto ~n~ext message", cmNextMessage, kbAltF8, hcNoContext, "Alt-F8") +
            *new TMenuItem("Goto ~p~revious message", cmPrevMessage, kbAltF7, hcNoContext, "Alt-F7") +
        *new TSubMenu("~O~ptions", kbAltO, hcOptionsMenu) +
            *new TMenuItem("~E~nvironment", kbNoKey,
                new TMenu(
                    *disabledMenuItem(new TMenuItem("~P~references...", cmNotReady, TKey(kbNoKey), hcEnvironmentPreferences)) +
                    *new TMenuItem("~E~ditor...", cmEnvironment, TKey(kbNoKey), hcEnvironmentEditor) +
                    *disabledMenuItem(new TMenuItem("~M~ouse...", cmNotReady, TKey(kbNoKey), hcEnvironmentMouse)) +
                    *disabledMenuItem(new TMenuItem("~S~tartup...", cmNotReady, TKey(kbNoKey), hcEnvironmentStartup)) +
                    *new TMenuItem("~C~olors...", cmColors, TKey(kbNoKey), hcEnvironmentColors)),
                hcEnvironmentMenu) +
            *new TMenuItem("~C~ompiler...", cmCompiler, TKey(kbNoKey)) +
            *disabledMenuItem(new TMenuItem("~D~irectories...", cmNotReady, TKey(kbNoKey))) +
        *new TSubMenu("~W~indow", kbAltW, hcWindowMenu) +
            *new TMenuItem("~S~ize/move", cmResize, kbCtrlF5, hcNoContext, "Ctrl-F5") +
            *new TMenuItem("~Z~oom", cmZoom, kbF5, hcNoContext, "F5") +
            *new TMenuItem("~T~ile", cmTile, TKey(kbNoKey)) +
            *new TMenuItem("C~a~scade", cmCascade, TKey(kbNoKey)) +
            *new TMenuItem("~N~ext", cmNext, kbF6, hcNoContext, "F6") +
            *new TMenuItem("~P~revious", cmPrev, kbShiftF6, hcNoContext, "Shift-F6") +
            *new TMenuItem("Project", cmProjectWindow, kbF11, hcNoContext, "F11") +
            *new TMenuItem("C~l~ose", cmClose, kbAltF3, hcNoContext, "Alt-F3") +
            *new TMenuItem("~L~ist all...", cmWindowList, kbAlt0, hcNoContext, "Alt+0") +
        *new TSubMenu("~H~elp", kbAltH, hcHelpMenu) +
            *new TMenuItem("~C~ontents", cmHelpContents, TKey(kbNoKey), 516) +
            *new TMenuItem("~I~ndex", cmHelpIndex, kbShiftF1, 10031, "Shift-F1") +
            *new TMenuItem("~A~bout", cmAbout, TKey(kbNoKey)));
}

TStatusLine *TurboIDEApp::initStatusLine(TRect r) {
    r.a.y = r.b.y - 1;
    return new IDEStatusLine(r,
        *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~F1~ Help", kbF1, cmHelp) +
            *new TStatusItem("~Shift-F1~ Index", kbShiftF1, cmHelpIndex) +
            *new TStatusItem("~F7~ Trace", kbF7, cmDebugStepInto) +
            *new TStatusItem("~F8~ Step", kbF8, cmDebugStepOver) +
            *new TStatusItem("~F9~ Make", kbF9, cmBuild) +
            *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
            *new TStatusItem("~F11~ Project", kbF11, cmProjectWindow) +
            *new TStatusItem(0, kbShiftDel, cmCut) +
            *new TStatusItem(0, kbCtrlIns, cmCopy) +
            *new TStatusItem(0, kbShiftIns, cmPaste) +
            *new TStatusItem(0, kbCtrlF5, cmResize));
}

void TurboIDEApp::showAbout() {
    messageBox("TurboIDE — консольная среда в стиле Borland C\n"
               "Редактор: Turbo Vision\n"
               "Компилятор: GCC; отладчик: GDB/MI.",
               mfInformation | mfOKButton);
}
}

void setEditorPrefixHint(TFileEditor *editor, int mode) {
    if (auto *status = dynamic_cast<IDEStatusLine *>(TProgram::statusLine))
        status->setPrefixMode(editor, mode);
}

void advanceEditorPrefixHintPage(TFileEditor *editor) {
    if (auto *status = dynamic_cast<IDEStatusLine *>(TProgram::statusLine))
        status->advancePrefixPage(editor);
}

int main() {
    TurboIDEApp app;
    app.run();
    return 0;
}
