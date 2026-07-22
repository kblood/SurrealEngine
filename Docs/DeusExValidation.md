# Deus Ex validation process

This document defines a repeatable validation process that can run alongside
other Surreal Engine work without opening an audio device or taking foreground
focus. It also prevents concurrent instances from overwriting one shared log.

## Non-interfering launch profile

Use all four options for automated or background checks:

```powershell
build/RelWithDebInfo/SurrealEngine.exe `
  --autostart `
  --nosound `
  --noactivate `
  --logfile=build/deusex-validation.log `
  --url=01_NYC_UNATCOIsland `
  "C:\Program Files (x86)\GOG Galaxy\Games\Deus Ex GOTY"
```

- `--autostart` bypasses the game selector when discovery produces exactly one
  game folder.
- `--nosound` selects the null audio device. It does not initialize OpenAL or
  open an operating-system output device. The log records `Audio disabled by
  --nosound`.
- `--noactivate` creates and shows a window without activation, disables the
  fullscreen path and cursor locking, and does not bring error UI to the front.
  The game still maintains its internal Extension `RootWindow` focus so menus
  and edit controls work.
- `--logfile=<path>` gives each process a separate log, which avoids collisions
  with other concurrent Surreal Engine projects.

On Windows, background menu validation sends targeted window messages rather
than global input. Absolute mouse movement is accepted in `--noactivate` mode
because foreground-only raw mouse events are unavailable. Validation must
compare `GetForegroundWindow()` before and after the run; all Deus Ex save tests
on 2026-07-22 preserved the original foreground handle.

## Crash collection without focus changes

Unhandled native crashes still produce a minidump and log in the standard
Surreal Engine crash-report directory. Under `--noactivate`, the engine does not
start the interactive crash uploader. A symbolized report can be written to a
file without opening a window:

```powershell
build/RelWithDebInfo/SurrealEngine.exe `
  --showcrashreport --noactivate `
  --logfile=build/deusex-crash-stack.log `
  "C:\path\to\report.dmp" "C:\path\to\report.log"
```

This process found two concrete Save Game screen defects:

1. `URootWindow::SetRootFocusWindow` walked past a detached focus hierarchy and
   dereferenced null. Both ancestor walks now stop safely at null.
2. `UObject::DynamicArray` treated an in-place `ScriptArray` as though it were a
   `TypedScriptArray` wrapper. The resulting pointer targeted the element
   property metadata and crashed on the first saved-slot insertion. The helper
   now wraps the actual in-place `ScriptArray` address.

## Current smoke sequence

1. Build `RelWithDebInfo` so minidumps contain useful source locations.
2. Run the three focused CTests: `DXTextTokenizer`, `DXAIPerception`, and
   `DXSavePath`.
3. Start Liberty Island with the non-interfering launch profile.
4. Open the pause menu and Save Game screen through targeted input.
5. Create a numbered save using an edit-control description.
6. Close cleanly, then launch `--url=?loadgame=<slot>`.
7. Confirm the process remains responsive and the restored world and HUD render.
8. Archive generated test saves under ignored `build/test-saves` before testing
   deletion.
9. Run a Release build and the focused tests again before committing.

Generated screenshots, logs, minidumps, and proprietary save packages are test
artifacts. Keep them under ignored build or local application-data directories;
do not commit them.
