# Ikemen GO debugger: state of the work

Handoff notes for whoever continues this work (people or Claude sessions).
[README.md](README.md) describes the debugger for its users.
Last updated 2026-10-25.

## Where it is going

The goal grew from a debugger into an editor of everything Ikemen GO lets
people customize (characters, stages, screenpacks...), with the engine showing
the result live and the things on screen clickable. Decisions taken with the
user (2026-10-25):

- **The editor stays a separate program**, and the game's frames are shown
  inside it (window Game) through shared memory; the editor draws its
  overlays and does the picking itself, from geometry the engine sends with
  each frame. Not ImGui inside the engine, not an overlay window.
- **Maybe upstream later**: engine code stays in new files (`src/editor_*.go`,
  `src/script_debugger.go`) with one-line calls elsewhere, inert without
  `-ikdbg`.
- **Characters first**, then stages, then screenpacks.

Phase 0, the infrastructure, is done: frames and geometry in shared memory,
keys from the editor, the game's window hidden and sized as the game's
picture (exact pixels), Lua commands through a ring in the same memory (all
of them every frame, in menus too; stdin stays for an unpatched Ikemen).
Mouse events are not sent: Ikemen GO does not use the mouse.

Phase 1, characters: drag to move a player (done); the code editor with
Apply (done: `editor_reload.go` recompiles the characters' states, commands,
constants, animations and sounds without restarting the match; the editor
widget is `src/editor.cpp`); next, the animation
editor with Clsn boxes dragged on the Game window; then sprites, sounds,
commands.

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
src/editor_frame.go      engine side: frames, geometry and input events in shared memory
src/editor_shm_*.go      the shared memory (Windows; a stub elsewhere)
src/editor_reload.go     reloading a character during a match (debugReloadChar)
tools/debugger/
  CMakeLists.txt         ImGui static lib + ikemen-debugger.exe; copies mod/ikdbg.lua next to the exe
  src/main.cpp           Win32 + OpenGL2 window and input for ImGui, DPI aware; test hooks (below)
  src/app.cpp            the debugger: settings, protocol handling, every window
  src/bridge.cpp/.h      starts Ikemen_GO.exe with pipes, reads its stdout/stderr lines, writes stdin
  src/frame.cpp/.h       reads the shared frames, writes input events and commands
  src/editor.cpp/.hpp    the code editor widget (with marks and line backgrounds)
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
  wrapped, no sources recorded, no frames shared);
- window Game (patched Ikemen, OpenGL renderers): the game's frames, Clsn1
  and Clsn2 boxes and axes drawn over them, hover and click to select a
  player or helper; while it has focus, keys go to the game. The game's own
  window is hidden. Dragging a player or helper moves it (`debugMoveBy`).

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
- More `test_ui.ps1` actions: `down:RIGHT`/`up:RIGHT` (hold and release a
  key), `ctrl:Z`, `drag:x1,y1,x2,y2`, `visible` (is the game's window
  shown), `game:hide|show|min|off`, `ps:code` (PowerShell, no `|` inside),
  and `lua:code`: Lua run as if typed in the Console (`WM_COPYDATA`,
  dwData 0x4D47). The debugger's log, results included, is also written to
  `build/ikemen-debugger.log`, so a test can read what Lua returned. Click
  coordinates depend on the layout: take a shot first.
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

Frames (`editor_frame.go`):

- `System.await` ends the frame (`gfx.EndFrame()`, the post-processing pass
  into the window's framebuffer) then swaps; the frame is read between the
  two with `gfx.ReadPixels` (window size, rows bottom first). It costs a
  synchronous read per frame; the renderers' async PBO path
  (`BeginScreenshot`/`FinishScreenshot`) is used by screenshots.
- A hidden window (SW_HIDE) keeps drawing and its frames read fine; a
  minimized one does not (the picture freezes). Vulkan is not handled yet.
- Clsn boxes are placed as `DebugClsn.Add`/`draw` place them: game units to
  the game picture's pixels with `(x + gameWidth/2) * widthScale`,
  `y * heightScale`; the window shows the picture at
  `GetScaledViewportSize()`. Not checked yet: the full-resolution
  composition (`shouldComposeFullResolution`, e.g. with the debug display),
  where the draw uses another aspect state.
- Keys: `OnKeyPressed`/`OnKeyReleased` take SDL key codes; the game does not
  pause without focus, but it lowers its volume (`FocusLossVolume`).
- Commands from the ring run with `sys.luaLState.DoString` inside `await`:
  gopher-lua's `PCall` turns Go panics into errors, so a trigger called
  without a character (in menus) fails cleanly. `id()` then fails: the mod
  calls it under `pcall`.
- The window is sized with `sys.window.Window.SetSize(scrrect)`; the game
  resolution is `GameWidth`/`GameHeight` of `save/config.ini` (1280x720
  here), the window size otherwise follows `WindowWidth`/`WindowHeight`.

Reloading a character (`editor_reload.go`):

- `Char.load(def, gi)` reads the def, constants, SFF (`loadSff` borrows the
  SFF in use: `findActiveSff`), palettes, AIR, SND; `CharCompiler.Compile`
  reads the commands into `p.cmd[pn]` (appending: compile into a scratch
  `Char`) and compiles the states. Neither touches the live `sys.cgi[pn]`.
- `Compile` clears `sys.stringPool[pn]`, which the running bytecode indexes:
  it is put back when the compile fails.
- Swapping: `gi.states` and every `c.ss.sb` of that player's old states
  (`c.ss.ps` resized), `root.cmd[pn]` keeping its `Buffer` (helpers share
  the root's slice), constants (gi and `c.size`), `gi.animTable` with
  `changeAnimEx` + `SetAnimElem` for whoever shows that player's
  animations, `gi.snd`.
- CNS compile errors name the line after the statedef's section, not the
  line of the bad value.

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

