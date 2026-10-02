#pragma once

#define Uses_TFileEditor
#include <tvision/tv.h>

constexpr ushort cmExpandPmacro = 140;
constexpr ushort cmMatchBracket = 141;
constexpr ushort cmRecordMacro = 142;
constexpr ushort cmStopMacro = 143;
constexpr ushort cmPlayMacro = 144;
constexpr ushort cmChoosePmacro = 146;
constexpr ushort cmMenuInsertTab = 147;
constexpr ushort cmMenuBlockEnd = 148;
constexpr ushort cmMenuSelectLine = 149;
constexpr ushort cmMenuSelectWord = 150;
constexpr ushort cmMenuIndentBlock = 151;
constexpr ushort cmMenuUnindentBlock = 152;
constexpr ushort cmMenuUpperCase = 153;
constexpr ushort cmMenuLowerCase = 154;
constexpr ushort cmMenuReadBlock = 155;
constexpr ushort cmMenuMoveBlock = 156;
constexpr ushort cmMenuWriteBlock = 157;
constexpr ushort cmMenuRectStart = 158;
constexpr ushort cmMenuRectEnd = 159;
constexpr ushort cmMenuRectCopy = 160;
constexpr ushort cmMenuRectDelete = 162;
constexpr ushort cmMenuRectClear = 163;
constexpr ushort cmMenuRectHide = 164;
constexpr ushort cmMenuRectMove = 165;
constexpr ushort cmMenuRectPaste = 166;
constexpr ushort cmMenuRectCut = 167;
constexpr ushort cmMenuRectToggleMovePaste = 168;
constexpr ushort cmMenuRectDuplicate = 169;
constexpr ushort cmMenuBlockStart = 170;
constexpr ushort cmMenuReplaceSelect = 171;
constexpr ushort cmMenuHideBlock = 172;
constexpr ushort cmMenuCopyBlock = 173;
constexpr ushort cmMenuInvertCase = 174;
constexpr ushort cmMenuAlternateCase = 175;

bool runEditorFeature(TFileEditor *editor, ushort command);
bool isEditorFeatureRecording(TFileEditor *editor);
bool expandPmacro(TFileEditor *editor, bool chooseFromList = false);
uint pmacroTriggerStart(TFileEditor *editor);
