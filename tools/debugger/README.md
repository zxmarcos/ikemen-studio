# Ikemen GO debugger

A debugger for Ikemen GO characters. It is a separate program
(`ikemen-debugger.exe`, Dear ImGui, Windows) that starts the game and talks to
a Lua mod (`mod/ikdbg.lua`) through the game's stdin and stdout.

It works at two levels:

- **Any Ikemen GO** (tested with the v1.0.0 Windows release): players and
  helpers, pause and tick stepping, breakpoints on Lua conditions, frame
  history, trace of state changes and hits, a Lua console.
- **Ikemen GO built from this branch** (`src/script_debugger.go` and a few
  lines in `compiler.go`, `bytecode.go` and `script.go`): also the state
  controllers that run on every tick with their file and line, a Code window,
  breakpoints on a state controller, and editing `var`/`fvar`, position and
  velocity.

See [NOTES.md](NOTES.md) for the state of the work, the build environment and the
tests.

## Build

With MSYS2 UCRT64 set up as Ikemen's [BUILDING.md](../../BUILDING.md) says,
plus `mingw-w64-ucrt-x86_64-cmake` and `mingw-w64-ucrt-x86_64-ninja`
(`scripts/msys_setup.ps1` installs everything into a fresh MSYS2):

```powershell
tools\debugger\scripts\build.ps1            # Ikemen GO and the debugger
tools\debugger\scripts\build.ps1 debugger   # only the debugger
```

The debugger lands in `tools/debugger/build/` with `ikdbg.lua` next to it. It
also builds with MSVC (`cmake -S tools/debugger -B tools/debugger/build`).

Ikemen GO itself is built by `build/build.sh Win64`; it needs `chars`,
`stages`, `data`, `font`, `sound` and `video` from a release next to
`Ikemen_GO.exe` (the repository ignores them), without replacing the
`data/*.zss` files the repository has.

## Use

1. Run `tools/debugger/build/ikemen-debugger.exe`. In **Session**, check the
   path of `Ikemen_GO.exe` (by default the one of this checkout), the
   characters, the stage, and which side the AI plays.
2. Press **Launch**. The debugger installs `ikdbg.lua` into the game's
   `external/mods` folder and starts the game with `-ikdbg 1`. Neither the mod
   nor the engine changes do anything without that flag, so normal play is
   not affected. `ikemen-debugger.exe --launch` starts the game at once with
   the saved settings.

Windows:

