#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TObject
#define Uses_TFrame
#define Uses_TWindow
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TFileDialog
#include "syntax.h"
#include "editor_features.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

IDETheme::IDETheme()
    : application(cpAppColor, sizeof(cpAppColor) - 1),
      messagesWindow("\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B", 8),
      messagesList("\x01\x01\x02\x03\x04", 5) {}

IDETheme &ideTheme() {
    static IDETheme theme;
    return theme;
}

namespace {
bool defaultPersistentBlocks = true;

enum SyntaxToken : unsigned char {
    tokenNormal,
    tokenKeyword,
    tokenComment,
    tokenString,
    tokenNumber,
    tokenPreprocessor
};

bool identifierStart(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

bool identifierContinue(unsigned char c) {
    return identifierStart(c) || (c >= '0' && c <= '9');
}

bool isCppFile(TStringView name) {
    const std::string_view path = name;
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos)
        return false;
    std::string_view extension = path.substr(dot);
    std::string lowered;
    lowered.reserve(extension.size());
    for (unsigned char c : extension)
        lowered.push_back(static_cast<char>(std::tolower(c)));
    static const std::unordered_set<std::string> supported = {
        ".c", ".h", ".cc", ".hh", ".cpp", ".cxx", ".hpp", ".hxx", ".inl", ".ipp"
    };
    return supported.count(lowered) != 0;
}

const std::unordered_set<std::string_view> &cppKeywords() {
    static const std::unordered_set<std::string_view> words = {
        "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "const", "constexpr", "continue",
        "decltype", "default", "delete", "do", "double", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "private",
        "protected", "public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof",
        "static", "static_assert", "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "wchar_t", "while", "xor", "_Bool", "_Complex", "_Imaginary"
    };
    return words;
}

class SyntaxEditor final : public TFileEditor {
    enum class SelectionCase { lower, upper, invert, alternate };

public:
    SyntaxEditor(const TRect &bounds, TScrollBar *horizontal, TScrollBar *vertical,
                 TIndicator *indicator, TStringView fileName)
        : TFileEditor(bounds, horizontal, vertical, indicator, fileName),
          persistentBlocks_(defaultPersistentBlocks) {}

    void handleEvent(TEvent &event) override {
        if (startsMouseSelection(event)) {
            discardSelectionAnchor();
        } else if (isShiftNavigation(event)) {
            if (!hasSelection() || (curPtr != selStart && curPtr != selEnd))
                discardSelectionAnchor();
        }
        if (replaceClipboardSelection_) {
            if (event.what == evKeyDown && (event.keyDown.controlKeyState & kbPaste) &&
                event.keyDown.textLength > 0) {
                const uint start = selStart, end = selEnd;
                setCurPtr(end, 0);
                selStart = start;
                selEnd = end;
                insertText(event.keyDown.text, event.keyDown.textLength, False);
                replaceClipboardSelection_ = false;
                blockHidden_ = false;
                clearEvent(event);
                updateMatchingBracket();
                return;
            }
            if (event.what == evKeyDown && !(event.keyDown.controlKeyState & kbPaste))
                replaceClipboardSelection_ = false;
        }
        if (event.what == evKeyDown) {
            if (prefixMode_) {
                const int mode = prefixMode_;
                if (event.keyDown.textLength == 1 && event.keyDown.text[0] == '?') {
                    advanceEditorPrefixHintPage(this);
                    clearEvent(event);
                    return;
                }
                prefixMode_ = 0;
                setEditorPrefixHint(this, 0);
                if (event.keyDown.keyCode != kbEsc)
                    executePrefix(mode, event);
                clearEvent(event);
                updateMatchingBracket();
                return;
            }
            if (event.keyDown.keyCode == kbCtrlQ || event.keyDown.keyCode == kbCtrlK) {
                prefixMode_ = event.keyDown.keyCode == kbCtrlQ ? 1 : 2;
                setEditorPrefixHint(this, prefixMode_);
                clearEvent(event);
                return;
            }
        }
        if (event.what == evCommand && event.message.command == cmMenuReplaceSelect) {
            replaceSelection();
            clearEvent(event);
            updateMatchingBracket();
            return;
        }
        if (event.what == evCommand && event.message.command == cmMenuHideBlock) {
            toggleBlockHidden();
            clearEvent(event);
            return;
        }
        const bool enter = isEnter(event);
        const bool closeBrace = isCloseBrace(event);
        const bool capture = recordable(event);
        const TEvent recorded = event;
        const uint oldCursor = curPtr;
        const bool snippetUndo = isUndo(event) && canUndoSnippet();
        if (snippetUndo) {
            undoSnippet();
            clearEvent(event);
        } else {
            if (snippetUndo_.valid && mayChangeText(event))
                snippetUndo_.valid = false;
            const bool handledDirect = handleDirectShortcut(event);
            const bool insertedDedentedBrace = !handledDirect && closeBrace && insertDedentedCloseBrace();
            if (mayChangeText(event) || handledDirect)
                tokensValid_ = false;
            if (handledDirect)
                clearEvent(event);
            if (insertedDedentedBrace)
                clearEvent(event);
            else if (!handledDirect)
                handleBaseEvent(event);
        }
        if (enter && !snippetUndo)
            indentAfterOpenBrace();
        if (capture && recording_ && !replaying_) {
            if (macroEvents_.size() < maxMacroEvents)
                macroEvents_.push_back(recorded);
            else {
                recording_ = false;
                refreshWindowTitle();
                messageBox("Macro recording stopped at the event limit.", mfError | mfOKButton);
            }
        }
        updateMatchingBracket();
        if (curPtr != oldCursor)
            lastCursor_ = oldCursor;
    }

