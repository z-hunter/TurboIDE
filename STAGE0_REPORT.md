# Этап 0 — результат проверки инструментов

Дата: 2026-10-01

## Решение

Для первой версии выбрать GCC как единственный компилятор пользовательского C-кода и GDB как отладчик. Проверенная пара — MinGW-w64 GCC 16.2.0 x64 и GDB 17.2 из переносимого [w64devkit 2.10.0](https://github.com/skeeto/w64devkit). TCC 0.9.27 не показывает локальные переменные даже с новым x64 GDB, поэтому отладка через TCC потребовала бы смены/сборки компилятора и всё равно пока не подтверждена. Не держать два компилятора в v1.

Turbo Vision собран из `magiblot/tvision` commit `b4831e2ca16652db327fb7cc964f1c8c2d512524` с MSVC 19.43 x64, CMake 4.4.2 и Windows SDK 10.0.18362. Собраны `tvedit.exe`, `tvdemo.exe`, `hello.exe` и библиотека. Добавлен минимальный исполняемый прототип `turboide.exe` на CMake + MSVC + Turbo Vision.

## Проверки и вывод

| Проверка | Результат |
|---|---|
| Turbo Vision и примеры | CMake configure/build прошёл; все указанные цели созданы. |
| Прототип в тестовой псевдоконсоли | Показал меню и окно; Alt-X завершил программу с exit code 0 и восстановил консоль. |
| x64 GCC + GDB на C | `break probe.c:4`, `-exec-next` и локальные значения работают; GDB/MI вернул `value=41`, затем `value=42`. |
| GDB/MI жизненный цикл | Breakpoint вызвал `*stopped,reason="breakpoint-hit"`, шаг — `reason="end-stepping-range"`, `-gdb-exit` завершил процесс. |
| Консоль для debuggee | `set new-console on` принимается в GDB/MI, `show new-console` подтверждает настройку. |
| TCC 0.9.27 x64 + GDB 17.2 | На той же отладочной пробе остановка по строке работает, locals отсутствуют; `-gdwarf` установленного TCC продолжает выдавать `.stab`. Отвергнуто для v1. |
| Win32 screen buffers в тестовой псевдоконсоли | `CreateConsoleScreenBuffer` и `SetConsoleActiveScreenBuffer` переключили экран на пользовательский и обратно на IDE. Это основа `Alt+F5`; интеграцию с реальным дочерним процессом надо покрыть при реализации Run. |
| Unicode path с GCC | Входной `.c` с кириллическим именем скомпилировался, если выходной EXE задан относительным ASCII-путём. Абсолютный Unicode output path у установленного linker завершился ошибкой. GDB попадает на строку и читает переменную, но отображает путь в системной кодировке. Не задавать GCC абсолютное имя output с Unicode; полную интеграцию многофайлового проекта проверить при реализации Build/Debug. |
| Windows Terminal | Отдельная ручная проверка не проводилась; ConPTY-псевдоконсоль не заменяет её. |

GCC и GDB запускались из временно распакованного w64devkit по пути `C:\Users\Professional\AppData\Local\Temp\w64devkit-x64-2.10.0\w64devkit\bin`. В репозиторий toolchain не копировать; путь к обоим исполняемым файлам будет задаваться настройкой IDE или находиться через `PATH`.

## Сборка прототипа

```powershell
cmake -S . -B .build/pinned-prototype -G "Visual Studio 17 2022" -A x64
cmake --build .build/pinned-prototype --config Release --target turboide -j 8
```

Файл: `src/main.cpp`. Результат: `.build/pinned-prototype/Release/turboide.exe`.

## Следующий этап

Основа UI: редактор документов, открытие/сохранение, dirty prompt и классическое меню. После неё подключить однопроектный GCC Build; не начинать GDB адаптер заново — использовать MI команды и события, уже проверенные в этой пробе.
