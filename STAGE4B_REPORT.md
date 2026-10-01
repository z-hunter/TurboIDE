# Stage 4b — checkpoint report

Status: implementation and local checks completed; **stage acceptance is pending manual terminal checks**.

## Implemented

- Split Locals and Watches into separate windows. Added Watch, Evaluate and Delete commands; each expression reports its own GDB error.
- Disabled inferior function calls (when supported) and writes to memory while evaluating a Watch, then restored those permissions. Explicit register expressions are rejected. Older GDB versions that lack the call guard accept only simple variable names for Watches; a failure to restore permissions stops the debug session.
- Persisted breakpoint locations and Watch expressions in the project's sibling `.dsk`; legacy session records remain readable, and unknown records are ignored. Projects opened from any selected directory keep their `.dsk` beside their `.prj`; File → Change dir remains the root for dialogs and new files.
- Added bounded GDB/MI command waits, bounded stop/cleanup, exit reason/code reporting, source-location diagnostics, and a session-owned Windows Job Object for cleanup of the debuggee and its child processes.
- Added a small nested/escaped GDB/MI parser self-check. Corrected README shortcuts and separated implemented behavior from pending checks in the plans.

## Checks run

- Clean x64 Release configure and build succeeded in `.build/stage4b-release`.
- `gdb_mi_self_check.exe` passed.
- The original MSBuild failure was caused by duplicate `PATH`/`Path` entries in the inherited environment. CMake was run from a Python child process with a normalized environment; no user processes were terminated.

## Still required for acceptance

Manual checks in Scanline Term, conhost and Windows Terminal are still needed for console input, Unicode, resize, Ctrl+C/Ctrl+Break, normal/nonzero/signal exits, missing GDB, closing IDE during debugging, Watch refresh/persistence and screen/input restoration. Until those scenarios pass, Stage 4b is not accepted.