    void formatLine(TDrawBuffer &drawBuffer, uint linePtr, int hScroll, int width,
                    TAttrPair colors) override {
        const uint savedStart = selStart, savedEnd = selEnd;
        if (blockHidden_)
            selStart = selEnd = curPtr;
        TFileEditor::formatLine(drawBuffer, linePtr, hScroll, width, colors);
        if (blockHidden_) {
            selStart = savedStart;
            selEnd = savedEnd;
        }
        ensureTokens();
        const bool breakpointLine = isBreakpointLine(linePtr);

        uint p = linePtr;
        int cellPos = 0;
        int x = 0;
        const uint triggerStart = pmacroTriggerStart(this);
        const int currentLine = lineNumber(linePtr);
        const int rectTop = std::min(rectTop_, rectBottom_);
        const int rectBottom = std::max(rectTop_, rectBottom_);
        const int rectLeft = std::min(rectLeft_, rectRight_);
        const int rectRight = std::max(rectLeft_, rectRight_);
        hScroll = std::max(hScroll, 0);
        width = std::max(width, 0);
        while (p < bufLen) {
            uint nextP = p;
            int nextPos = cellPos;
            nextCharAndPos(nextP, nextPos);
            if (x > width || (x == width && cellPos < nextPos))
                break;
            const char c = bufChar(p);
            if (c == '\r' || c == '\n')
                break;

            if (nextPos > hScroll) {
                const int charWidth = nextPos - std::max(cellPos, hScroll);
                const unsigned char token = p < tokens_.size() ? tokens_[p] : tokenNormal;
                const bool selected = !blockHidden_ && selStart <= p && p < selEnd;
                const bool rectSelected = rectangleValid_ && !rectangleHidden_ &&
                    currentLine >= rectTop && currentLine <= rectBottom &&
                    cellPos >= rectLeft && cellPos < rectRight;
                if (rectSelected)
                    drawBuffer.putAttribute(static_cast<ushort>(x), 0x70);
                else if (!selected && (token != tokenNormal || linePtr == diagnosticLineStart_ ||
                                  linePtr == executionLineStart_ || breakpointLine)) {
                    TColorAttr attr = colorFor(token);
                    if (linePtr == diagnosticLineStart_)
                        attr = ideTheme().diagnosticLine;
                    else if (linePtr == executionLineStart_)
                        attr = static_cast<TColorAttr>((attr & 0x0F) |
                            (ideTheme().executionLine & 0xF0));
                    else if (breakpointLine)
                        attr = static_cast<TColorAttr>((attr & 0x0F) |
                            (ideTheme().breakpointLine & 0xF0));
                    for (int col = 0; col < charWidth && x + col < width; ++col)
                        drawBuffer.putAttribute(static_cast<ushort>(x + col), attr);
                }
                if (p == matchingOpen_ || p == matchingClose_)
                    drawBuffer.putAttribute(static_cast<ushort>(x), ideTheme().matchingBracket);
                if (!selected && triggerStart != invalidPosition &&
                    p >= triggerStart && p < triggerStart + 2)
                    drawBuffer.putAttribute(static_cast<ushort>(x), 0x1F);
                x += charWidth;
            }
            p = nextP;
            cellPos = nextPos;
        }
    }

    void setDiagnosticLine(int line) {
        diagnosticLineStart_ = lineOffset(line);
        drawView();
    }

    void setDebugState(const std::vector<int> &breakpoints, int executionLine) {
        breakpointLines_.clear();
        for (int line : breakpoints)
            if (line > 0) breakpointLines_.insert(line);
        executionLineStart_ = lineOffset(executionLine);
        drawView();
    }

    bool runFeature(ushort command) {
        switch (command) {
        case cmExpandPmacro:
        case cmChoosePmacro: {
            const std::string before = bufferText();
            const uint caretBefore = curPtr;
            expandPmacro(this, command == cmChoosePmacro);
            saveSnippetUndo(before, caretBefore);
            tokensValid_ = false;
            updateMatchingBracket();
            drawView();
            return true;
        }
        case cmMatchBracket:
            if (matchingTarget_ != invalidPosition) {
                moveCaret(matchingTarget_);
                trackCursor(True);
                updateMatchingBracket();
                drawView();
            }
            return true;
        case cmMenuBlockStart: beginBlock(); return true;
        case cmMenuBlockEnd: endBlock(); return true;
        case cmMenuHideBlock: toggleBlockHidden(); return true;
        case cmMenuReplaceSelect: replaceSelection(); return true;
        case cmMenuCopyBlock: copyBlock(); return true;
        case cmMenuSelectLine: selectLine(); return true;
        case cmMenuSelectWord: selectWord(); return true;
        case cmMenuIndentBlock: indentSelection(1); return true;
        case cmMenuUnindentBlock: indentSelection(-1); return true;
        case cmMenuUpperCase: changeSelectionCase(SelectionCase::upper); return true;
        case cmMenuLowerCase: changeSelectionCase(SelectionCase::lower); return true;
        case cmMenuInvertCase: changeSelectionCase(SelectionCase::invert); return true;
        case cmMenuAlternateCase: changeSelectionCase(SelectionCase::alternate); return true;
        case cmMenuReadBlock: readBlock(); return true;
        case cmMenuMoveBlock: moveBlock(); return true;
        case cmMenuWriteBlock: writeBlock(); return true;
        case cmMenuRectStart: setRectangleStart(); return true;
        case cmMenuRectEnd: setRectangleEnd(); return true;
        case cmMenuRectCopy: copyRectangle(true); return true;
        case cmMenuRectDelete: editRectangle(0); return true;
        case cmMenuRectClear: editRectangle(1); return true;
        case cmMenuRectHide: rectangleHidden_ = !rectangleHidden_; drawView(); return true;
        case cmMenuRectMove: moveRectangle(); return true;
        case cmMenuRectPaste: if (moveOnPaste_) moveRectangle(); else pasteRectangle(); return true;
        case cmMenuRectCut: copyRectangle(true); editRectangle(0); return true;
        case cmMenuRectToggleMovePaste: moveOnPaste_ = !moveOnPaste_; return true;
        case cmMenuRectDuplicate: copyRectangle(false); pasteRectangle(); return true;
        case cmRecordMacro:
            macroEvents_.clear();
            recording_ = true;
            refreshWindowTitle();
            return true;
        case cmStopMacro:
            recording_ = false;
            refreshWindowTitle();
            return true;
        case cmPlayMacro:
            if (recording_) {
                messageBox("Stop recording before playing the macro.", mfInformation | mfOKButton);
                return true;
            }
            replaying_ = true;
            for (auto event : macroEvents_)
                handleEvent(event);
            replaying_ = false;
            tokensValid_ = false;
            updateMatchingBracket();
            drawView();
            return true;
        default:
            return false;
        }
    }

    bool recording() const { return recording_; }

    void setPersistentBlocks(bool enabled) {
        persistentBlocks_ = enabled;
        drawView();
    }

    bool persistentBlocks() const { return persistentBlocks_; }

    void moveCaret(uint position) {
        if (persistentBlocks_ && hasSelection()) {
            const uint start = selStart, end = selEnd;
            setCurPtr(position, 0);
            selStart = start;
            selEnd = end;
            update(ufView);
        } else {
            setCurPtr(position, 0);
        }
    }

private:
    static char prefixCharacter(const TEvent &event) {
        if (event.keyDown.keyCode == kbEsc)
            return 27;
        if (event.keyDown.textLength == 1)
            return static_cast<char>(std::toupper(static_cast<unsigned char>(event.keyDown.text[0])));
        const ushort key = event.keyDown.keyCode;
        if (key >= 1 && key <= 26)
            return static_cast<char>('A' + key - 1);
        return static_cast<char>(std::toupper(static_cast<unsigned char>(key & 0xFF)));
    }

    void dispatchEditorCommand(ushort command) {
        const uint oldCursor = curPtr;
        TEvent commandEvent{};
        commandEvent.what = evCommand;
        commandEvent.message.command = command;
        handleBaseEvent(commandEvent);
        if (curPtr != oldCursor)
            lastCursor_ = oldCursor;
    }

