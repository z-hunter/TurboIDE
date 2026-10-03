# Команды редактора TurboIDE

Справочник соответствует текущей реализации. Семантика блоков сверена с локальной копией SetsEd; сторонние исходники не включены в Git. `TFPMemo` в FPC сам отключает `efPersistentBlocks`. `TEditor::handleEvent` — базовый диспетчер Turbo Vision; команды TurboIDE проходят через `SyntaxEditor` и `runEditorFeature`. Режимы Ctrl-Q и Ctrl-K доступны во всех файлах редактора. Подсветка синтаксиса остаётся только для C/C++.

## Выделение и режим Persistent blocks

Настройка находится в **Options → Environment → Editor → Persistent blocks**. По умолчанию включена; значение сохраняется как `persistent_blocks=0|1` в `%LOCALAPPDATA%\TurboIDE\settings.ini` и применяется ко всем открытым редакторам. `Default extension` из того же диалога сохраняется как `default_extension` и используется при создании нового файла.

| Поведение | Persistent blocks выключен | Persistent blocks включён |
|---|---|---|
| Движение курсора при выделении | Снимает выделение | Сохраняет блок и двигает курсор отдельно |
| Ввод и Shift+Ins | Заменяют выделенный текст | Вставляют у курсора, блок остаётся |
| Indent / Unindent | Изменяют выделенный блок и сохраняют выделение | То же |
| Backspace / Delete при выделении | Вырезают блок в Clipboard | Удаляют символ у курсора, блок остаётся |
| Ctrl+Ins | Копирует в системный/внутренний Clipboard | Копирует, блок остаётся |
| Shift+Del / Ctrl+Shift+X | Вырезает блок в Clipboard | То же |
| Ctrl+Del | Удаляет блок без копирования | То же |
| Ctrl-K, C / Ctrl-K, V | Недоступны | Копирует или перемещает блок внутри файла без Clipboard |
| Ctrl+Shift+Ins | Явно заменяет выделение содержимым Clipboard | То же; это явная замена вместо вставки у курсора |

## Команды Ctrl-Q

Нажмите Ctrl-Q, затем вторую клавишу. Esc отменяет префикс; `?` переключает страницу подсказки.

| Клавиша | Действие | Внутренняя реализация |
|---|---|---|
| A | Replace | `cmReplace` → диалог замены |
| B / K | Перейти к началу / концу блока | `selStart` / `selEnd`, `moveCaret()` |
| C / R | Конец / начало файла | `cmTextEnd` / `cmTextStart` |
| D / S | Конец / начало строки | `cmLineEnd` / `cmLineStart` |
| E / X | Перемещение на страницу вверх / вниз | `lineMove()` |
| F | Find | `cmFind` → диалог поиска |
| H / Y | Удалить от курсора до начала / конца строки | `cmDelStart` / `cmDelEnd` |
| L | Показать длину выделения | `selEnd - selStart` |
| M | Проиграть записанный макрос | `cmPlayMacro` → `macroEvents_` |
| P | Перейти к предыдущей позиции курсора | `lastCursor_`, `moveCaret()` |
| [ / ] | Перейти к парной скобке | `cmMatchBracket` → `findMatchingBracket()` |
| 0…9 | Перейти к метке | `marks_`, `moveCaret()` |

## Команды Ctrl-K

Нажмите Ctrl-K, затем вторую клавишу. Esc отменяет префикс; `?` переключает страницу подсказки.

| Клавиша | Действие | Внутренняя реализация |
|---|---|---|
| B / K | Начать / закончить выделение блока | `beginBlock()` / `endBlock()` |
| H | Скрыть / показать блок | `toggleBlockHidden()` |
| C | Дублировать видимый блок у курсора; Persistent blocks должен быть включён | `copyBlock()`; без Clipboard; скрытый блок сначала показывается |
| V | Переместить видимый блок к курсору; Persistent blocks должен быть включён | `moveBlock()`; без Clipboard |
| Y | Вырезать блок в Clipboard | `clipCut()` |
| L / T | Выделить строку / слово | `selectLine()` / `selectWord()` |
| I / U | Увеличить / уменьшить отступ на один пробел | `indentSelection(±1, 1)` |
| Tab / Shift+Tab | Увеличить / уменьшить отступ на ширину Tab | `indentSelection(±1)` |
| M / O | Перевести блок в верхний / нижний регистр | `changeSelectionCase()` |
| R / W | Вставить блок из файла / записать блок в файл | `readBlock()` / `writeBlock()` |
| Ctrl+Ins | Скопировать прямоугольный блок | `copyRectangle(true)` |
| Shift+B / K | Начать / закончить прямоугольное выделение | `setRectangleStart()` / `setRectangleEnd()` |
| Shift+C / T | Скопировать / вырезать прямоугольник | `copyRectangle()` / `editRectangle()` |
| Shift+L / E | Удалить прямоугольник / заменить его пробелами | `editRectangle(0/1)` |
| Shift+H | Скрыть / показать прямоугольник | `rectangleHidden_` |
| Shift+M / V | Переместить прямоугольник | `moveRectangle()` |
| Shift+O | Дублировать прямоугольник | `copyRectangle(false)` + `pasteRectangle()` |
| Shift+P | Вставить прямоугольник (или переместить, если включён режим move-on-paste) | `pasteRectangle()` / `moveRectangle()` |
| Shift+A | Переключить move-on-paste | `moveOnPaste_` |
| Shift+Ins | Вставить прямоугольный Clipboard | `pasteRectangle()` |
| 0…9 | Установить метку | `marks_` |

## Прямые клавиши редактора

