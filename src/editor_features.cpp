#define Uses_TApplication
#define Uses_TButton
#define Uses_TDialog
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TEvent
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_MsgBox
#include <tvision/tv.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "editor_features.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct PseudoMacro {
    std::string trigger;
    std::string name;
    std::string body;
    unsigned mode = 0;
};

void loadPseudoMacroFile(const std::filesystem::path &path,
                         std::vector<PseudoMacro> &macros);

std::vector<PseudoMacro> &pseudoMacros() {
    static std::vector<PseudoMacro> macros;
    static bool loaded = false;
    if (loaded)
        return macros;
    loaded = true;

    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return macros;
    const auto config = std::filesystem::path(executable).parent_path() / L"cfgfiles";
    loadPseudoMacroFile(config / L"pmacros.pmc", macros);
    loadPseudoMacroFile(config / L"cpmacros.pmc", macros);
    return macros;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseQuoted(std::string_view line, std::string &value) {
    const auto quote = line.find('"');
    if (quote == std::string_view::npos)
        return false;
    value.clear();
    for (size_t i = quote + 1; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"')
            return true;
        if (c == '\\' && i + 1 < line.size()) {
            switch (line[++i]) {
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'v': c = '\v'; break;
            default: c = line[i]; break;
            }
        }
        value.push_back(c);
    }
    return false;
}

void loadPseudoMacroFile(const std::filesystem::path &path,
                         std::vector<PseudoMacro> &macros) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return;

    PseudoMacro current;
    bool active = false;
    std::string line;
    const auto flush = [&] {
        if (active && current.trigger.size() == 2 && !current.body.empty())
            macros.push_back(std::move(current));
        current = {};
        active = false;
    };

    while (std::getline(input, line)) {
        const auto clean = trim(line);
        if (clean.empty() || clean[0] == ';')
            continue;
        if (clean.rfind("Trigger:", 0) == 0) {
            flush();
            std::string trigger;
            if (parseQuoted(clean, trigger) && trigger.size() == 2) {
                current.trigger = std::move(trigger);
                current.name = current.trigger;
                active = true;
            }
            continue;
        }
        if (!active)
            continue;
        if (clean.rfind("Mode:", 0) == 0) {
            unsigned bit = 0;
            size_t pos = clean.find(':') + 1;
            while (pos < clean.size() && bit < 32) {
                while (pos < clean.size() && (clean[pos] == ' ' || clean[pos] == '\t' || clean[pos] == ','))
                    ++pos;
                if (pos >= clean.size())
                    break;
                if (clean[pos] == '1')
                    current.mode |= (1u << bit);
                if (clean[pos] == '0' || clean[pos] == '1')
                    ++bit;
                ++pos;
            }
            continue;
        }
        if (clean.rfind("Name:", 0) == 0) {
            current.name = trim(clean.substr(5));
            continue;
        }
        if (clean.front() == '"') {
            std::string fragment;
            if (parseQuoted(clean, fragment))
                current.body += fragment;
        }
    }
    flush();
}

class CompletionList : public TListViewer {
public:
    CompletionList(const TRect &bounds, TScrollBar *scrollBar,
                   const std::vector<std::string> &items)
        : TListViewer(bounds, 1, nullptr, scrollBar), items_(items) {
        visible_.reserve(items_.size());
        for (size_t i = 0; i < items_.size(); ++i)
            visible_.push_back(i);
        setRange(static_cast<short>(visible_.size()));
    }

    void getText(char *dest, short item, short maxLen) override {
        if (item < 0 || static_cast<size_t>(item) >= visible_.size()) {
            *dest = 0;
            return;
        }
        const auto &text = items_[visible_[static_cast<size_t>(item)]];
        std::snprintf(dest, static_cast<size_t>(maxLen) + 1, "%s", text.c_str());
    }

    void handleEvent(TEvent &event) override {
        if (event.what == evKeyDown) {
            const ushort key = event.keyDown.keyCode;
            if (key == kbEnter) {
                endModal(cmOK);
                clearEvent(event);
                return;
            }
            if (key == kbEsc) {
                endModal(cmCancel);
                clearEvent(event);
                return;
            }
            if (event.keyDown.textLength == 1) {
                const unsigned char c = static_cast<unsigned char>(event.keyDown.text[0]);
                if (std::isalnum(c) || c == '_' || c >= 0x80) {
                    filter_.push_back(static_cast<char>(c));
                    applyFilter();
                    clearEvent(event);
                    return;
                }
            }
        }
        TListViewer::handleEvent(event);
        if (event.what == evMouseDown && focused >= 0 &&
            (event.mouse.eventFlags & meDoubleClick))
            endModal(cmOK);
    }

