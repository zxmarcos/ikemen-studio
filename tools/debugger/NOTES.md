# Ikemen GO debugger: state of the work

Handoff notes for whoever continues this work (people or Claude sessions).
[README.md](README.md) describes the debugger for its users.
Last updated 2026-10-25.

## Where things are

Branch `debugger-bridge` of this clone (local only, never pushed), on top of
upstream `develop` (e9bfe112):

| Commit | What |
|---|---|
| 2383ca8f | Engine side: `src/script_debugger.go` (new, ~210 lines), and one or two lines each in `compiler.go`, `bytecode.go`, `script.go` |
| 2014c20a | `tools/debugger`: the debugger program, the mod, the scripts and these notes |
| aff8c4e2 | `CLAUDE.md`: rules and environment for Claude sessions in this clone |


Files:

```
src/script_debugger.go   engine side: controller sources and trace, setters, Lua functions
tools/debugger/
  CMakeLists.txt         ImGui static lib + ikemen-debugger.exe; copies mod/ikdbg.lua next to the exe
  src/main.cpp           Win32 + OpenGL2 window and input for ImGui, DPI aware; test hooks (below)
  src/app.cpp            the debugger: settings, protocol handling, every window
  src/bridge.cpp/.h      starts Ikemen_GO.exe with pipes, reads its stdout/stderr lines, writes stdin
  src/json.h             small JSON parser for the protocol
  mod/ikdbg.lua          the in-game side (Lua mod, installed into external/mods at launch)
  third_party/imgui      Dear ImGui 1.92.9b docking (only the files used)
  scripts/               build.ps1/build.sh, msys_setup.ps1, test_ui.ps1, probe.ps1
```

## What works

Everything in the README's window table, tested against `kfm` (CNS, with
the ZSS common files) and `ryu_pots` (Ryu by Pots: about 6000 controllers, a
helper "Bag"):

- pause, exact stepping by 1/10/N ticks, speed, save/load state, reset
  round, reload the match;