| Window | What it does |
|---|---|
| Session | Path of the game, characters, stage, AI per side, extra arguments; Launch/Restart/Stop; connection state |
| Game (engine side) | The game itself: its frames, with the collision boxes and axes over them; hover a player or helper to see it, click to select it, drag it to move it. The mouse wheel zooms (the game's pixels, kept sharp), the right button pans, "Follow it" keeps the selected one in view. While the window has focus, the keyboard goes to the game (but F5, F10, F11). The game's own window is hidden unless Session says otherwise |
| Control | Pause/continue (F5), step 1 tick (F10), step 10 (F11), run N ticks, speed, save/load state, reset round, reload the match from disk, collision boxes, Ikemen's debug display |
| Players | Every player and helper: state, previous state, animation, element, time, types, ctrl, position, velocity, life, power |
| Animation (engine side) | The selected entity's animation element in its AIR file: the elements (a click shows one on the game), the element's sprite, offset and time, its Clsn1 and Clsn2 boxes (own, or the action's default and how many elements share it), add and delete boxes. The action can be shown on the player, copied, renumbered or deleted; an element duplicated, deleted, moved or made the loop start. With **On the game, drag its boxes**, the boxes are drawn on the Game window with handles: drag an edge, a corner or the inside; with **the sprite**, a drag near the player moves the element's offset. The previous element's boxes can be shown faint. Every change goes to the game at once (only the animations are compiled again); **Save** writes the AIR file, which the Code window edits too |
| Sprites (engine side) | The selected player's SFF file (v1 or v2): the sprites, filtered by group; a preview (wheel: zoom, drag: move the axis) with any of the palettes; renumber, axis, replace with a PNG, save as a PNG, delete; add PNG files (to the character's palette 1,1, or with palettes of their own). **Save** writes SFF v2 (the first time the old file is kept as `.bak`) and the game reads it again at once. The Animation window picks an element's sprite among thumbnails and can show the previous element's sprite faint on the game |
| Sounds (engine side) | The selected player's SND file: every sound with its length and format, played here (Play, Stop); renumber, replace with a WAV file, save as a WAV file, delete, add WAV files. **Save** writes the file (the first time the old one is kept as `.bak`) and the game reads it again at once |
| Commands (engine side) | The selected player's input as the game reads it: the keys held now (hover one: for how many frames) and before, with how many frames each lasted. The [Command] sections of its CMD file: the steps done of each command under way, green while `command = "name"` is true (fading for a second after), how many times each was completed. The selected command's name, keys, time, buffer.time and steptime are edited here (empty: the default); duplicate, delete, add, show in Code. **Apply** (Ctrl+S) saves and the game compiles the characters again, as in Code |
| Stage (engine side) | The stage's def file: its sections, and the fields of the one selected, edited here (change, remove, add). Each change is shown at once: the game loads the stage again from the text, without restarting the match (Reset round keeps it); **Save** (Ctrl+S) writes the file, **Revert** goes back to it. With **Stage** checked on the Game window (or "Pick on the Game window" here), the mouse picks the stage's BGs instead of the players: hover shows which, a click selects its section, a drag moves it (its `start`, whole units; BGs positionlinked to it follow). **Limits** draws on the game how far the camera goes (boundleft/right/high/low), the players' bounds, the floor and the starting positions (P1-P4, the camera's); in pick mode their handles drag those fields. **Free camera**: the wheel zooms and the right button pans what is drawn (the game's camera and the players' bounds stay as they are); **Fit the stage** shows all of it. The stage's time is shown, **Restart the stage** sets it back to 0; BGCtrls that ran on the stage's last step are green in the list; a BGCtrl (or a BGCtrlDef, with all of its BGCtrls) shows when it runs on a time bar, and its BGs are outlined on the game |
| Fight screen (engine side) | The fight screen's def (`fight.def`: lifebars, power, face, name, time, combo, round...): its sections, the elements of the one selected that are on screen, and the fields of the element selected (keys without its prefix; or all of the section's), edited here; the element's place (its group's `pos`, its own `offset`) can be dragged. Each change is shown at once: the game loads the fight screen again from the text (in about 20 ms: the SFF, SND and fonts in use are kept; **Reload** reads them again), the match and the round going on; **Save** (Ctrl+S) writes the file, **Revert** goes back to it. With **Fight screen** checked on the Game window (or "Pick on the Game window" here), the mouse picks the fight screen's elements instead of the players: hover shows which (the smallest under the mouse: a bar's fill before its frame), a click selects it, a drag moves it with its group (`p1.pos`, `team1.pos`, `pos`...), Alt+drag moves it alone (its `offset`) |
| Inspector | The selected entity in full, `var`/`fvar` (changes of the last tick in yellow), and actions: SelfState, ChangeAnim, set life, full life/power, stand, toggle AI. Engine side: double-click a `var`/`fvar` to change it, set position and velocity |
| Frame data | Tick by tick history of the selected entity (1200 ticks); state changes and hits are highlighted. Engine side: the number of controllers that ran; click a tick to see them in Code |
| Trace | State changes and hits of every entity |
| Code | An editor of the character's files (syntax colours, completion and documentation of triggers and controllers, undo). **Save and apply** (Ctrl+S) saves the changed files and the game loads and compiles the characters again without restarting the match (engine side); when that fails, the game keeps the previous version and the line of the error turns red. Engine side, also: the controllers that ran on the tick, in the order they ran (all states: -4, -3, -2, -1, the current state, +1, and any state changed into during the tick); in the file, lines that ran are green, a dot marks each controller, clicking the dot sets a breakpoint. After each trigger line (CNS `trigger`/`triggerall`, ZSS `if`) its value on the tick shown: true, false, undefined, or nothing when it was not evaluated (a trigger after one that held, a state that did not run); after a controller that did not run, how many ticks ago it last did. The dot of a statedef sets a breakpoint on entering that state. Follows the selected entity's state, but not while you type |
| Breakpoints | A Lua condition over Ikemen's triggers, for anyone, one player, or one player and its helpers; the game pauses on the tick the condition becomes true (what is already true when it is set waits until it turns false and true again). Entering a state is the condition `stateNo() == n`, set from the Code window. Engine side: breakpoints on state controllers, the game pauses after a tick in which one ran. Both are saved with the settings |
| Console | Any Lua, run in the game (`player(2); return stateNo(), var(3)`). Ikemen's own log is shown too |

Settings, breakpoints and the window layout are saved in
`ikemen-debugger.ini` next to the exe.

## Engine side

`src/script_debugger.go` and `src/editor_*.go` hold everything; the other files only call them:

- `compiler.go` records where each state controller and statedef comes from
  (CNS and ZSS) and wraps the controller (`CharCompiler.traced`), only under
  `-ikdbg`;
- `bytecode.go` keeps the statedef's source in `StateBytecode.src`, and runs
  `OC_debugrec`, which the compiler appends to each trigger under `-ikdbg`
  so that its value is recorded where it is evaluated;
- `script.go` registers the new Lua functions.