    size_t selectedIndex() const {
        return focused >= 0 && static_cast<size_t>(focused) < visible_.size()
            ? visible_[static_cast<size_t>(focused)] : items_.size();
    }

private:
    void applyFilter() {
        visible_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].size() < filter_.size())
                continue;
            bool match = true;
            for (size_t j = 0; j < filter_.size(); ++j)
                if (std::tolower(static_cast<unsigned char>(items_[i][j])) !=
                    std::tolower(static_cast<unsigned char>(filter_[j]))) {
                    match = false;
                    break;
                }
            if (match)
                visible_.push_back(i);
        }
        setRange(static_cast<short>(visible_.size()));
        focusItem(0);
        drawView();
    }

    const std::vector<std::string> &items_;
    std::vector<size_t> visible_;
    std::string filter_;
};

class CompletionPopup : public TGroup {
public:
    CompletionPopup(const TRect &bounds, const std::vector<std::string> &items,
                    size_t visibleRows)
        : TGroup(bounds) {
        TScrollBar *scrollBar = nullptr;
        const int width = size.x;
        if (visibleRows < items.size())
            scrollBar = new TScrollBar(TRect(width - 1, 0, width, size.y));
        list_ = new CompletionList(TRect(0, 0, width - (scrollBar ? 1 : 0), size.y),
                                   scrollBar, items);
        insert(list_);
        list_->select();
        if (scrollBar)
            insert(scrollBar);
    }

    TPalette &getPalette() const override {
        static TPalette palette("\x20\x21\x22\x23\x24\x25\x26\x27"
                                "\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
                                "\x30\x31\x32\x33\x34\x35\x36\x37"
                                "\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F", 32);
        return palette;
    }

    size_t selectedIndex() const { return list_->selectedIndex(); }

private:
    CompletionList *list_;
};

size_t chooseCompletion(TFileEditor *editor, const std::vector<std::string> &items,
                        bool acceptSingle = true) {
    if (items.empty())
        return items.size();
    if (acceptSingle && items.size() == 1)
        return 0;

    TPoint anchor = editor->makeGlobal(editor->cursor);
    TRect desk = TProgram::deskTop->getExtent();
    anchor.x -= TProgram::deskTop->origin.x;
    anchor.y -= TProgram::deskTop->origin.y;
    const int deskWidth = desk.b.x - desk.a.x;
    const int deskHeight = desk.b.y - desk.a.y;
    size_t widest = 0;
    for (const auto &item : items)
        widest = std::max(widest, item.size());
    const int width = std::max(1, std::min<int>(deskWidth - 2,
                                               static_cast<int>(widest) + 2));
    const int x = std::clamp(anchor.x, 0, std::max(0, deskWidth - width));
    const int rows = std::min<int>(static_cast<int>(items.size()),
                                   std::max(1, deskHeight - 2));
    const bool above = anchor.y + rows + 1 >= deskHeight && anchor.y > deskHeight / 2;
    const int y = above ? std::max(0, anchor.y - rows) :
                          std::min(deskHeight - rows, anchor.y + 1);
    auto *popup = new CompletionPopup(TRect(x, y, x + width, y + rows), items,
                                      static_cast<size_t>(rows));
    const ushort result = TProgram::deskTop->execView(popup);
    const size_t selected = result == cmOK ? popup->selectedIndex() : items.size();
    TObject::destroy(popup);
    return selected;
}

struct Variable {
    std::string name;
    std::string defaultValue;
};

bool parseVariable(std::string_view body, size_t at, Variable &variable, size_t &end) {
    if (at + 1 >= body.size() || body[at] != '@' || body[at + 1] != '{')
        return false;
    end = body.find('}', at + 2);
    if (end == std::string_view::npos || end == at + 2 || body[at + 2] == '(')
        return false;
    const auto definition = body.substr(at + 2, end - at - 2);
    const auto separator = definition.find(';');
    variable.name = std::string(definition.substr(0, separator));
    if (separator != std::string_view::npos)
        variable.defaultValue = std::string(definition.substr(separator + 1));
    return true;
}