    bool handleDirectShortcut(const TEvent &event) {
        if (event.what != evKeyDown) return false;
        const TKey key(event.keyDown);
        constexpr ushort ctrlShift = kbCtrlShift | kbShift;
        constexpr ushort ctrlAlt = kbCtrlShift | kbAltShift;
        const auto matches = [&](ushort character, ushort modifiers) {
            return key == TKey(character, modifiers);
        };

        if (matches('[', kbAltShift) || matches(']', kbAltShift)) {
            runFeature(cmMatchBracket);
            return true;
        }
        if (key == TKey(kbIns, ctrlShift)) {
            replaceSelection();
            return true;
        }
        if (event.keyDown.controlKeyState & kbCtrlShift) {
            int digit = -1;
            const uchar scan = event.keyDown.charScan.scanCode;
            if (scan == 0x0B) digit = 0;
            else if (scan >= 0x02 && scan <= 0x0A) digit = scan - 0x01;
            if (digit >= 0 && !(event.keyDown.controlKeyState & kbAltShift)) {
                if (event.keyDown.controlKeyState & kbShift) {
                    marks_[digit] = curPtr;
                    markSet_[digit] = true;
                } else if (markSet_[digit]) {
                    moveCaret(std::min(marks_[digit], bufLen));
                }
                return true;
            }
        }

        ushort command = 0;
        if (matches('Y', ctrlShift)) command = cmDelEnd;
        else if (matches(kbBack, ctrlShift)) command = cmDelStart;
        else if (matches('B', ctrlShift)) command = cmMenuBlockStart;
        else if (matches('K', ctrlShift)) command = cmMenuBlockEnd;
        else if (matches('C', ctrlShift)) command = cmCopy;
        else if (matches('H', ctrlShift)) command = cmMenuHideBlock;
        else if (matches('X', ctrlShift)) command = cmCut;
        else if (matches('L', ctrlShift)) command = cmMenuSelectLine;
        else if (matches('T', ctrlShift)) command = cmMenuSelectWord;
        else if (matches('I', ctrlShift)) command = cmMenuIndentBlock;
        else if (matches('U', ctrlShift)) command = cmMenuUnindentBlock;
        else if (matches('M', ctrlShift)) command = cmMenuUpperCase;
        else if (matches('O', ctrlShift)) command = cmMenuLowerCase;
        else if (matches('V', ctrlShift)) command = cmMenuMoveBlock;
        else if (matches('R', ctrlShift)) command = cmMenuReadBlock;
        else if (matches('W', ctrlShift)) command = cmMenuWriteBlock;
        else if (matches('I', kbCtrlShift)) {
            TEvent tab{};
            tab.what = evKeyDown;
            tab.keyDown.keyCode = kbTab;
            tab.keyDown.charScan.charCode = '\t';
            TFileEditor::handleEvent(tab);
            return true;
        }
        else if (matches('B', ctrlAlt)) command = cmMenuRectStart;
        else if (matches('K', ctrlAlt)) command = cmMenuRectEnd;
        else if (matches('C', ctrlAlt)) command = cmMenuRectCopy;
        else if (matches('T', ctrlAlt)) command = cmMenuRectCut;
        else if (matches('L', ctrlAlt)) command = cmMenuRectDelete;
        else if (matches('E', ctrlAlt)) command = cmMenuRectClear;
        else if (matches('H', ctrlAlt)) command = cmMenuRectHide;
        else if (matches('M', ctrlAlt)) command = cmMenuRectMove;
        else if (matches('P', ctrlAlt)) command = cmMenuRectPaste;
        else if (matches('O', ctrlAlt)) command = cmMenuRectDuplicate;
        else if (matches('A', ctrlAlt)) command = cmMenuRectToggleMovePaste;
        else return false;

        switch (command) {
        case cmDelEnd: case cmDelStart: case cmCopy: case cmCut:
            dispatchEditorCommand(command);
            break;
        default:
            runFeature(command);
            break;
        }
        return true;
    }

    void executePrefix(int mode, const TEvent &event) {
        const char key = prefixCharacter(event);
        const bool shifted = (event.keyDown.controlKeyState & kbShift) != 0;
        if (mode == 1) {
            switch (key) {
            case 'A': dispatchEditorCommand(cmReplace); return;
            case 'B': if (hasSelection()) moveCaret(selStart); return;
            case 'C': dispatchEditorCommand(cmTextEnd); return;
            case 'D': dispatchEditorCommand(cmLineEnd); return;
            case 'E': moveCaret(lineMove(curPtr, -curPos.y)); return;
            case 'F': dispatchEditorCommand(cmFind); return;
            case 'H': dispatchEditorCommand(cmDelStart); return;
            case 'K': if (hasSelection()) moveCaret(selEnd); return;
            case 'L': {
                char text[80];
                std::snprintf(text, sizeof(text), "Selection length: %u",
                              hasSelection() ? selEnd - selStart : 0);
                messageBox(text, mfInformation | mfOKButton);
                return;
            }
            case 'M': runFeature(cmPlayMacro); return;
            case 'P': {
                const uint previous = curPtr;
                moveCaret(std::min(lastCursor_, bufLen));
                lastCursor_ = previous;
                return;
            }
            case 'R': dispatchEditorCommand(cmTextStart); return;
            case 'S': dispatchEditorCommand(cmLineStart); return;
            case 'X': moveCaret(lineMove(curPtr, size.y - 1 - curPos.y)); return;
            case 'Y': dispatchEditorCommand(cmDelEnd); return;
            case '[': case ']': runFeature(cmMatchBracket); return;
            default:
                if (key >= '0' && key <= '9' && markSet_[key - '0'])
                    moveCaret(std::min(marks_[key - '0'], bufLen));
                return;
            }
        }

        if (mode == 2 && event.keyDown.keyCode == kbCtrlIns) {
            copyRectangle(true);
            return;
        }

        if (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbShiftTab ||
            key == '\t') {
            indentSelection(shifted || event.keyDown.keyCode == kbShiftTab ? -1 : 1);
            return;
        }
        if (shifted) {
            switch (key) {
            case 'B': setRectangleStart(); return;
            case 'K': setRectangleEnd(); return;
            case 'C': copyRectangle(true); return;
            case 'E': editRectangle(1); return;
            case 'H': rectangleHidden_ = !rectangleHidden_; drawView(); return;
            case 'L': editRectangle(0); return;
            case 'M': case 'V': moveRectangle(); return;
            case 'O': copyRectangle(false); pasteRectangle(); return;
            case 'P': if (moveOnPaste_) moveRectangle(); else pasteRectangle(); return;
            case 'T': copyRectangle(true); editRectangle(0); return;
            case 'A': moveOnPaste_ = !moveOnPaste_; return;
            default: return;
            }
        }
        switch (key) {
        case 'B': beginBlock(); return;
        case 'C': copyBlock(); return;
        case 'H': toggleBlockHidden(); return;
        case 'I': indentSelection(1, 1); return;
        case 'K': endBlock(); return;
        case 'L': selectLine(); return;
        case 'M': changeSelectionCase(SelectionCase::upper); return;
        case 'O': changeSelectionCase(SelectionCase::lower); return;
        case 'R': readBlock(); return;
        case 'T': selectWord(); return;
        case 'U': indentSelection(-1, 1); return;
        case 'V': moveBlock(); return;
        case 'W': writeBlock(); return;
        case 'Y': cutSelectedBlock(); return;
        default:
            if (key >= '0' && key <= '9') {
                marks_[key - '0'] = curPtr;
                markSet_[key - '0'] = true;
            }
            return;
        }
    }

