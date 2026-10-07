# Help databases and authoring

## Format and strategy

TurboIDE pins `magiblot/tvision` at `b4831e2ca16652db327fb7cc964f1c8c2d512524`.
That revision includes `THelpViewer`, `THelpWindow`, `THelpFile`, and the TVHC
example compiler. Turbo Vision help uses the `FBHF` streamable binary format:
integer context IDs map to serialized `THelpTopic` records through `THelpIndex`.
Each topic contains paragraphs and inline cross-reference offsets. TVHC accepts
`.topic Symbol=ID` sections, `{label:Symbol}` links, wrapping paragraphs and
indented fixed-layout lines. `THelpViewer` already handles Tab, Shift+Tab,
Enter, mouse selection, scrolling, and Esc. It has no Back history or visible
index of its own. TVDemo demonstrates opening a `THelpFile` in a `THelpWindow`.

The supplied `reference/TCHELP.TCH` is Borland THELP version 52, not `FBHF`.
Its typed records include a context map, compressed fixed-screen text, keyword
positions for inline links, an alphabetical index, a nibble table and tags.
The document uses CP437 characters and control bytes for links and code. A
structural parse of this file found 2,857 screens, 2,063 index entries and
13,424 links, with no unresolved destinations among 2,837 unique targets.
THELP screens have no separate title field; a display heading is a useful
title when present.

The historical conversion path is **THELP -> TVHC source -> FBHF**. A standalone C++17 tool
decodes the original records and emits TVHC source with an explicit Contents
topic. The pinned TVHC compiler produces the native database. TurboIDE reads
that database through Turbo Vision; neither the IDE nor the converter requires
Python. The converter accepts an input path, so it does not depend on the
proprietary file being present in a normal build.

## Shipped databases

This is historical Borland compiler documentation. It does not describe the
GCC/GDB toolchain used by TurboIDE. `TCHELP.TCH` and generated TVHC source stay
local. The committed `reference/TCHELP.h32` is the historical input to the
merge; it is not copied to the release unchanged.

TurboIDE-specific documentation is stored as the tracked TVHC source
`docs/help/turboide.txt`. Every normal build compiles it to an intermediate
database, then `tools/merge_help.cpp` copies its topics into the tracked legacy
`reference/TCHELP.h32` database. The release contains one native Turbo Vision
file: `help/tchelp.h32`.

The merger preserves every historical topic, its context ID, and its internal
links. TurboIDE-owned contexts start at 12000 and are copied after the legacy
topics, so they can be opened by F1 through the same file. This permits a new
topic to be added without changing the viewer, file selection, or keyboard
workflow. Duplicate context IDs are a build error: rewriting an existing
legacy topic requires an explicit merger policy change, not an accidental
override in the new source.

`Help → Contents`, `Help → Index`, and editor `Ctrl+F1` remain historical entry
points for now. Once TurboIDE has its own general contents and index, these
commands can be rewritten in the combined database.

## Writing TurboIDE help

TVHC source uses `.topic Name=ContextId` headers. Normal text wraps to the
viewer width. A line that begins with a space is fixed-layout text; use it for
all visible lines in TurboIDE topics. Links use `{visible text:TopicName}`;
TVHC reports unresolved target names while compiling. Begin the source with
`;` for comments.

Match the original Turbo C++ visual language: a title is framed by `▄` above
and `▀` below, lists use `■`, and notices, dialogs, and tables use the same
Unicode pseudographics (`╔═╗`, `┌─┐`, and so on) as the converted legacy
topics. This source is UTF-8 and the native viewer already renders those
characters from the historical database. Do not emit ANSI escape sequences:
`THelpViewer` renders screen cells itself and does not interpret terminal
formatting codes. Describe actual current behaviour and shortcuts, then link
related topics at the end. Do not assign a new context below 12000 or above
16379: the supplied TVHC compiler retains Borland's 16-bit topic-ID limit even
when it writes a `.h32` database.

The generated `.h32` and generated context header stay in the build directory;
only `docs/help/turboide.txt` belongs in Git.

## UI context IDs

TurboIDE uses original Borland IDs as stable UI context IDs. F1 resolves the
focused dialog control, window or menu item through Turbo Vision's `helpCtx`;
a missing topic falls back to Contents (10030).

| TurboIDE element | Context ID |
| --- | ---: |
| File, Edit, Search, Run, Compile, Debug, Project, Options, Window, Help menus | 411–420 |
| Editor, Watches, Messages, Project windows | 402, 403, 405, 409 |
| Find, Replace, Go to Line, Run Parameters, Add Watch dialogs | 562, 566, 568, 572, 590 |
| Editor Options, Colors | 899, 915 |
| TurboIDE native Contents, Jump to Symbol, Back from Symbol, Word Completion, Class Browser | 12000–12004 |

F1 resolves the focused dialog, window, or menu item through its `helpCtx`.
For a selected menu item, Turbo Vision also passes this context to the status
line. `IDEStatusLine::contextHint` supplies the short one-line explanation
shown after `F1 Help`; add an entry there whenever a TurboIDE menu topic is
introduced. This keeps the pointer and keyboard menu paths identical.

