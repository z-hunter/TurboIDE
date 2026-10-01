# SetsEd: что заимствовать для TurboIDE

Дата: 2026-10-01. Исследован `reference/SetsEd`: standalone-редактор SET's
editor, ориентированный на старый Turbo Vision и Unix/DOS/MinGW. Цель этого
документа — уменьшить объём будущей работы в TurboIDE, не перенося чужую
платформу и не создавая новые подсистемы раньше необходимости.

## Вывод

Права и GPL позволяют нам переносить исходники SetsEd. Ограничение здесь
техническое: полный редактор связан с собственным `TCEditor`, старой
сериализацией Turbo Vision, глобальными объектами и POSIX/shell API. Такой
перенос не будет малым и вернёт в новый Windows-проект тот же монолит, от
которого он сознательно отделён.

Исходники можно и нужно брать там, где модуль действительно изолирован. Для
ядра редактора правильный критерий другой: перенос оправдан только если он
даёт функцию, которой нет в текущем `TFileEditor`, с меньшей ценой, чем
небольшое расширение современной Turbo Vision.

## Сопоставление подсистем

| SetsEd | Что там действительно полезно | Что уже есть в TurboIDE | Решение |
| --- | --- | --- | --- |
| `setedit/runprog.cc` | Единая модель внешнего процесса: старт, отмена, вывод, возврат экрана | `src/build.cpp`, `src/run.cpp`, `src/debugger.cpp` запускают GCC, программу и GDB через `CreateProcessW`, pipes и отдельный Console Screen Buffer | Ничего не переносить. Наша модель корректнее для интерактивной Win32-консоли: SetsEd в основном приостанавливает/возобновляет экран и вызывает shell. |
| `setedit/edmsg.cc` | Сообщение хранит текст, файл, строку, колонку, тип; учитываются continuation lines и текущая папка `make` | `BuildMessage` и `parseMessage` в `src/build.cpp` уже извлекают GCC `file:line:column` | Взять как спецификацию для будущих `make`/CMake build logs. Пока TurboIDE вызывает GCC напрямую, стек `Entering/Leaving directory` и профили парсеров не нужны. |
| `setedit/dskwin.cc`, `dskclose.cc`, `dskmessa.cc` | Сохранять bounds, visibility, cursor/resume и Z-order отдельных окон; при восстановлении обрезать координаты по текущему экрану | `src/desktop_session.cpp` уже сохраняет проектное окно, редакторы, bounds, курсор, active file, breakpoints и watches; `restoreRect` ограничивает окно экраном | Формат `.dsk` TurboIDE оставить. Единственный возможный маленький прирост — сохранить Z-order и visibility проектного/сообщений, когда это станет заметной потерей UX. |
| `mainsrc/loadshl.cc` | Выбор синтаксиса по extension, shebang, modeline и правилам имени | `src/syntax.cpp` корректно ограничен C/C++ расширениями, ради которых создан продукт | Не вводить язык описания подсветки и PCRE-конфигурацию. Расширять механизм только вместе с реальной поддержкой нового языка. |
| `mainsrc/tags.cc` | Индекс символов: чтение tags, переход, completion; обновление, когда исходник новее индекса | В TurboIDE пока нет навигации по символам | Лучший кандидат на следующую редакторскую функцию: запуск установленного Universal Ctags и собственный небольшой reader его стабильного машинного вывода. Не переносить 2146 строк legacy-парсера Exuberant Ctags format 2. |
| `mainsrc/completi.cc` | Хорошая минимальная UX-модель completion: один вариант вставляется сразу; несколько — popup у курсора; набор следующей буквы фильтрует список | Нет провайдера символов | Взять поведение после появления symbol index. Popup должен быть маленьким `TListBox`; отдельный framework completion не нужен. |
| `setedit/intgrep.cc` | Project/search-in-files и переход по `file:line` | Нет отдельного project search | Не переносить: модуль запускает GNU grep через shell и временные файлы. Если функция понадобится, делать явный Win32 process с заданными аргументами либо один простой рекурсивный поиск без shell. |
| `mainsrc/ceditor.cc` | Redo, macros, column selections, block indent, auto-indent, file clipboard и прочие editor power-features | TurboIDE использует готовый современный `TFileEditor` | Не переносить целиком: `TCEditor` — другой базовый editor, а не subclass `TEditor`. Он потребует переноса рендеринга, кодировок, файлового слоя и 20+ зависимых модулей. Отдельные алгоритмы можно брать точечно. |

## Проверка: даст ли перенос TCEditor code folding

**Нет: в SetsEd нет folding исходного текста.** Поиск `fold`/`collapse` в
редакторе приводит к дереву переменных отладчика (`setedit/debug.cc`), а
`cmcExpandCode` в `TCEditor` раскрывает макросы, не сворачивает блоки кода.
Значит, перенос 14 554 строк `ceditor.cc` сам по себе не приблизит нас к
folding.