    void beginBlock() {
        blockStart_ = curPtr;
        blockSelecting_ = true;
        blockHidden_ = false;
        setSelect(curPtr, curPtr, False);
    }

    void endBlock() {
        if (!blockSelecting_)
            return;
        setSelect(std::min(blockStart_, curPtr), std::max(blockStart_, curPtr),
                  Boolean(curPtr < blockStart_));
        blockSelecting_ = false;
        blockHidden_ = false;
    }

    void selectLine() {
        cancelPendingBlockSelection();
        const uint start = lineStart(curPtr);
        setSelect(start, lineEnd(curPtr), Boolean(curPtr == start));
        blockHidden_ = false;
    }

    void selectWord() {
        cancelPendingBlockSelection();
        uint start = curPtr;
        uint end = curPtr;
        if (start > 0 && start == end)
            start = prevWord(start);
        end = nextWord(end);
        setSelect(start, end, False);
        blockHidden_ = false;
    }

    void indentSelection(int direction, int requestedWidth = 0) {
        if (!hasSelection())
            return;
        const uint start = lineStart(selStart);
        const uint end = selEnd;
        std::string changed;
        const int width = requestedWidth > 0 ? requestedWidth : std::max(1, TEditor::tabSize);
        const std::string indent(static_cast<size_t>(width), ' ');
        bool lineBeginning = true;
        for (uint p = start; p < end;) {
            if (lineBeginning && direction > 0)
                changed += indent;
            if (lineBeginning && direction < 0) {
                int removed = 0;
                while (p < end && removed < width && bufChar(p) == ' ') {
                    p = nextChar(p);
                    ++removed;
                }
            }
            if (p >= end) break;
            const char c = bufChar(p);
            changed.push_back(c);
            lineBeginning = c == '\n' || c == '\r';
            p = nextChar(p);
            if (c == '\r' && p < end && bufChar(p) == '\n') {
                changed.push_back('\n');
                p = nextChar(p);
            }
        }
        setSelect(start, end, False);
        insertText(changed.data(), static_cast<uint>(changed.size()), False);
        setSelect(start, start + static_cast<uint>(changed.size()), False);
    }

    void changeSelectionCase(SelectionCase mode) {
        if (!hasSelection()) return;
        const uint start = selStart;
        std::string text;
        size_t letter = 0;
        for (uint p = selStart; p < selEnd; p = nextChar(p)) {
            const unsigned char c = static_cast<unsigned char>(bufChar(p));
            unsigned char changed = c;
            switch (mode) {
            case SelectionCase::lower: changed = static_cast<unsigned char>(std::tolower(c)); break;
            case SelectionCase::upper: changed = static_cast<unsigned char>(std::toupper(c)); break;
            case SelectionCase::invert:
                changed = static_cast<unsigned char>(std::islower(c) ? std::toupper(c) : std::tolower(c));
                break;
            case SelectionCase::alternate:
                if (std::isalpha(c)) {
                    changed = static_cast<unsigned char>((letter++ & 1) ? std::tolower(c) : std::toupper(c));
                }
                break;
            }
            text.push_back(static_cast<char>(changed));
        }
        setSelect(selStart, selEnd, False);
        insertText(text.data(), static_cast<uint>(text.size()), False);
        if (persistentBlocks_)
            setSelect(start, start + static_cast<uint>(text.size()), False);
    }

    void copyBlock() {
        if (!persistentBlocks_ || !hasSelection()) return;
        if (blockHidden_) {
            blockHidden_ = false;
            update(ufView);
            return;
        }
        std::string text;
        for (uint p = selStart; p < selEnd; p = nextChar(p))
            text.push_back(bufChar(p));
        const uint caret = curPtr, oldLength = bufLen;
        selStart = selEnd = caret;
        insertText(text.data(), static_cast<uint>(text.size()), False);
        const uint inserted = bufLen - oldLength;
        setSelect(caret, caret + inserted, False);
        blockHidden_ = false;
        trackCursor(True);
    }

    void moveBlock() {
        if (!persistentBlocks_ || !hasSelection() || blockHidden_ ||
            (curPtr >= selStart && curPtr < selEnd)) return;
        const uint start = selStart, end = selEnd;
        std::string text;
        for (uint p = start; p < end; p = nextChar(p))
            text.push_back(bufChar(p));
        const uint destination = curPtr > end ? curPtr - (end - start) : curPtr;
        setCurPtr(end, 0);
        selStart = start;
        selEnd = end;
        deleteSelect();
        setCurPtr(destination, 0);
        insertText(text.data(), static_cast<uint>(text.size()), False);
        setSelect(destination, destination + static_cast<uint>(text.size()), False);
        blockHidden_ = false;
        trackCursor(True);
    }

    void readBlock() {
        char path[512] = "*.*";
        TView *dialog = TProgram::application->validView(
            new TFileDialog("*.*", "Read block", "~N~ame", fdOpenButton, 120));
        if (!dialog) return;
        dialog->setData(path);
        const bool accepted = TProgram::deskTop->execView(dialog) != cmCancel;
        if (accepted)
            dialog->getData(path);
        TObject::destroy(dialog);
        if (!accepted) return;

        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        if (!file) {
            messageBox("Cannot read the selected block file.", mfError | mfOKButton);
            return;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), {});
        if (file.bad()) {
            messageBox("An error occurred while reading the block file.", mfError | mfOKButton);
            return;
        }
        if (!persistentBlocks_ && hasSelection())
            clipCut();
        insertAtCaret(text.data(), static_cast<uint>(text.size()));
    }

    void replaceSelection() {
        if (!hasSelection()) return;
        const uint start = selStart, end = selEnd;
        setCurPtr(end, 0);
        selStart = start;
        selEnd = end;
        replaceClipboardSelection_ = true;
        TEvent paste{};
        paste.what = evCommand;
        paste.message.command = cmPaste;
        TFileEditor::handleEvent(paste);
        if (!hasSelection()) {
            replaceClipboardSelection_ = false;
            blockHidden_ = false;
        }
    }

    void positionCaretAtSelectionEnd() {
        const uint start = selStart, end = selEnd;
        setCurPtr(end, 0);
        selStart = start;
        selEnd = end;
        update(ufView);
    }

    void cutSelectedBlock() {
        if (!hasSelection()) return;
        if (persistentBlocks_)
            positionCaretAtSelectionEnd();
        clipCut();
    }

    void toggleBlockHidden() {
        if (!hasSelection()) return;
        blockHidden_ = !blockHidden_;
        update(ufView);
    }

    static uint remapBlockEdge(uint edge, uint editStart, uint removed, uint inserted,
                               bool rightAffinity) {
        const uint editEnd = editStart + removed;
        if (edge < editStart || (edge == editStart && !rightAffinity))
            return edge;
        if (edge > editEnd || (edge == editEnd && rightAffinity))
            return edge - removed + inserted;
        return editStart + (rightAffinity ? inserted : 0);
    }

