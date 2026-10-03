# Retro-Turbo-IDE

A console IDE in the style of **Borland Turbo C**. Its interface is built on
modern Turbo Vision port; user sources are compiled with MinGW-w64 GCC.

The goal is to recreate the experience of Borland’s legendary classic Turbo C IDE from the DOS era in the modern Windows console.
There are some enhancements, such as snippet support and macros, but care was taken to ensure they feel organic—as if, in some parallel universe, Borland still existed and continued developing projects for DOS :)

<img width="1363" height="1008" alt="Screenshot_39" src="https://github.com/user-attachments/assets/53448ab8-f625-4484-a5a1-2c418243c8c7" />

## Building the IDE

From the project directory in PowerShell:

```powershell
cmake -S . -B .build/ide -G "Visual Studio 17 2022" -A x64
cmake --build .build/ide --config Release --target turboide
.\.build\ide\Release\turboide.exe
```

On the first configuration, CMake downloads the Turbo Vision revision pinned in
`CMakeLists.txt` from [magiblot/tvision](https://github.com/magiblot/tvision)
through `FetchContent`. No separate installation or manual include/library setup
is needed; GitHub access is required for the first configuration.

If Turbo Vision has already been downloaded, or if a local checkout is needed,
pass its root directory to CMake through `FETCHCONTENT_SOURCE_DIR_TVISION`:

```powershell
git clone https://github.com/magiblot/tvision.git C:/dev/tvision
cmake -S . -B .build/ide-local -G "Visual Studio 17 2022" -A x64 `
  -DFETCHCONTENT_SOURCE_DIR_TVISION=C:/dev/tvision
cmake --build .build/ide-local --config Release --target turboide
```

Point to the root of the `tvision` repository—the directory containing
`CMakeLists.txt`. TurboIDE applies small compatibility patches to Turbo Vision
sources while CMake configures, so use the revision pinned in `CMakeLists.txt`
or verify a newer version first. Quote paths containing spaces.

## Turbo C++ historical reference

**Help → Contents** opens a temporary historical Borland help database that
CMake copies to `help/tchelp.h32` beside `turboide.exe`. In the editor,
Ctrl+F1 looks up the identifier under the cursor through the database's
alphabetical index. F1 still opens contextual help. Instructions for converting
`TCHELP.TCH`, building TVHC, and installing the `.h32` file are in
[HELP_CONVERSION.md](HELP_CONVERSION.md). Borland documentation describes its
old compiler; TurboIDE uses GCC/GDB.

## GCC compiler

The IDE looks for `gcc.exe` in `PATH`. To provide an explicit path in the
current PowerShell session:

```powershell
$env:TURBOIDE_GCC = "C:\tools\w64devkit\bin\gcc.exe"
.\.build\ide\Release\turboide.exe
```

Use an x64 MinGW-w64 GCC. GCC supports Unicode user names in source paths; the
linker receives a relative ASCII output name inside `.turboide-build`.

### Borland `conio.h` compatibility

The release includes the GCC-adapted `coniow` implementation. TurboIDE adds it
to every user C build, so legacy sources can use either `#include <conio.h>` or
`#include <coniow.h>` without project configuration.

## Build commands

- `Alt+F9` compiles the current saved `.c` file.
- `F9` builds the open project; when no project is loaded, it builds the current
  `.c` file.
- On errors, the Messages window opens automatically. Arrow keys and Space
  track the selected line; Enter navigates to the source and closes the window.
  **F11** opens Project, **Shift+F11** opens Messages, and **F12** opens
  Compiler Messages.
- Before building, modified files can be saved, built from their on-disk
  versions, or the build can be cancelled.
- The result is `.turboide-build\program.exe` in the source or project working
  directory. This directory is excluded from Git.

To create a project, choose **Project → New project...** in the project
directory, then add source files with **Project → Add item...**. The IDE does
not modify an existing `.prj` when creating a project whose name is already in
use. A project file normally lives in the project root; the file dialog can
create or open a project in any accessible directory. The file uses TurboIDE's
text format, not Borland's binary format.

Example UTF-8 project file:

```text
source=src/main.c
source=src/math.c
include=include
define=USE_FAST_MATH
library=user32
```

Paths are relative to the `.prj` directory. `source` may occur more than once;
`include` adds a header directory; `define` is passed as `-D`; and
`library=foo` is passed as `-lfoo` (a complete `-lfoo` argument is accepted as
well). Program arguments use repeated `arg=...` lines; the runtime directory
uses `rundir=...`.

**File → Change dir...** sets TurboIDE's current directory. It is stored in
settings and is the root for file dialogs and new files; it does not prevent
opening a project in a different directory. The IDE stores each project's
session beside its `.prj` in a same-named `.dsk`: open files, window positions,
cursors, active window, breakpoints, and Watch expressions. The session is
restored the next time the project is opened. `.dsk` is TurboIDE's text format;
it is not compatible with FPC binary desktop files.

Builds use `-g -O0 -Wall -Wextra` so the subsequent GDB session can see symbols
and variables.

## Debugging

- **Debug → Start debugging** builds the current file or project and starts GDB.
  Set `TURBOIDE_GDB` to `gdb.exe`, or add it to `PATH`.
- **F4** continues, **F7** steps into, **F8** steps over, **Ctrl+F2** stops
  debugging, and **Ctrl+F8** toggles a breakpoint on the current line.
- **Ctrl+F7** adds a Watch; **Ctrl+F4** reevaluates Watches. In the Watches
  window, **Del** removes the selected expression. Values update whenever the
  debugger stops; TurboIDE prohibits memory writes and explicit register access.
  GDB 9.1+ also blocks function calls, so it supports arbitrary expressions;
  older versions accept only simple variable names.
- Locals and Watches appear in separate windows. When execution stops, the IDE
  shows the source line and values; **Alt+F5** displays the program screen.

## Running and the user screen

- `Ctrl+F9` builds the current files and runs the program.
- The program receives console input/output; after it exits, the IDE restores
  the screen and editor window from which it was launched.
- `Alt+F5` displays the User Screen. Press Enter to open `cmd.exe` there; use
  `exit` to close the shell. `Alt+F5` or Esc returns to the IDE.
- For project arguments, add one `arg=value` per `.prj` line; each value becomes
  one `argv` argument.