Базовые команды обрабатываются `TEditor::handleEvent` и командами Turbo Vision из `tvision/editors.h`.

| Клавиша | Команда |
|---|---|
| Ctrl+A | Выделить весь файл (`cmSelectAll`) |
| Ctrl+C / D / E / F | Страница вниз / символ вправо / строка вверх / слово вправо |
| Ctrl+G / H / T / Y | Удалить символ / Backspace / слово / строку |
| Ctrl+I / M | Tab / новая строка |
| Ctrl+J | Перейти к строке (`cmGoToLine`) |
| Ctrl+K / Q | Войти в соответствующий префиксный режим |
| Ctrl+L / R / S / X | Повторить поиск / страница вверх / символ влево / строка вниз |
| Ctrl+O / V | Переключить автоотступ / вставку-замену |
| Ctrl+P | Развернуть точное совпадение триггера перед курсором; если совпадения нет — открыть полный список сниппетов |
| Ctrl+U | Undo |
| Стрелки, Home, End, PgUp, PgDn | Навигация по символам, строкам и страницам |
| Ctrl+Left / Ctrl+Right | Слово влево / вправо |
| Ctrl+Home / Ctrl+End | Начало / конец файла |
| Ctrl+Backspace / Alt+Backspace | Удалить слово слева / Undo |
| Ctrl+Del / Shift+Del | Удалить выделение / вырезать выделение в Clipboard |
| Ctrl+Ins / Shift+Ins | Копировать / вставить Clipboard |
| Alt+[ / Alt+] | Перейти к парной скобке |
| Alt+Shift+Backspace | Redo |

Дополнительные прямые команды выделения: Ctrl+Shift+B/K — начало/конец блока; C — копировать в Clipboard; H — скрыть/показать; X — вырезать; L/T — выделить строку/слово; I/U — увеличить/уменьшить отступ; M/O — верхний/нижний регистр; V — переместить; R/W — чтение/запись блока; Y — удалить до конца строки; Ctrl+Shift+Insert — заменить выделение из Clipboard. Цифры с Ctrl+Shift ставят метки, Ctrl+цифра переходит к метке.

Прямоугольное выделение остаётся доступно отдельными Ctrl+Alt-комбинациями: B/K начало/конец, C копия, T вырезать, L удалить, E очистить пробелами, H скрыть/показать, M переместить, P вставить, O дублировать, A переключить move-on-paste. Эти команды убраны из меню Selection.

## Команды меню Edit

| Пункт | Идентификатор / API |
|---|---|
| Undo, Redo, Cut, Copy, Paste | `cmUndo`, `cmRedo`, `cmCut`, `cmCopy`, `cmPaste` → `SyntaxEditor::handleEvent` |
| Navigation | `cmPageDown`, `cmCharRight`, `cmLineUp`, `cmWordRight`, `cmSearchAgain`, `cmPageUp`, `cmCharLeft`, `cmLineDown`, `cmWordLeft`, `cmGoToLine`, `cmMatchBracket` |
| Delete | `cmDelChar`, `cmBackSpace`, `cmDelWord`, `cmDelWordLeft`, `cmDelLine`, `cmDelStart/End`, `cmClear` → `TFileEditor::handleEvent` |
| Insert | `cmNewLine`, `cmInsMode`, `cmIndentMode`, `cmExpandPmacro`, `cmChoosePmacro` |
| Selection → Select all, Start/End block | `cmSelectAll`, `cmMenuBlockStart/End` → `beginBlock()` / `endBlock()` |
| Selection → Copy/Cut/Hide/Duplicate | `cmCopy`, `cmCut`, `cmMenuHideBlock`, `cmMenuCopyBlock` → `handleBaseEvent()` / методы `SyntaxEditor` |
| Selection → Select line/word, Indent/Unindent, Case | `cmMenuSelectLine/Word`, `cmMenuIndentBlock/UnindentBlock`, `cmMenuUpperCase/LowerCase/InvertCase` → методы `SyntaxEditor` |
| Selection → Replace from Clipboard | `cmMenuReplaceSelect` → `replaceSelection()`; хоткей Ctrl+Shift+Insert (`kbShCtInsert` в SetsEd) |
| Selection → Read/Write block | `cmMenuReadBlock`, `cmMenuWriteBlock` → `readBlock()` / `writeBlock()`; ошибка чтения показывается до изменения текста |
| Record/Stop/Play macro | `cmRecordMacro`, `cmStopMacro`, `cmPlayMacro` → macro event buffer in `SyntaxEditor` |

Пункты Edit отключаются, если фокус не в окне редактора; операции с блоком также отключаются, когда нет подходящего выделения. Duplicate в меню и Ctrl-K, C/V требуют включённого Persistent blocks. `Choose snippet...` принудительно открывает список; Ctrl-P открывает его только при отсутствии точного триггера.

## Где искать код

- Обработка Ctrl-Q/Ctrl-K, persistent-блоков, блока, Clipboard и прямоугольников: `src/syntax.cpp`, класс `SyntaxEditor` (`handleEvent`, `executePrefix`, `handleBaseEvent`, `runFeature`).
- Публичные ID команд редактора: `src/editor_features.h`.
- Меню Edit, обработка команд приложения и включение/отключение пунктов: `src/main.cpp` (`initMenuBar`, `syncEditMenuState`, `handleEvent`).
- Настройка Persistent blocks и пользовательские значения: `src/main.cpp` (`createEditorDialog`, `editEnvironment`), `src/settings.cpp`, `src/settings.h`.
- Базовая реализация Undo/навигации/Clipboard и кодов команд: зависимость Turbo Vision, `tvision/editors.h` и `tvision/teditor1.cpp`.
