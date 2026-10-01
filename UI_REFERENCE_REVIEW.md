# UI reference review: Borland IDE and Free Pascal IDE

Дата: 2026-10-01

## Применённые изменения

- Верхние меню переставлены в привычный Borland порядок: File, Edit, Search, Run, Compile, Debug, Project, Tools, Options, Window, Help.
- Compile содержит Compile (Alt-F9), Make (F9) и Compiler messages (F12). Run содержит Run (Ctrl-F9) и User screen (Alt-F5).
- Tools содержит Messages (F11), переходы по сообщениям Alt-F8 / Alt-F7.
- Нижнее окно Messages занимает семь строк рабочего стола, как в FPC IDE; в нём есть горизонтальная и вертикальная прокрутка.
- Список сообщений использует бирюзовую палитру FPC Browser. Ошибки открывают окно автоматически; после успешной сборки журнал сохраняется скрытым и открывается по F11/F12.
- Перемещение по строкам списка и Space отслеживают позицию в редакторе, Enter переходит к исходнику и скрывает окно. Alt-F8 / Alt-F7 проходят сообщения с исходными координатами по кругу.
- Строка состояния показывает F1 Help, Alt-F8 Next Msg, Alt-F7 Prev Msg, Alt-F9 Compile, F9 Make, F10 Menu.
- Project теперь загружается в отдельное окно списка файлов, а не в редактор. Project содержит New/Open/Close/Add/Delete; Insert добавляет файл, Delete удаляет выбранный файл, Enter открывает его в редакторе. Window → Project возвращает окно списка.
- Popup сборки увеличен до 60×14 ячеек, использует серый Borland-диалог с тенью, таблицу Total/File и нижнюю синюю полосу с бирюзовым сообщением. `Lines compiled` показывает число физических строк исходников проекта (GCC не выдаёт собственный счётчик); после успешной сборки `Program size` показывает размер exe.

FPC IDE отдельно называет `Tools → Messages` (F11) и `Compile → Compiler messages` (F12). В TurboIDE обе команды пока открывают одно окно, поскольку единственный источник сообщений — сборка GCC. FPC показывает Messages в семистрочном нижнем окне с двумя полосами прокрутки. Исходный список отслеживает позицию исходника при смене строки; Enter переходит к исходнику и закрывает окно, а Space оставляет его открытым.

Turbo C User's Guide 2.0 уточняет поведение Alt-F8/F7: они перемещают курсор к следующему/предыдущему сообщению, не требуя открывать Message window; на последнем сообщении Alt-F8 ничего не делает. В TurboIDE сохранена циклическая навигация, но Messages остаётся закрытым, если было закрыто; F11 показывает окно с выделенным последним выбранным сообщением. Project window и команды Open/Close/Add/Delete взяты из Turbo C++ User's Guide 1990: Ins добавляет, Del удаляет, список файлов — отдельное окно. FPC IDE здесь не источник поведения: её User Guide описывает primary file вместо полноценного project manager.

Для локального разбора скопированы исходники Free Pascal IDE в [`reference/FPCSource/packages/ide`](reference/FPCSource/packages/ide). Ревизия, перечень файлов и лицензия указаны в [`reference/FPCSource/README.md`](reference/FPCSource/README.md) и [`reference/FPCSource/LICENSE`](reference/FPCSource/LICENSE).

## Что можно добавить без GDB

| Элемент | Размер | Польза / ограничение |
| --- | --- | --- |
| Маркер текущей ошибки в левом поле редактора | Малый | Закрывает заметный пробел с Borland; курсор уже перемещается к строке диагностики. |
| Маркер строки диагностики в редакторе при отслеживании сообщения | Малый | FPC помечает строку исходника; TurboIDE пока перемещает курсор без отдельного маркера. |
| Умное изменение размера редактора при открытии нижнего Messages окна | Средний | Сейчас окно перекрывает нижнюю часть редактора; Turbo Vision уже умеет Tile/Cascade, потребуется аккуратно ограничить область тайлинга. |
| Список открытых окон в Window меню, Alt+цифра для редакторов | Средний | Пригодится для нескольких исходников; надо назначать и сохранять номера окон. |
| Recent files / recent projects | Средний | Полезно в повседневной работе, но требует сериализации настроек и меню динамических команд. |
| Раздельные окна Messages и Compiler messages | Малый после появления второго источника | Сейчас лишнее: обе команды используют диагностики одной сборки. Разделять при добавлении внешних инструментов/поиска. |

## Лучше делать вместе с GDB

- Toggle breakpoint (Ctrl-F8) с визуальным маркером, переходом на остановленную строку и синхронизацией состояния с GDB.
- Watches / Evaluate (Ctrl-F7 / Ctrl-F4), Call Stack (Ctrl-F3), Breakpoint List и окно GDB/MI.
- F7 Trace into, F8 Step over, F4 Go to Cursor, Alt-F4 Until Return, Ctrl-F2 Program reset.
- Все эти команды уже имеют знакомые места в Debug/Run меню, но полноценные действия зависят от состояния отладочной сессии и GDB/MI. Пока пункты меню остаются заглушками.

## Референсы

- [FPC User Guide, глава 6: IDE](https://www.freepascal.org/docs-html/user/userch6.html) — разделы меню, окон, редактора и отладки.
- [FPC User Guide: Messages window](https://www.freepascal.org/daily/doc/user/usersu62.html) — F11, переход к исходнику и выбор между Enter и Space.
- [FPC User Guide: IDE screen](https://www.freepascal.org/docs-html/user/usersu30.html) — меню, рабочий стол и строка статуса.
- [FPC IDE source](https://github.com/fpc/FPCSource/tree/3e6a1a0dffbeeb0f91a425a558b24036c6a318d7/packages/ide) — локальная копия конкретной ревизии, включая `fpcompil.pas`, `fptools.pas`, `fpviews.pas`, `fpconst.pas` и связанные include-файлы. Используем как поведенческий и визуальный ориентир; исходники сохранены с исходными заголовками и лицензией.
- [Turbo C User's Guide 2.0](https://www.bitsavers.org/pdf/borland/turbo_c/Turbo_C_User_Guide_Ver_2.0_1988.pdf) — описывает Alt-F8/F7 переход по сообщениям.
- [Turbo C++ User's Guide 1990](https://bitsavers.org/pdf/borland/turbo_c/Turbo_C%2B%2B_Users_Guide_1990.pdf) — Project window, Open/Close Project, Add Item, Delete Item, Insert и Delete.

## Проверка

- Release x64 собран после изменений Messages. Первая линковка была заблокирована работающим `turboide.exe`; после его закрытия повторная сборка прошла.
- Release x64 собран после добавления Project window и сериализации списка файлов в `.prj`.
- Исходник подтверждает структуру палитры FPC: `TMessagesWindow` возвращает 12-слотовую `CBrowserWindow`, а `TToolMessageListBox` — `CBrowserListBox` из пяти слотов. В Turbo Vision цвета ребёнка преобразуются через палитру окна; стандартные индексы списка выходили за пределы восьми цветов `wpCyanWindow` и давали `errorAttr` (красный фон). Теперь окно предоставляет 12 валидных cyan-слотов, а список использует FPC-совместимое отображение.
- В закреплённой ревизии FPC нашлись привязки Alt-F8 / Alt-F7 и включение команд при наличии `MessagesWindow`; отдельного обработчика этих команд в исходниках IDE не найдено. В TurboIDE переход реализован явно: начинается с первого/последнего сообщения, пропускает записи без координат и циклически проходит список.