    void restorePersistentBlock(uint start, uint end, uint editStart, uint oldLength) {
        const uint inserted = bufLen > oldLength ? bufLen - oldLength : 0;
        const uint removed = oldLength > bufLen ? oldLength - bufLen : 0;
        start = remapBlockEdge(start, editStart, removed, inserted, true);
        end = remapBlockEdge(end, editStart, removed, inserted, false);
        start = std::min(start, bufLen);
        end = std::min(end, bufLen);
        if (start > end) std::swap(start, end);
        selStart = start;
        selEnd = end;
        selecting = False;
        update(ufView);
        updateCommands();
    }

    void insertAtCaret(const void *text, uint length) {
        if (!persistentBlocks_ || !hasSelection()) {
            insertText(text, length, False);
            return;
        }
        const uint start = selStart, end = selEnd, caret = curPtr, oldLength = bufLen;
        const bool hidden = blockHidden_;
        selStart = selEnd = caret;
        insertText(text, length, False);
        restorePersistentBlock(start, end, caret, oldLength);
        blockHidden_ = hidden;
        update(ufView);
    }

    uint deletionStart(const TEvent &event, uint caret, uint removed, uint lineStartBefore) {
        if (event.what == evCommand) {
            const ushort command = event.message.command;
            if (command == cmBackSpace || command == cmDelWordLeft || command == cmUndo)
                return caret > removed ? caret - removed : 0;
            if (command == cmDelLine)
                return lineStartBefore;
        }
        if (event.what == evKeyDown &&
            (event.keyDown.keyCode == kbBack || event.keyDown.keyCode == kbCtrlBack ||
             event.keyDown.keyCode == kbCtrlU))
            return caret > removed ? caret - removed : 0;
        if (event.what == evKeyDown && event.keyDown.keyCode == kbCtrlY)
            return lineStartBefore;
        return caret;
    }

    bool isSelectedBlockDelete(const TEvent &event) const {
        if (event.what == evCommand)
            return event.message.command == cmCut || event.message.command == cmClear;
        if (event.what != evKeyDown) return false;
        return event.keyDown.keyCode == kbShiftDel || event.keyDown.keyCode == kbCtrlDel;
    }

    bool isDefaultBlockCut(const TEvent &event) const {
        if (event.what == evCommand)
            return event.message.command == cmBackSpace || event.message.command == cmDelChar;
        if (event.what != evKeyDown) return false;
        return event.keyDown.keyCode == kbBack || event.keyDown.keyCode == kbDel;
    }

    bool isShiftSelection(const TEvent &event) const {
        return event.what == evKeyDown && (event.keyDown.controlKeyState & kbShift);
    }

    bool isShiftNavigation(const TEvent &event) const {
        if (event.what != evKeyDown)
            return false;
        const TKey key(event.keyDown);
        if (!(key.mods & kbShift))
            return false;
        switch (key.code) {
        case kbLeft: case kbRight: case kbUp: case kbDown:
        case kbHome: case kbEnd: case kbPgUp: case kbPgDn:
        case kbCtrlLeft: case kbCtrlRight: case kbCtrlHome: case kbCtrlEnd:
        case kbCtrlPgUp: case kbCtrlPgDn:
            return true;
        default:
            return false;
        }
    }

    bool startsMouseSelection(const TEvent &event) const {
        return event.what == evMouseDown && (event.mouse.buttons & mbLeftButton);
    }

    void discardSelectionAnchor() {
        blockHidden_ = false;
        cancelPendingBlockSelection();
        selecting = False;
        setSelect(curPtr, curPtr, False);
    }

    void cancelPendingBlockSelection() {
        blockStart_ = curPtr;
        blockSelecting_ = false;
    }

    void handleBaseEvent(TEvent &event) {
        const bool copyCommand = (event.what == evCommand && event.message.command == cmCopy) ||
            (event.what == evKeyDown && event.keyDown.keyCode == kbCtrlIns);
        if (!persistentBlocks_ && hasSelection() && isDefaultBlockCut(event)) {
            TEvent cut{};
            cut.what = evCommand;
            cut.message.command = cmCut;
            TFileEditor::handleEvent(cut);
            return;
        }
        if (persistentBlocks_ && hasSelection() && isSelectedBlockDelete(event)) {
            positionCaretAtSelectionEnd();
            TFileEditor::handleEvent(event);
            return;
        }
        if (startsMouseSelection(event) || !persistentBlocks_ || !hasSelection() || copyCommand ||
            isShiftSelection(event) || (event.what == evCommand &&
            (event.message.command == cmSelectAll || event.message.command == cmStartSelect))) {
            if (event.what == evCommand && event.message.command == cmSelectAll)
                cancelPendingBlockSelection();
            TFileEditor::handleEvent(event);
            return;
        }
        const uint start = selStart, end = selEnd, caret = curPtr, oldLength = bufLen;
        const uint lineStartBefore = lineStart(caret);
        const bool hidden = blockHidden_;
        selStart = selEnd = caret;
        selecting = False;
        TFileEditor::handleEvent(event);
        if (hasSelection() && selecting) {
            blockHidden_ = false;
            return;
        }
        const uint removed = oldLength > bufLen ? oldLength - bufLen : 0;
        restorePersistentBlock(start, end,
                               deletionStart(event, caret, removed, lineStartBefore), oldLength);
        blockHidden_ = hidden;
        update(ufView);
    }

    void writeBlock() {
        if (!hasSelection()) return;
        char path[512]{};
        TView *dialog = TProgram::application->validView(
            new TFileDialog("*.*", "Write block", "~N~ame", fdOKButton, 121));
        if (!dialog) return;
        if (TProgram::deskTop->execView(dialog) == cmOK) {
            dialog->getData(path);
            std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
            for (uint p = selStart; file && p < selEnd; p = nextChar(p))
                file.put(bufChar(p));
        }
        TObject::destroy(dialog);
    }

    bool rectangleBounds(int &top, int &bottom, int &left, int &right,
                         uint &start, uint &end) {
        if (!rectangleValid_) return false;
        top = std::min(rectTop_, rectBottom_);
        bottom = std::max(rectTop_, rectBottom_);
        left = std::min(rectLeft_, rectRight_);
        right = std::max(rectLeft_, rectRight_);
        if (left == right) return false;
        start = lineOffset(top);
        end = lineEnd(lineOffset(bottom));
        return true;
    }

    void setRectangleStart() {
        rectTop_ = rectBottom_ = lineNumber(curPtr);
        rectLeft_ = rectRight_ = charPos(lineStart(curPtr), curPtr);
        rectangleValid_ = false;
        rectangleHidden_ = false;
        drawView();
    }

    void setRectangleEnd() {
        rectBottom_ = lineNumber(curPtr);
        rectRight_ = charPos(lineStart(curPtr), curPtr);
        rectangleValid_ = rectTop_ != rectBottom_ || rectLeft_ != rectRight_;
        rectangleHidden_ = false;
        drawView();
    }