bool askForVariables(const std::vector<Variable> &variables,
                     std::vector<std::string> &values, const char *title) {
    if (variables.empty())
        return true;
    const int screenWidth = TProgram::deskTop->getExtent().b.x -
                            TProgram::deskTop->getExtent().a.x;
    const int screenHeight = TProgram::deskTop->getExtent().b.y -
                             TProgram::deskTop->getExtent().a.y;
    const int width = std::max(18, std::min(68, screenWidth - 2));
    const int rows = std::min<int>(static_cast<int>(variables.size()),
                                   std::max(1, screenHeight - 6));
    const int height = rows + 5;
    if (variables.size() > static_cast<size_t>(rows)) {
        messageBox("This snippet asks for too many values at once.", mfError | mfOKButton);
        return false;
    }
    auto *dialog = new TDialog(TRect(0, 0, width, height), title);
    dialog->options |= ofCentered;
    std::vector<TInputLine *> inputs;
    const int labelWidth = std::clamp<int>(static_cast<int>(variables.front().name.size()), 6,
                                           std::max(6, width / 3));
    for (int i = 0; i < rows; ++i) {
        const int y = 2 + i;
        const std::string labelText = variables[static_cast<size_t>(i)].name;
        auto *input = new TInputLine(TRect(4 + labelWidth, y, width - 3, y + 1), 255);
        std::vector<char> initial(variables[static_cast<size_t>(i)].defaultValue.begin(),
                                  variables[static_cast<size_t>(i)].defaultValue.end());
        initial.push_back(0);
        input->setData(initial.data());
        dialog->insert(input);
        dialog->insert(new TLabel(TRect(2, y, 4 + labelWidth, y + 1), labelText.c_str(), input));
        inputs.push_back(input);
    }
    dialog->insert(new TButton(TRect(width - 23, height - 3, width - 13, height - 1),
                               "O~K~", cmOK, bfDefault));
    dialog->insert(new TButton(TRect(width - 12, height - 3, width - 2, height - 1),
                               "Cancel", cmCancel, bfNormal));
    dialog->selectNext(False);
    TView *valid = TProgram::application->validView(dialog);
    if (!valid) {
        TObject::destroy(dialog);
        return false;
    }
    const ushort result = TProgram::deskTop->execView(valid);
    if (result != cmOK) {
        TObject::destroy(valid);
        return false;
    }
    values.clear();
    for (size_t i = 0; i < variables.size(); ++i) {
        std::vector<char> value(256, 0);
        inputs[i]->getData(value.data());
        values.emplace_back(value.data());
    }
    TObject::destroy(valid);
    return true;
}