- players and helpers, inspector, `var`/`fvar` changes highlighted;
- condition breakpoints (Lua over Ikemen's triggers, edge triggered) and
  controller breakpoints; both persist;
- frame history of 1200 ticks, trace of state changes and hits;
- Code window: the controllers that ran, in order, across state changes
  within the tick (e.g. `common1.cns.zss:49` changeState to 11 seen in the
  same tick as the new state's controllers); file lines match the sources
  exactly for CNS and ZSS;
- editing `var`/`fvar` (double-click), position and velocity;
- the patched Ikemen without `-ikdbg` plays normally (controllers are not
  wrapped, no sources recorded).

## Build environment (this machine)

- MSYS2 at `$env:MSYS2_ROOT`, UCRT64: gcc 16.2, SDL2 2.32.10, libxmp
  4.7.4, cmake, ninja. Installed for this work by `scripts/msys_setup.ps1`.
  `C:\msys64` is an unrelated 2016 install: do not use it.
- Go 1.27.3 at `C:\Program Files\Go` (the build scripts put it on the PATH).
- `scripts/build.ps1 [ikemen] [debugger]` runs `scripts/build.sh` in MSYS2.
  Ikemen's `build/build.sh Win64` also builds libvpx and ffmpeg the first
  time (long), copies their DLLs to `lib/`, and writes `./Ikemen_GO.exe`.
- PowerShell 5.1 strips the inner quotes of native arguments: run MSYS2 work
  as script files, never as `bash -lc "..."` with quotes inside.
- The debugger also builds with Visual Studio 2022 (x64 dev shell, `cmake -G
  Ninja`); the MSYS2 build links statically, so the exe needs no DLLs.
- Release assets (`chars`, `stages`, `data`, `font`, `sound`, `video`) were
  copied from the v1.0.0 Windows release; git ignores them. Test
  characters in `chars/`: kfm, kfm720, kfm_zaxis, kfm_zss, ryu_pots.

## Testing

- `scripts/probe.ps1 [-P1 ryu_pots]`: protocol test without the GUI. Starts
  the game with the mod, sends commands over stdin at fixed times, prints the
  replies, sample sources and one tick's trace; full output in
  `build/probe.log`.
- `scripts/test_ui.ps1 -Fresh -Actions "key:F5|wait:1|click:620,300|shot:name"`:
  GUI test. Starts the debugger with `--launch`, places both windows, runs the
  actions and saves the debugger's frames as PNGs in `build/shots`. The app
  has two test messages (in `main.cpp`):
  - `WM_APP+0x4D47`: mouse, wParam 0 move, 1/2 left button down/up, lParam
    `x | y << 16` in client pixels. Posted real mouse messages get lost to
    `WM_MOUSELEAVE`.
  - `WM_APP+0x4D48`: save the next frame as `shot_<wParam>.bmp` next to the
    exe (`glReadPixels`).
- The screen is at 125%: test scripts must call `SetProcessDPIAware`. Never
  capture the screen with `CopyFromScreen`: it once captured the user's
  browser instead of the game.
- A debug-build ImGui assertion shows a modal box that can be hidden behind
  the window and looks like a freeze: attach `cdb.exe` (Windows Kits) and
  `~*kb` to find it.

## Engine facts learned

Lua bridge:

- `.lua` files in `external/mods` load at start; `[Common] Lua = loop()`
  runs `hook.run("loop")` every frame of a match, also while paused.
- `sys.commandLine`: one stdin line per frame is run with `DoString` at the
  end of the match loop. A Lua error there, or in the loop hook, panics the
  game: everything runs under `pcall`. A UTF-8 BOM on stdin breaks the first
  line.
- `frameStep()` from stdin or the hook does nothing: the flag is cleared at
  the start of the loop before it is read. Stepping = unpause, then pause
  again from the hook after N ticks.
- A command line quick match (`-p1 x -p2 y`) starts before the mods load
  unless `-loadmotif` is given.
- `debugWC` is the trigger context; `player(n)`, `playerId(id)`,
  `playerIndex(i)`, `playerIndexExist(i)` move it (restore it afterwards).
- gopher-lua: `table.concat` over thousands of strings fails with "registry
  overflow"; join in groups.
- `jsonEncode` only writes files, hence the mod's own JSON writer.
- 557 Lua functions are registered in `script.go`, among them `getHitVar`,
  `hitDefVar`, `hitDefAttr`, `explodVar`, `projVar`, `numExplod`,
  `numProj`, `const`, `playSnd`, `rectNew`/`rectDraw`,
  `textImgNew`/`textImgDraw`, `animNew`/`animDraw`, `sffNew`, `stageVar`,
  `getStageInfo`, `replayRecord`, `reload`, `saveState`/`loadState`. Ikemen's
  own debug keys are in `external/script/debug.lua`.

Compiler (`compiler.go`):

- CNS: the `[State]` header line is `c.i + 1` (0-based index); for a
  statedef, after `c.i++`, the line is `c.i`. ZSS: `c.i` is already the
  1-based line of the token.
- Controllers are appended at the two `NullStateController` checks (CNS) and
  in `stateBlock` (ZSS); the debugger wraps them there (`c.traced`).
- `StateBlock.Run` type-switches only on `*StateBlock`, so a wrapper type
  around a controller is safe. A wrapped controller runs only when its
  triggers held, so "ran" means "triggers held".
- States live in `sys.cgi[pn].states`; the running state is `c.ss.sb`
  (`*StateBytecode`, with `playerNo`).

## Limits and next steps

Limits today:

- Breakpoints stop after the tick; no stepping between controllers (that
  would need the match loop to yield in the middle of `Char.update`).
- No live editing of sprites, animations, sounds, commands or states: edit
  the files, then Reload match (`reload()`, Shift+F4).
- Not in netplay or rollback matches (no stdin bridge there).
- The game draws in its own window; the debugger cannot draw over it.

Cheap next steps (mod and app only): breakpoints on entering a state; an
"only if" condition on controller breakpoints; a Watch list of Lua
expressions; a Hits log with `getHitVar` (damage, hit time, fall); move
frame data (startup/active/recovery/advantage) computed from the snapshot
history; explods and projectiles lists via `explodVar`/`projVar`.