У `TCEditor` есть полезная для будущего folding архитектурная черта: он сам
владеет gap buffer, индексом длин строк (`LineLengthArray`), отрисовкой строк
и обработкой курсора. Поэтому туда можно встроить visible-line mapping. Но
это ровно причина, по которой его перенос дорог: все эти уровни придётся
сделать совместимыми с текущим Turbo Vision и UTF-8.

Факты, определяющие объём порта:

- `TCEditor` наследуется от SetsEd `TViewPlus`, а не от `TEditor` или
  `TFileEditor`. `TViewPlus` напрямую изменяет байты legacy screen buffer;
  современная Turbo Vision рисует через `TDrawBuffer`.
- Основной код — `ceditor.cc` (14 554 строки), форматирование и syntax
  highlighting — `editorfo.cc` (3 944), плюс `search.cc`, `editwind.cc`,
  `linelen.cc`, `edconst.cc`, `tsindica.cc`, `loadshl.cc` и библиотеки
  `settvuti`/`easydiag`. Список линковки standalone editor в
  `makes/editor.mak` существенно шире этого минимума.
- `TCEditor` работает с `char *` и `TVCodePage`; текущий `TFileEditor`
  поддерживает UTF-8 и использует `nextCharAndPos` для ширины символов.
  Прямое подключение ухудшит уже работающие Unicode-paths и редактор.
- `src/main.cpp` уже имеет много точек, напрямую работающих с
  `TEditWindow`/`TFileEditor`: save, search, session cursor, diagnostics,
  breakpoints и подсветка. Замена потребует адаптера либо переписывания этих
  вызовов.

Итог: **целый TCEditor — отдельный migration-проект, не малый reuse.** Его
следует брать только если мы сознательно выбираем SetsEd editor как новый
долгосрочный фундамент, а не как быстрый путь к folding.

## Короткий путь к folding

Сохранить текущий `SyntaxEditor : TFileEditor` и добавить folding к современной
Turbo Vision. В актуальном `TEditor` `draw()` и `handleEvent()` virtual, но
`lineMove`, `nextLine`, `prevLine` и `setCurPtr` не virtual. Именно через них
видимые строки, scrolling, mouse hit-test и навигация сейчас считают все
логические строки.

Минимальная техническая работа для folding поэтому находится в закреплённой
Turbo Vision, уже патчимую CMake:

1. Сделать необходимый набор line-navigation hooks virtual и добавить
   `FoldEditor`, который держит ranges и mapping visible row → buffer offset.
2. Оставить буфер, undo, UTF-8, file I/O, clipboard и поиск в `TEditor`.
3. В `SyntaxEditor` распознавать C/C++ block ranges тем же проходом, который
   уже вычисляет tokens, и рисовать gutter marker.
4. На закрытии range пропускать строки в mapping; команды вверх/вниз,
   PgUp/PgDn, mouse и scrollbar используют те же hooks.

Это остаётся локальным расширением одного редактора вместо переноса SetsEd
platform. Перед реализацией стоит сделать маленький spike: один fold range,
toggle по команде и проверка cursor/scroll/mouse. Если hooks недостаточны,
тогда честный следующий вариант — локально скопировать и развивать текущие
исходники `TEditor`, а не старый `TCEditor`.

## Что можно перенести из SetsEd напрямую и точечно

| Модуль | Практическая ценность | Решение |
| --- | --- | --- |
| `mainsrc/bufun.cc` | Эвристика «Jump to function» для C с пропуском строк, comments и preprocessor | Можно выделить сканер без UI и старого codepage layer. Но Ctags точнее и покрывает C++; брать, только если нужен zero-dependency jump до Ctags. |
| `mainsrc/completi.cc` | Компактный popup выбора и поведение one-result auto-accept | Можно перенести UX и часть dialog кода после появления источника кандидатов. Сам модуль мал, но опирается на старые collection classes. |
| `mainsrc/ceditor.cc`, `SearchOpenSymbol*` | Нахождение парной скобки с игнорированием comments/strings по syntax attributes | Брать только алгоритм и адаптировать к текущему token map; перенос метода буквально невозможен без private line-edit state TCEditor. |
| `mainsrc/ceditor.cc`, block indent/comment | Нужные редакторские команды | Реализовать поверх текущего буфера, когда пользователь их запросит. Их перенос не связан с folding. |

## Восемь интересующих функций

