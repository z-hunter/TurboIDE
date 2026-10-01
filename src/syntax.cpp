#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TObject
#include "syntax.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

IDETheme::IDETheme()
    : application(cpAppColor, sizeof(cpAppColor) - 1),
      messagesWindow("\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B", 8),
      messagesList("\x01\x01\x01\x01\x01", 5) {}

IDETheme &ideTheme() {
    static IDETheme theme;
    return theme;
}

namespace {
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
public:
    SyntaxEditor(const TRect &bounds, TScrollBar *horizontal, TScrollBar *vertical,
                 TIndicator *indicator, TStringView fileName)
        : TFileEditor(bounds, horizontal, vertical, indicator, fileName) {}

    void handleEvent(TEvent &event) override {
        if (mayChangeText(event))
            tokensValid_ = false;
        TFileEditor::handleEvent(event);
    }

    void formatLine(TDrawBuffer &drawBuffer, uint linePtr, int hScroll, int width,
                    TAttrPair colors) override {
        TFileEditor::formatLine(drawBuffer, linePtr, hScroll, width, colors);
        ensureTokens();
        const bool breakpointLine = isBreakpointLine(linePtr);

        uint p = linePtr;
        int cellPos = 0;
        int x = 0;
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
                const bool selected = selStart <= p && p < selEnd;
                if (!selected && (token != tokenNormal || linePtr == diagnosticLineStart_ ||
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

private:
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
    std::unordered_set<int> breakpointLines_;
    bool tokensValid_ = false;
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
    if (!isCppFile(fileName))
        return;
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