    std::string rectangleText() {
        int top, bottom, left, right;
        uint start, end;
        if (!rectangleBounds(top, bottom, left, right, start, end)) return {};
        std::string result;
        for (int row = top; row <= bottom; ++row) {
            const uint rowStart = lineOffset(row);
            const uint rowEnd = lineEnd(rowStart);
            const uint from = std::min(charPtr(rowStart, left), rowEnd);
            const uint to = std::min(charPtr(rowStart, right), rowEnd);
            int column = charPos(rowStart, from);
            while (column < left) { result.push_back(' '); ++column; }
            for (uint p = from; p < to; p = nextChar(p))
                result.push_back(bufChar(p));
            while (column++ < right && to == rowEnd)
                result.push_back(' ');
            if (row < bottom) result.push_back('\n');
        }
        return result;
    }

    void copyRectangle(bool toSystemClipboard) {
        rectClipboard_ = rectangleText();
        if (!toSystemClipboard || !TEditor::clipboard) return;
        TEditor *clip = TEditor::clipboard;
        clip->setBufLen(0);
        clip->setCurPtr(0, 0);
        if (!rectClipboard_.empty())
            clip->insertText(rectClipboard_.data(), static_cast<uint>(rectClipboard_.size()), False);
    }

    void editRectangle(int operation) {
        int top, bottom, left, right;
        uint start, end;
        if (!rectangleBounds(top, bottom, left, right, start, end)) return;
        std::string changed;
        for (int row = top; row <= bottom; ++row) {
            const uint rowStart = lineOffset(row);
            const uint rowEnd = lineEnd(rowStart);
            const uint from = std::min(charPtr(rowStart, left), rowEnd);
            const uint to = std::min(charPtr(rowStart, right), rowEnd);
            for (uint p = rowStart; p < from; p = nextChar(p)) changed.push_back(bufChar(p));
            if (operation == 1) {
                for (int column = left; column < right; ++column) changed.push_back(' ');
            } else if (operation == 2 || operation == 3) {
                for (uint p = from; p < to; p = nextChar(p)) {
                    const unsigned char c = static_cast<unsigned char>(bufChar(p));
                    changed.push_back(static_cast<char>(operation == 2 ? std::toupper(c) : std::tolower(c)));
                }
            }
            for (uint p = to; p < rowEnd; p = nextChar(p)) changed.push_back(bufChar(p));
            if (row < bottom) {
                const uint next = nextLine(rowStart);
                for (uint p = rowEnd; p < next; p = nextChar(p)) changed.push_back(bufChar(p));
            }
        }
        setSelect(start, end, False);
        insertText(changed.data(), static_cast<uint>(changed.size()), False);
    }

    void pasteRectangle() {
        if (rectClipboard_.empty() && TEditor::clipboard) {
            rectClipboard_.reserve(TEditor::clipboard->bufLen);
            for (uint p = 0; p < TEditor::clipboard->bufLen; ++p)
                rectClipboard_.push_back(TEditor::clipboard->bufChar(p));
        }
        if (rectClipboard_.empty()) return;
        const int top = lineNumber(curPtr);
        const int column = charPos(lineStart(curPtr), curPtr);
        std::istringstream input(rectClipboard_);
        std::string rowText;
        int row = 0;
        while (std::getline(input, rowText)) {
            uint rowStart = lineOffset(top + row);
            if (rowStart >= bufLen && rowStart != 0) break;
            const uint rowEnd = lineEnd(rowStart);
            const uint position = std::min(charPtr(rowStart, column), rowEnd);
            setCurPtr(position, 0);
            int currentColumn = charPos(rowStart, position);
            while (currentColumn++ < column) insertText(" ", 1, False);
            if (!rowText.empty()) insertText(rowText.data(), static_cast<uint>(rowText.size()), False);
            ++row;
        }
        trackCursor(True);
    }

    void moveRectangle() {
        const int destinationLine = lineNumber(curPtr);
        const int destinationColumn = charPos(lineStart(curPtr), curPtr);
        copyRectangle(false);
        if (rectClipboard_.empty()) return;
        const int top = std::min(rectTop_, rectBottom_);
        const int bottom = std::max(rectTop_, rectBottom_);
        editRectangle(0);
        const int row = destinationLine > bottom ? destinationLine - (bottom - top + 1) : destinationLine;
        setCurPtr(std::min(lineOffset(std::max(1, row)), bufLen), 0);
        setCurPtr(std::min(charPtr(lineStart(curPtr), destinationColumn), bufLen), 0);
        pasteRectangle();
    }

    struct SnippetUndo {
        uint start = 0;
        uint caret = 0;
        std::string removed;
        std::string inserted;
        bool valid = false;
    };

    static constexpr uint invalidPosition = std::numeric_limits<uint>::max();
    static constexpr size_t maxMacroEvents = 4096;

    static bool isUndo(const TEvent &event) {
        return event.what == evCommand && event.message.command == cmUndo;
    }

    std::string bufferText() {
        std::string text;
        text.reserve(bufLen);
        for (uint i = 0; i < bufLen; ++i)
            text.push_back(bufChar(i));
        return text;
    }

    void saveSnippetUndo(const std::string &before, uint caret) {
        const std::string after = bufferText();
        if (before == after)
            return;
        size_t start = 0;
        while (start < before.size() && start < after.size() && before[start] == after[start])
            ++start;
        size_t oldEnd = before.size();
        size_t newEnd = after.size();
        while (oldEnd > start && newEnd > start && before[oldEnd - 1] == after[newEnd - 1]) {
            --oldEnd;
            --newEnd;
        }
        snippetUndo_.start = static_cast<uint>(start);
        snippetUndo_.caret = caret;
        snippetUndo_.removed = before.substr(start, oldEnd - start);
        snippetUndo_.inserted = after.substr(start, newEnd - start);
        snippetUndo_.valid = true;
    }

    bool canUndoSnippet() {
        if (!snippetUndo_.valid || snippetUndo_.start + snippetUndo_.inserted.size() > bufLen)
            return false;
        for (size_t i = 0; i < snippetUndo_.inserted.size(); ++i)
            if (bufChar(snippetUndo_.start + static_cast<uint>(i)) != snippetUndo_.inserted[i])
                return false;
        return true;
    }

    void undoSnippet() {
        const SnippetUndo undo = std::move(snippetUndo_);
        snippetUndo_ = {};
        setSelect(undo.start, undo.start + static_cast<uint>(undo.inserted.size()), False);
        if (undo.removed.empty())
            deleteSelect();
        else
            insertText(undo.removed.data(), static_cast<uint>(undo.removed.size()), False);
        setCurPtr(std::min<uint>(undo.caret, bufLen), 0);
        trackCursor(True);
        updateCommands();
        tokensValid_ = false;
    }

    static bool isEnter(const TEvent &event) {
        return (event.what == evCommand && event.message.command == cmNewLine) ||
               (event.what == evKeyDown && event.keyDown.keyCode == kbEnter);
    }

    static bool isCloseBrace(const TEvent &event) {
        return event.what == evKeyDown &&
               ((event.keyDown.textLength == 1 && event.keyDown.text[0] == '}') ||
                event.keyDown.charScan.charCode == '}');
    }