| Функция | Исходник SetsEd | Реальный объём reuse | Решение |
| --- | --- | --- | --- |
| PMacros: разворачивание по двум символам | `mainsrc/pmacros.cc` (347 строк), `TCEditor::ExpandMacro` | Формат и parser почти самостоятельны: `Trigger: "xx"`, body, режимы, именованные переменные и Lisp-вставки. Само применение связано с буфером `TCEditor`. | **Хороший первый перенос.** Взять parser и семантику триггера; заменить `TStringCollection` на `std::vector`/`std::string`; применить текст через API текущего `TFileEditor`. Начать с literal body, cursor marker и переменных, добавлять режимы только по реальным `.pmc`. |
| Записанные макросы | `ceditor.cc`, `keytrans.cc` | Recorder сохраняет raw key codes и команды в `MacroArray`, replay создаёт синтетические `TEvent`. Это небольшая идея, но не переносимый готовый компонент. | **Небольшая отдельная функция.** Перехватывать пользовательские команды и печатный ввод в `SyntaxEditor::handleEvent`; воспроизводить их тем же маршрутом. Не переносить 1286 строк key translator и старый формат keyboard macros. |
| Скрипты редактора | `sdg/mli.cc`, `sdg/mliedito.cc`, `mainsrc/slpinter.cc` | Старый Lisp-интерпретатор отделён от большей части SetsEd, но bridge `TMLIEditor` напрямую читает `Editor->buffer`, selection, cursor, windows, search и key bindings старого редактора. | **Не переносить. Выбрать Lua.** Lua даёт поддерживаемый C API и позволит открыть только нужные editor-функции, без совместимости с глобальным API SetsEd. `lua.exe` уже есть в GCC toolchain; для линковки всё равно нужны его headers и library. |
| Обычный auto-indent | современный `TEditor::newLine` | Уже есть: при `autoIndent == True` новая строка копирует начальные пробелы предыдущей. | **Ничего не переносить.** Проверить, что опция не отключается в TurboIDE. |
| Intelligent indent | `TCEditor::AnalizeLineForIndent`, `loadshl.cc` (`NLIndent`) | C-подобная логика зависит от line-edit state; декларативные правила `NLIndent` связаны с большим syntax grammar. | **Добавить малый C/C++ вариант отдельно:** сохранить leading whitespace, увеличить после `{`, уменьшить перед `}`. Полный `NLIndent` переносить лишь вместе с совместимым parser подсветки. |
| Подсветка и переход к парной скобке | `TCEditor::SearchMatchOnTheFly`, `SearchOpenSymbolXY`, `SearchCloseSymbolXY` | Алгоритм компактный; SetsEd игнорирует скобки в strings/comments по syntax attributes. | **Лучший точечный reuse.** Адаптировать сканирование к уже существующему `tokens_` в `src/syntax.cpp`, добавить команду jump и атрибуты пары в renderer. |
| Word completion через TAGS | `mainsrc/tags.cc`, `mainsrc/completi.cc` | Логика кандидатов и popup полезны, reader (2146 строк) рассчитан на Exuberant Ctags format 2 и старые collections. | **Делать вместе с Class Browser.** Один новый небольшой index читает стабильный вывод Universal Ctags; popup и поведение SetsEd можно перенести/воссоздать без completion framework. |
| Class Browser через TAGS | `mainsrc/tags.cc`, `TagsClassBrowser` | Там уже есть построение classes, namespaces, parents и members, но структуры и UI legacy. | **Тот же symbol index.** Перенести правила группировки как ориентир, хранить `name/path/line/kind/scope` в современном простом model. Не связывать с legacy tree/collections. |
| Прямоугольные блоки | `TCEditor::selRect*` (от ~5191), custom draw/undo/clipboard | Операции глубоко используют собственный buffer, selection state, renderer и undo `TCEditor`. | **Не является малым переносом.** Простые rectangular copy/paste можно написать как построчные преобразования; видимое прямоугольное выделение и мышь потребуют расширения/форка механизма selection в `TEditor`. Отложить до явной UX-потребности. |
| Настраиваемая подсветка многих языков | `mainsrc/loadshl.cc` (1470), `mainsrc/editorfo.cc` (3944), `cfgfiles/syntaxhl.shl` (150 KB) | Ценны corpus правил и модель: extension/shebang/modeline, comments, strings, keywords, `NLIndent`. Parser и renderer завязаны на однобайтный `TCEditor`, PCRE и старый screen buffer. | **Использовать данные и идеи, не engine.** Для следующего языка добавить короткое descriptor-rule set к текущему lexer. Полную совместимость с `.shl` оправдывает только требование немедленно поддержать весь catalog SetsEd. |

### Порядок, который даёт максимум при малом риске

1. Сопоставление скобок, jump к паре, PMacros и C/C++ smart indent: всё
   расширяет существующий `SyntaxEditor` и не меняет его фундамент.