`Help → Contents` currently opens legacy Contents; Shift+F1 opens the legacy
alphabetical Index; Ctrl+F1 remains identifier lookup in the editor.

## References

- Pinned Turbo Vision: `include/tvision/help.h`, `include/tvision/helpbase.h`,
  `source/tvision/help.cpp`, `source/tvision/helpbase.cpp`,
  `examples/tvhc/tvhc.cpp`, `examples/tvdemo/tvdemo1.cpp`.
- [THelpViewer THELP parser](https://github.com/mariolacko/THelpViewer/tree/9730fed593dda3a442ac0a874dc6c3b43909e37f/converter/readers)
  and [format notes](https://github.com/mariolacko/THelpViewer/tree/9730fed593dda3a442ac0a874dc6c3b43909e37f/docs/Formats).

## Build and install locally

Run from the repository root in PowerShell. Every `turboide` build compiles
`docs/help/turboide.txt` through TVHC, merges it with the historical database,
and copies the single result to `help/tchelp.h32` beside the executable. The
normal regression check opens that merged result and verifies both legacy and
TurboIDE-owned contexts:

```powershell
cmake -S . -B .build/help -G "Visual Studio 17 2022" -A x64 -DTURBOIDE_BUILD_TESTS=ON
cmake --build .build/help --config Release --target turboide turboide_help_database_check
& .\.build\help\Release\turboide_help_database_check.exe
```

The legacy THELP converter tools remain opt-in. Use this separate procedure
only when changing or investigating `TCHELP.TCH` conversion:

```powershell
cmake -S . -B .build/help -G "Visual Studio 17 2022" -A x64 -DTURBOIDE_BUILD_HELP_TOOLS=ON
cmake --build .build/help --config Release --target tch_to_tvhc help_database_check
& .\.build\help\Release\tch_to_tvhc.exe reference\TCHELP.TCH .build\help\tchelp.txt
& .\.build\help\Release\turboide_tvhc.exe .build\help\tchelp.txt .build\help\tchelp.h32 .build\help\tchelp_ids.h
& .\.build\help\Release\help_database_check.exe .build\help\tchelp.h32
```

In the IDE, **Help → Contents** opens historical Contents. F1 opens the topic
for the focused control or selected menu command and falls back to Contents
when that topic is absent.
In an editor, Ctrl+F1 opens the index entry matching the identifier under the
cursor (or immediately before it), such as `printf`, `malloc`, `sizeof` or
`struct`. The match ignores ASCII letter case. If there is no matching entry,
the IDE reports that no topic was found. Use Tab/Shift+Tab to select links,
Enter to follow, Backspace or Alt+Left to return, and Esc to close. The IDE loads
`help/tchelp.h32` next to `turboide.exe`. `openHelpDatabase` accepts any
native database path and numeric context ID, so future TurboIDE-specific help
can use the same viewer. This converted file starts at context **10030**; the
original Contents context **399** is also present.

## Validation on the supplied file

The C++ converter reported 2,857 source screens, 18 alias contexts, 2,063
alphabetical index entries, 2,904 generated topics, 15,692 links and **0
unresolved links**. Twelve page navigation links were materialized. It removed
989 code control bytes. TVHC produced a 2,695,014-byte `FBHF` file. The native
`help_database_check` loaded contexts 399, 10030, 10031 and 1191, resolved the
Contents links and verified the `#include <stdio.h>` code line without a control
glyph. The existing `gdb_mi_self_check` and `run_interrupt_test` exited 0.

Representative comparisons with the original THELP records:

- Contents (399): all 15 visible lines and 13 original links preserved in order;
  the converter adds one alphabetical-index link.
- DOSERROR example (1191): 39 visible lines after removing code delimiters from
  40 source lines; the `#include`, indentation and four references are retained.
- API category (1450): 33 visible lines and all 18 links retained in order.

In a classic **conhost** window, Contents rendered with the original box layout.
F1, Enter, Backspace, Tab+Enter and Esc opened Help, followed links, went back,
selected another link and returned to the existing editor. A missing file and
the unconverted `.TCH` at the expected `.h32` path both showed error dialogs
without closing the IDE. Scanline Term and Windows Terminal were not tested.

## Known conversion losses

- TVHC requires an initial space for a fixed-layout line. Original lines that
  began at column zero shift one column right in the native viewer.
- THELP's 80×24 screen-specific layout, color attributes and original keyword
  navigation geometry are not represented exactly by TVHC. The native window
  scrolls and can resize instead.
- Code-block control bytes are removed; code text and spacing remain, but
  special code-block styling is not transferred.
- The converter exposes the alphabetical index as linked A–Z/Symbols topics.
  Ctrl+F1 searches these index labels for an exact identifier match. It does
  not provide a type-to-search or full-text search command.
- This tool supports the supplied THELP version 52 only; it rejects other
  versions rather than guessing their record layout.