    static bool recordable(const TEvent &event) {
        if (event.what == evKeyDown)
            return true;
        if (event.what != evCommand)
            return false;
        const ushort command = event.message.command;
        return (command >= cmCharLeft && command <= cmEncoding) ||
               command == cmCut || command == cmPaste || command == cmUndo ||
               command == cmClear || command == cmSelectAll;
    }

    void refreshWindowTitle() {
        if (auto *window = dynamic_cast<TWindow *>(owner))
            if (window->frame)
                window->frame->drawView();
    }

    bool insertDedentedCloseBrace() {
        if (hasSelection())
            return false;
        ensureTokens();
        const uint begin = lineStart(curPtr);
        uint p = begin;
        int column = 0;
        while (p < curPtr) {
            const char c = bufChar(p);
            if (c != ' ' && c != '\t')
                return false;
            const int tab = std::max(1, TEditor::tabSize);
            column += c == '\t' ? tab - (column % tab) : 1;
            p = nextChar(p);
        }
        if (!column)
            return false;
        const int tab = std::max(1, TEditor::tabSize);
        const int target = std::max(0, column - tab);
        p = curPtr;
        int newColumn = column;
        while (p > begin && newColumn > target) {
            const uint previous = prevChar(p);
            const char c = bufChar(previous);
            const int width = c == '\t' ? tab - ((newColumn - 1) % tab) : 1;
            newColumn = std::max(0, newColumn - width);
            p = previous;
        }
        if (p >= curPtr)
            return false;
        setSelect(p, curPtr, False);
        insertText("}", 1, False);
        trackCursor(True);
        return true;
    }

    void indentAfterOpenBrace() {
        ensureTokens();
        uint p = curPtr;
        while (p > 0) {
            const uint previous = prevChar(p);
            const char c = bufChar(previous);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                p = previous;
                continue;
            }
            if (c == '{' && previous < tokens_.size() && tokens_[previous] == tokenNormal) {
                const std::string indentation(static_cast<size_t>(std::max(1, TEditor::tabSize)), ' ');
                insertAtCaret(indentation.data(), static_cast<uint>(indentation.size()));
                trackCursor(True);
            }
            break;
        }
    }

    static char matchingOpen(char c) {
        switch (c) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return 0;
        }
    }

    static char matchingClose(char c) {
        switch (c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default: return 0;
        }
    }

    uint findMatchingBracket(uint position) {
        ensureTokens();
        if (position >= bufLen || position >= tokens_.size() || tokens_[position] != tokenNormal)
            return invalidPosition;
        const char bracket = bufChar(position);
        if (const char close = matchingClose(bracket)) {
            std::vector<char> expected{close};
            for (uint p = nextChar(position); p < bufLen; p = nextChar(p)) {
                if (p >= tokens_.size() || tokens_[p] != tokenNormal)
                    continue;
                const char c = bufChar(p);
                if (const char nested = matchingClose(c)) {
                    expected.push_back(nested);
                } else if (matchingOpen(c)) {
                    if (expected.back() == c) {
                        expected.pop_back();
                        if (expected.empty())
                            return p;
                    }
                }
            }
        } else if (const char open = matchingOpen(bracket)) {
            std::vector<char> expected{open};
            for (uint p = position; p > 0;) {
                p = prevChar(p);
                if (p >= tokens_.size() || tokens_[p] != tokenNormal)
                    continue;
                const char c = bufChar(p);
                if (const char nested = matchingOpen(c)) {
                    expected.push_back(nested);
                } else if (matchingClose(c)) {
                    if (expected.back() == c) {
                        expected.pop_back();
                        if (expected.empty())
                            return p;
                    }
                }
            }
        }
        return invalidPosition;
    }

    void updateMatchingBracket() {
        const uint oldOpen = matchingOpen_;
        const uint oldClose = matchingClose_;
        matchingOpen_ = matchingClose_ = matchingTarget_ = invalidPosition;
        const auto isBracket = [](char c) { return matchingOpen(c) || matchingClose(c); };
        uint candidate = invalidPosition;
        if (curPtr < bufLen && isBracket(bufChar(curPtr)))
            candidate = curPtr;
        else if (curPtr > 0 && isBracket(bufChar(prevChar(curPtr))))
            candidate = prevChar(curPtr);
        if (candidate < bufLen) {
            const uint match = findMatchingBracket(candidate);
            if (match != invalidPosition) {
                if (matchingOpen(bufChar(candidate))) {
                    matchingOpen_ = match;
                    matchingClose_ = candidate;
                    matchingTarget_ = match;
                } else {
                    matchingOpen_ = candidate;
                    matchingClose_ = match;
                    matchingTarget_ = match;
                }
            }
        }
        if (oldOpen != matchingOpen_ || oldClose != matchingClose_)
            drawView();
    }

    uint lineOffset(int line) {
        uint position = 0;
        for (int current = 1; line > 0 && current < line && position < bufLen; ++current)
            position = nextLine(position);
        return line > 0 ? lineStart(position) : std::numeric_limits<uint>::max();
    }

    int lineNumber(uint position) {
        int line = 1;
        for (uint i = 0; i < position && i < bufLen; ++i) {
            const char c = bufChar(i);
            if (c == '\n' || (c == '\r' && (i + 1 >= bufLen || bufChar(i + 1) != '\n')))
                ++line;
        }
        return line;
    }

    bool isBreakpointLine(uint position) {
        return breakpointLines_.count(lineNumber(position)) != 0;
    }

    static bool mayChangeText(const TEvent &event) {
        if (event.what == evKeyDown) {
            const auto &key = event.keyDown;
            const unsigned char c = key.charScan.charCode;
            return key.textLength != 0 || c == 8 || c == 9 || c == 10 || c == 13 || c == 127;
        }
        if (event.what != evCommand)
            return false;
        switch (event.message.command) {
        case cmCut:
        case cmPaste:
        case cmUndo:
        case cmClear:
        case cmReplace:
        case cmSearchAgain:
        case cmNewLine:
        case cmBackSpace:
        case cmDelChar:
        case cmDelWord:
        case cmDelWordLeft:
        case cmDelStart:
        case cmDelEnd:
        case cmDelLine:
            return true;
        default:
            return false;
        }
    }

    static TColorAttr colorFor(unsigned char token) {
        IDETheme &theme = ideTheme();
        switch (token) {
        case tokenKeyword: return theme.keyword;
        case tokenComment: return theme.comment;
        case tokenString: return theme.stringLiteral;
        case tokenNumber: return theme.number;
        case tokenPreprocessor: return theme.preprocessor;
        default: return theme.normalText;
        }
    }

    void ensureTokens() {
        if (tokensValid_ && tokens_.size() == bufLen)
            return;
        if (!isCppFile(fileName)) {
            tokens_.clear();
            tokensValid_ = true;
            return;
        }
        // ponytail: rescan the buffer after edits; large files can use per-line state checkpoints.
        tokens_.assign(bufLen, tokenNormal);
        bool inBlockComment = false;
        const auto mark = [this](uint begin, uint end, unsigned char token) {
            end = std::min<uint>(end, static_cast<uint>(tokens_.size()));
            for (uint i = begin; i < end; ++i)
                tokens_[i] = token;
        };

        uint lineStart = 0;
        while (lineStart < bufLen) {
            uint lineEnd = lineStart;
            while (lineEnd < bufLen && bufChar(lineEnd) != '\r' && bufChar(lineEnd) != '\n')
                ++lineEnd;
            uint first = lineStart;
            while (first < lineEnd && (bufChar(first) == ' ' || bufChar(first) == '\t'))
                ++first;
            const bool directive = first < lineEnd && bufChar(first) == '#';
            if (directive)
                mark(first, lineEnd, tokenPreprocessor);

            uint p = lineStart;
            while (p < lineEnd) {
                const char c = bufChar(p);
                const char next = p + 1 < lineEnd ? bufChar(p + 1) : '\0';
                if (inBlockComment) {
                    const uint begin = p;
                    while (p < lineEnd) {
                        if (bufChar(p) == '*' && p + 1 < lineEnd && bufChar(p + 1) == '/') {
                            p += 2;
                            inBlockComment = false;
                            break;
                        }
                        ++p;
                    }
                    mark(begin, p, tokenComment);
                    continue;
                }
                if (c == '/' && next == '/') {
                    mark(p, lineEnd, tokenComment);
                    break;
                }
                if (c == '/' && next == '*') {
                    const uint begin = p;
                    p += 2;
                    inBlockComment = true;
                    while (p < lineEnd) {
                        if (bufChar(p) == '*' && p + 1 < lineEnd && bufChar(p + 1) == '/') {
                            p += 2;
                            inBlockComment = false;
                            break;
                        }
                        ++p;
                    }
                    mark(begin, p, tokenComment);
                    continue;
                }
                if (directive) {
                    ++p;
                    continue;
                }
                if (c == '"' || c == '\'') {
                    const uint begin = p++;
                    const char quote = c;
                    bool escaped = false;
                    while (p < lineEnd) {
                        const char current = bufChar(p++);
                        if (escaped) {
                            escaped = false;
                            continue;
                        }
                        if (current == '\\') {
                            escaped = true;
                            continue;
                        }
                        if (current == quote)
                            break;
                    }
                    mark(begin, p, tokenString);
                    continue;
                }
                const auto byte = static_cast<unsigned char>(c);
                if ((byte >= '0' && byte <= '9') ||
                    (c == '.' && next >= '0' && next <= '9')) {
                    const uint begin = p++;
                    while (p < lineEnd) {
                        const auto digit = static_cast<unsigned char>(bufChar(p));
                        if (!identifierContinue(digit) && bufChar(p) != '.')
                            break;
                        ++p;
                    }
                    mark(begin, p, tokenNumber);
                    continue;
                }
                if (identifierStart(byte)) {
                    const uint begin = p++;
                    while (p < lineEnd && identifierContinue(static_cast<unsigned char>(bufChar(p))))
                        ++p;
                    std::string word;
                    word.reserve(p - begin);
                    for (uint i = begin; i < p; ++i)
                        word.push_back(bufChar(i));
                    if (cppKeywords().count(word) != 0)
                        mark(begin, p, tokenKeyword);
                    continue;
                }
                ++p;
            }

            lineStart = lineEnd;
            if (lineStart < bufLen && bufChar(lineStart) == '\r')
                ++lineStart;
            if (lineStart < bufLen && bufChar(lineStart) == '\n')
                ++lineStart;
        }
        tokensValid_ = true;
    }

    std::vector<unsigned char> tokens_;
    std::vector<TEvent> macroEvents_;
    std::unordered_set<int> breakpointLines_;
    SnippetUndo snippetUndo_;
    std::string rectClipboard_;
    uint marks_[10]{};
    bool markSet_[10]{};
    uint blockStart_ = 0;
    uint lastCursor_ = 0;
    int prefixMode_ = 0;
    int prefixPage_ = 0;
    bool blockSelecting_ = false;
    bool persistentBlocks_ = false;
    bool blockHidden_ = false;
    bool replaceClipboardSelection_ = false;
    bool rectangleValid_ = false;
    bool rectangleHidden_ = false;
    bool moveOnPaste_ = false;
    int rectTop_ = 1;
    int rectBottom_ = 1;
    int rectLeft_ = 0;
    int rectRight_ = 0;
    uint matchingOpen_ = invalidPosition;
    uint matchingClose_ = invalidPosition;
    uint matchingTarget_ = invalidPosition;
    bool tokensValid_ = false;
    bool recording_ = false;
    bool replaying_ = false;
    uint diagnosticLineStart_ = std::numeric_limits<uint>::max();
    uint executionLineStart_ = std::numeric_limits<uint>::max();
};
} // namespace