2. Один TAGS index: сразу даёт completion и Class Browser.
3. Recorder; затем Lua с малым editor API: текст/selection/cursor, commands,
   status и безопасные project queries. Не переносить `.slp` bridge.
4. Rectangular selection и полный declarative highlighter — только при
   подтверждённой потребности, так как оба затрагивают базовую модель редактора.

## Что стоит сделать следующим

### 1. Символьная навигация через внешний Universal Ctags — общий backend для двух функций

SetsEd доказывает ценность связки «индекс проекта → jump to symbol →
completion», но его реализация устарела. Минимальный вариант для TurboIDE:

1. По явной команде пользователя искать `ctags.exe` в `PATH`.
2. Запускать его для открытого проекта в `.turboide-build` или во временном
   файле, не через shell.
3. Загрузить только `name`, `path`, `line` и `kind` в `std::vector`.
4. Добавить Class Browser и word completion из того же индекса.
5. Перестраивать индекс только если source files новее индекса.

Оценка: один небольшой модуль и два UI-входа. Это добавляет реальную
навигацию и completion без парсера C/C++, LSP, базы данных или нового dependency. Функция
должна оставаться optional: если `ctags.exe` отсутствует, IDE сообщает, как
включить её, и продолжает работать.

### 2. Улучшить parser diagnostics только с новым build backend

В `edmsg.cc` полезны две детали: continuation привязывается к предыдущей
диагностике, а `make` меняет текущую директорию через сообщения
Entering/Leaving directory. Это не требуется текущему `runBuild`: он всегда
запускает один GCC с известным `workingDirectory`, и его parser уже понимает
Windows drive letters и `file:line[:column]`.

Когда появится команда «Build with make/CMake», тогда добавить в `BuildMessage`
`severity` и три небольших проверки: GCC с колонкой, сообщение из вложенной
папки make, continuation. Не добавлять настраиваемые compiler profiles.

### 3. Проверка внешнего изменения файла — маленькая независимая защита

SetsEd периодически сравнивает время файла на диске и предупреждает перед
потерей правок. В TurboIDE такой проверки пока нет. Она полезна при генерации
кода, `git checkout` или редактировании файла второй программой.

Реализация не должна копировать SetsEd: при открытии запомнить
`last_write_time`, а перед Save/Build/Run сверить его для изменённого документа.
Если файл изменился извне, показать Reload / Keep editor / Cancel. Это
изолированный шаг и не требует watcher, поток или polling.

## Осознанно исключено

- Двоичные project/desktop formats SetsEd. Наши UTF-8 `.prj` и `.dsk` проще
  просматривать, переживают версии и уже соответствуют проекту.
- Встроенный редактор регулярных выражений, старый translator клавиатурных
  последовательностей, Lisp/`.slp` bridge, MP3, calendar, screen saver, print
  и file clipboard. Они не поддерживают основной цикл C → GCC → GDB. PMacros,
  recorder и Lua рассмотрены отдельно выше.
- Перенос старых PCRE, zlib, bzip2, libamp и остальных vendored библиотек.
  Для текущей цели они не нужны; при реальной потребности использовать
  поддерживаемую системную/пакетную версию с отдельной проверкой лицензии.
- Перенос алгоритма запуска из `runprog.cc`: он строит команду для shell и
  перенаправляет stdout в файлы. TurboIDE намеренно использует аргументы
  `CreateProcessW` и отдельный экран, чтобы не ломать `scanf`, `conio` и
  cursor operations пользовательской программы.

## Источники, проверенные в SetsEd

- Лицензии и границы компонентов: `copyrigh`, разделы «Editor classes»,
  «SETTVUtil», итоговое резюме.
- Process и User Screen: `setedit/runprog.cc` (945 строк).
- Diagnostics: `setedit/edmsg.cc` (834 строки), включая типы `fitInfo`,
  `fitWarning`, `fitError`, `fitCont`.
- Desktop state: `setedit/dskwin.cc`, `dskclose.cc`, `dskmessa.cc`.
- Syntax selection/configuration: `mainsrc/loadshl.cc` (1470 строк).
- Tags и completion: `mainsrc/tags.cc` (2146 строк), `mainsrc/completi.cc`
  (247 строк).
- Editor scope: `mainsrc/ceditor.cc` (14 554 строки).

## Решение

SetsEd — разрешённый source pool для изолированных модулей и алгоритмов, но
не кандидат на перенос `TCEditor` целиком. Для folding сначала сделать spike
на текущем `TFileEditor` с visible-line hooks. Для перечисленных функций
начать с алгоритма парных скобок, PMacros и малой C/C++-логики отступов; затем
взять TAGS как общий backend completion и Class Browser. Для скриптов выбрать
Lua с узким API. Не создавать полную совместимость со старым syntax DSL или
прямоугольным selection engine, пока этого не требуют реальные сценарии.