bool expandPseudoMacro(TFileEditor *editor, bool chooseFromList) {
    auto &macros = pseudoMacros();
    if (macros.empty()) {
        messageBox("Pseudo macro files could not be loaded.", mfError | mfOKButton);
        return true;
    }

    std::string typed;
    if (!chooseFromList && editor->curPtr && editor->bufChar(editor->curPtr - 1) < 0x80 &&
        !std::isspace(static_cast<unsigned char>(editor->bufChar(editor->curPtr - 1)))) {
        typed.push_back(editor->bufChar(editor->curPtr - 1));
        if (editor->curPtr > 1 &&
            !std::isspace(static_cast<unsigned char>(editor->bufChar(editor->curPtr - 2))))
            typed.insert(typed.begin(), editor->bufChar(editor->curPtr - 2));
    }

    auto exact = macros.end();
    if (!chooseFromList && typed.size() == 2)
        exact = std::find_if(macros.begin(), macros.end(), [&](const PseudoMacro &macro) {
            return macro.trigger == typed;
        });
    size_t removeTrigger = 0;
    const PseudoMacro *chosen = nullptr;
    if (exact != macros.end()) {
        chosen = &*exact;
        removeTrigger = 2;
    } else {
        std::vector<const PseudoMacro *> candidates;
        for (const auto &macro : macros)
            candidates.push_back(&macro);
        std::vector<std::string> names;
        names.reserve(candidates.size());
        for (const auto *macro : candidates)
            names.push_back(macro->name + " [" + macro->trigger + "]");
        const size_t selected = chooseCompletion(editor, names, false);
        if (selected >= candidates.size())
            return true;
        chosen = candidates[selected];
        removeTrigger = 0;
    }

    std::vector<Variable> variables;
    for (size_t i = 0; i < chosen->body.size(); ++i) {
        if (chosen->body[i] == '@' && i + 1 < chosen->body.size() && chosen->body[i + 1] == '@') {
            ++i;
            continue;
        }
        Variable variable;
        size_t end = 0;
        if (parseVariable(chosen->body, i, variable, end)) {
            variables.push_back(std::move(variable));
            i = end;
        } else if (chosen->body.compare(i, 3, "@{(") == 0) {
            messageBox("This snippet contains an unsupported Lua/Lisp expression.",
                       mfError | mfOKButton);
            return true;
        }
    }
    std::vector<std::string> values;
    if (!askForVariables(variables, values, chosen->name.c_str()))
        return true;

    editor->lock();
    const uint replaceStart = editor->hasSelection() ? editor->selStart :
        editor->curPtr - static_cast<uint>(removeTrigger);
    if (editor->hasSelection() || removeTrigger) {
        if (!editor->hasSelection())
            editor->setSelect(replaceStart, editor->curPtr, False);
        editor->deleteSelect();
        editor->setCurPtr(replaceStart, 0);
    }

    int oldOverwrite = editor->overwrite;
    int oldAutoIndent = editor->autoIndent;
    editor->overwrite = (chosen->mode & 1) != 0;
    editor->autoIndent = (chosen->mode & 2) != 0;
    uint cursorMarker = editor->curPtr;
    size_t variableIndex = 0;
    const auto insertString = [&](std::string_view text) {
        if (!text.empty())
            editor->insertText(text.data(), static_cast<uint>(text.size()), False);
    };

    for (size_t i = 0; i < chosen->body.size(); ++i) {
        const char c = chosen->body[i];
        if (c == '@' && i + 1 < chosen->body.size()) {
            const char marker = chosen->body[i + 1];
            if (marker == '@') {
                insertString("@");
                ++i;
                continue;
            }
            if (marker >= '0' && marker <= '3') {
                if (marker == '0')
                    cursorMarker = editor->curPtr;
                ++i;
                continue;
            }
            Variable variable;
            size_t end = 0;
            if (parseVariable(chosen->body, i, variable, end)) {
                if (variableIndex < values.size())
                    insertString(values[variableIndex]);
                ++variableIndex;
                i = end;
                continue;
            }
        }
        if (c == '\n') {
            editor->newLine();
        } else if (c == '\b') {
            if (editor->curPtr > 0)
                editor->deleteRange(editor->prevChar(editor->curPtr), editor->curPtr, True);
        } else if (c == '\t') {
            const uint line = editor->lineStart(editor->curPtr);
            const int column = editor->charPos(line, editor->curPtr);
            const int tab = std::max(1, TEditor::tabSize);
            const int spaces = tab - (column % tab);
            insertString(std::string(static_cast<size_t>(spaces), ' '));
        } else {
            insertString(std::string_view(&c, 1));
        }
    }
    editor->overwrite = oldOverwrite;
    editor->autoIndent = oldAutoIndent;
    editor->setCurPtr(cursorMarker, 0);
    editor->trackCursor(True);
    editor->unlock();
    editor->drawView();
    return true;
}
} // namespace

bool expandPmacro(TFileEditor *editor, bool chooseFromList) {
    return editor ? expandPseudoMacro(editor, chooseFromList) : false;
}

uint pmacroTriggerStart(TFileEditor *editor) {
    if (!editor || editor->curPtr < 2)
        return std::numeric_limits<uint>::max();
    const char first = editor->bufChar(editor->curPtr - 2);
    const char second = editor->bufChar(editor->curPtr - 1);
    const auto &macros = pseudoMacros();
    for (const auto &macro : macros)
        if (macro.trigger.size() == 2 && macro.trigger[0] == first && macro.trigger[1] == second)
            return editor->curPtr - 2;
    return std::numeric_limits<uint>::max();
}