void setEditorDiagnostic(TFileEditor *editor, int line) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        syntaxEditor->setDiagnosticLine(line);
}

void setEditorDebugState(TFileEditor *editor, const std::vector<int> &breakpoints,
                         int executionLine) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        syntaxEditor->setDebugState(breakpoints, executionLine);
}

SyntaxEditWindow::SyntaxEditWindow(const TRect &bounds, TStringView fileName, int number)
    : TWindowInit(&TWindow::initFrame), TEditWindow(bounds, fileName, number) {
    TFileEditor *oldEditor = editor;
    const TRect editorBounds = oldEditor->getBounds();
    TScrollBar *horizontal = oldEditor->hScrollBar;
    TScrollBar *vertical = oldEditor->vScrollBar;
    TIndicator *indicator = oldEditor->indicator;
    const std::string editorFileName = oldEditor->fileName;
    remove(oldEditor);
    TObject::destroy(oldEditor);
    auto *syntaxEditor = new SyntaxEditor(editorBounds, horizontal, vertical, indicator,
                                          editorFileName);
    editor = syntaxEditor;
    insert(syntaxEditor);
}

const char *SyntaxEditWindow::getTitle(short maxSize) {
    const char *base = TEditWindow::getTitle(maxSize);
    if (!isEditorFeatureRecording(editor))
        return base;
    titleBuffer_ = base;
    titleBuffer_ += " [REC]";
    return titleBuffer_.c_str();
}

bool runEditorFeature(TFileEditor *editor, ushort command) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        return syntaxEditor->runFeature(command);
    return false;
}

bool isEditorFeatureRecording(TFileEditor *editor) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        return syntaxEditor->recording();
    return false;
}

bool editorSupportsPrefixKeys(TFileEditor *editor) {
    return dynamic_cast<SyntaxEditor *>(editor) != nullptr;
}

void setDefaultPersistentBlocks(bool enabled) {
    defaultPersistentBlocks = enabled;
}

void setEditorPersistentBlocks(TFileEditor *editor, bool enabled) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        syntaxEditor->setPersistentBlocks(enabled);
}

bool editorPersistentBlocks(TFileEditor *editor) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        return syntaxEditor->persistentBlocks();
    return false;
}

void moveEditorCursor(TFileEditor *editor, uint position) {
    if (auto *syntaxEditor = dynamic_cast<SyntaxEditor *>(editor))
        syntaxEditor->moveCaret(position);
    else if (editor)
        editor->setCurPtr(position, 0);
}