`editor_frame.go` shares each frame drawn with the debugger, with the boxes of
the players and helpers in it, and takes keys and Lua commands back (shared memory
`Local\IkemenFrame<pid>`, its layout is at the top of the file); `system.go`
calls it once per frame. `editor_input.go` adds each player's input buffer
and the commands under way to that geometry; `editor_stage.go` adds where
each BG of the stage was drawn (one line in `Stage.draw` and one in
`Animation.Draw` tell it), moves BGs and reloads the stage; `editor_fight.go`
adds where each element of the fight screen was drawn (`Layout` keeps the
section and prefix it was read from; its draws and `RenderSprite` tell it)
and reloads the fight screen (`findActiveSff`, `findActiveSnd` and
`loadFnt` lend it the files in use).

New Lua functions: `debugTraceEnable(on)`, `debugTraceRead()` (character id,
source index pairs since the last call), `debugSourceCount()`,
`debugSources(from, count)`, `stateSource([stateNo])`, `setVar`, `setFvar`,
`setSysVar`, `setSysFvar`, `setPos(x, y[, z])`, `setVel(x, y[, z])`, `debugMoveBy(dx, dy)` (pixels of the game picture), `debugReloadChar(pn)` (`editor_reload.go`: load and compile a player's files again in the middle of a match), `debugReloadAnims(pn[, textFile])` (the animations alone, optionally from unsaved text), `debugCharFiles(pn)`, `debugSetAnimElem(elem[, time])`, `debugReloadSprites(pn)` and `debugReloadSounds(pn)` (the SFF or SND read again, for every player using it), `debugTriggerWatch(id)` (record that character's trigger values; they go out with the frames), `debugStageBgMove(index, dx, dy)`, `debugReloadStage([textFile])`, `debugStageReset()`, `debugReloadFight([textFile[, keep]])` and `debugCamera(on[, x, y, scale])` (the editor's free camera: one line in `system.go` passes the camera each frame is drawn with through `editor_stage.go`).

## How it works

Ikemen GO already provides what the bridge needs:

- `.lua` files in `external/mods` are loaded at start.
- `[Common] Lua = loop()` runs `hook.run("loop")` on every frame of a match,
  also while the game is paused.
- The match loop runs one line of stdin per frame as Lua.
- More than 500 Lua functions read the triggers of any player or helper
  (`playerId(id)` moves the trigger context), and act on the match
  (`selfState`, `changeAnim`, `setLife`, `togglePause`, `saveState`, `reload`, ...).

Protocol, one line each:

| Direction | Line | Content |
|---|---|---|
| game → debugger | `@IKHELLO {...}` | mod and Ikemen GO versions, whether the engine side is there |
| game → debugger | `@IKS {...}` | snapshot after each tick: tick, round, pause, every entity, events, breakpoint hit counts (vars only for the watched entity), and with the engine side the controllers that ran. While paused, only after a command or twice a second |
| game → debugger | `@IKSRC {...}` | engine side: where controllers come from, 300 per frame until all are sent (a character like Ryu by Pots has about 6000) |
| game → debugger | `@IKBRK {...}` | a breakpoint stopped the game |
| game → debugger | `@IKRES {...}` | result or error of a command |
| game → debugger | `@IKRELOAD {...}` | engine side: result of reloading the characters (per player: ok, or the compiler's error) |
| game → debugger | `@IKFILES {...}` | engine side: every player's files ([Files] of the def, resolved) |
| debugger → game | `if ikdbg then ikdbg.exec('...') end` | Lua, run under `pcall`. Through stdin, or with the engine side through the shared memory (no limit per frame, menus too) |

Snapshots are about 1 KB per tick with two characters.

## What was found on the way

- **A Lua error from stdin or from the frame hook stops the game**: the
  engine raises it. The debugger only sends `ikdbg.exec(...)`, which runs the
  code under `pcall`, and the mod's frame hook runs under `pcall` too.
- **`table.concat` of thousands of strings fails in gopher-lua** ("registry
  overflow"): the mod joins long lists in groups and sends sources in chunks.
- **`frameStep()` does not work from stdin or from the frame hook**: the
  match loop clears the flag at the start of each frame, before using it.
  Stepping unpauses the game and pauses it again from the frame hook after
  exactly N ticks.
- **Command line matches start before the mods load** (`main.lua` runs the
  quick match first). The debugger adds `-loadmotif`, which loads the mods
  first.
- **Stdin must not start with a UTF-8 BOM** (some .NET hosts write one):
  the first line then fails to parse and stops the game.
- A condition that stays true would stop the game on every tick after
  Continue, so condition breakpoints fire when it becomes true.

## Limits

- Breakpoints stop the game after the tick, not in the middle of a state:
  the trace shows exactly which controllers ran and in which order, but
  there is no stepping from one controller to the next.
- **Save and apply** keeps the SFF and SND in use: changes to them made
  outside the debugger need **Reload match** (the Sprites window reloads the
  SFF it saves).
- The stdin bridge only exists in the normal match loop: not in netplay or
  rollback matches.
