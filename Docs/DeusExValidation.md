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
  fullscreen path and cursor locking, applies `WS_EX_NOACTIVATE` on Windows,
  and does not bring error UI to the front.
  The game still maintains its internal Extension `RootWindow` focus so menus
  and edit controls work.
- `--logfile=<path>` gives each process a separate log, which avoids collisions
  with other concurrent Surreal Engine projects.

On Windows, background menu validation sends targeted window messages rather
than global input. Absolute mouse movement is accepted in `--noactivate` mode
because foreground-only raw mouse events are unavailable. Validation must
poll `GetForegroundWindow()` during the run, not only compare endpoints. The
game window was never foreground during the final overwrite, reload, and
thumbnail checks on 2026-07-22. Window captures use `PrintWindow`; desktop
captures would record whichever unrelated project remains in front.

An initial implementation applied `SW_SHOWNOACTIVATE` only when showing the
window. A launch-from-zero check revealed that render-device initialization
could make the process foreground before that call (54 of 189 samples in the
reproducing run). `WS_EX_NOACTIVATE` is now applied immediately after native
window creation, before render-device setup, and checked again when the window
is shown. The final startup check recorded zero foreground samples out of 181,
confirmed the style bit, kept the process responsive, and closed it cleanly.

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
2. Run the four focused CTests: `DXTextTokenizer`, `DXAIPerception`,
   `DXSavePath`, and `DXPropertySerialization`.
3. Start Liberty Island with the non-interfering launch profile.
4. Open the pause menu and Save Game screen through targeted input.
5. Create a numbered save using an edit-control description.
6. Close cleanly, then launch `--url=?loadgame=<slot>`.
7. Confirm the process remains responsive and the restored world and HUD render.
8. Reopen Save Game, select the existing row, overwrite it through the stock
   confirmation dialog, close, and reload it in a fresh process.
9. Reopen the overwritten row and inspect its deserialized thumbnail for
   orientation, color, and expected current-scene content.
10. Archive generated test saves under ignored `build/test-saves`, delete the
    slot through the stock confirmation dialog, and verify the loaded process
    remains responsive after the directory disappears.
11. Run a Release build and the focused tests again before committing.

Training progression has its own behavior checklist in
[`DeusExTraining.md`](DeusExTraining.md). Its normal-entry baseline must begin
at `--url=DX`; direct `--url=00_Training` launches are suitable for iterating on
an already isolated blocker.

Generated screenshots, logs, minidumps, and proprietary save packages are test
artifacts. Keep them under ignored build or local application-data directories;
do not commit them.
