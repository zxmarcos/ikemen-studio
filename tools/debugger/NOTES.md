# Ikemen GO debugger: state of the work

Handoff notes for whoever continues this work (people or Claude sessions).
[README.md](README.md) describes the debugger for its users.
Last updated 2026-10-26.

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
widget is `src/editor.cpp`); the animation editor
(done: window Animation; Clsn boxes and the sprite's offset dragged on the
Game window, which zooms and pans; the previous element's boxes; elements
duplicated, deleted, moved, loop start; actions copied, renumbered,
deleted; `src/air.cpp` parses and edits AIR text, `tests/air_test.cpp`
tests it. Not done: a sprite onion skin and a canvas of its own, which need
the app to draw sprites, i.e. an SFF reader); sprites (done: window
Sprites, `src/sff.cpp` with checked
decoders and lodepng; `debugReloadSprites`; the sprite picker and the onion
sprite in Animation); sounds (done: window Sounds, `src/snd.cpp`,
`debugReloadSounds`; played by the app with PlaySound); commands (done:
window Commands; `editor_input.go` sends each player's input buffer and the
commands under way with the frame geometry; `src/cmd.cpp` parses and edits
the [Command] sections, applied with the Code window's reload); live
trigger values in Code (done: `OC_debugrec` appended to each trigger under
`-ikdbg` records its value where the engine evaluates it, for the one
character `debugTriggerWatch` names; no second evaluation, so triggers with
side effects (`:=`) are safe), "ran N ticks ago", breakpoints on entering a
state.

Phase 2, stages: begun. Window Stage (the def's sections and fields,
`src/ini.cpp`), BGs picked and dragged on the Game window
(`editor_stage.go`: each BG's rectangle and scale with the frame,
`debugStageBgMove`), the stage reloaded during a match (`debugReloadStage`).
Then: every edit reloads the stage from the unsaved text (a file next to
the def that the engine deletes once read; Save writes the def); the
limits (camera bounds, players' bounds, floor, starting positions) drawn
and dragged on the game; a free camera that only moves what is drawn
(`debugCamera`), with "Fit the stage"; BGCtrls (which ran on the stage's
last step, a line in `bgCtrlAction`; their times on a bar; their BGs
outlined; `debugStageReset`). Every field is already live through the
reload, which is light: a 2D stage reloads in under 1 ms (`loadSff`
borrows the SFF in use through `findActiveSff`), a 3D stage's glTF model
is kept while its file does not change (`editorStageModel`: stage3d went
from ~510 ms to under 1 ms), and the stage goes on from where it was
(`editorKeepStage`: `Stage.reset` used to set its time back to 0; BGs
matched by index, so not when BGs were added or removed).

Phase 3, screenpacks: begun with the fight screen (`fight.def`). Window
Fight screen: its sections and fields (`src/ini.cpp`), its elements
(`editor_fight.go`: each `Layout` drawn, with the section and prefix it was
read from) picked on the Game window and dragged (the group's `pos`, or the
element's `offset` with Alt), each edit reloading the fight screen from the
text in about 20 ms, the round going on. Then the motif (`system.def`): window
Motif, the same editing, its texts and animations picked in menus and
screens (`editor_motif.go`), reloaded in place (~400 ms, so a drag writes
when let go); its screens' BGs (`[TitleBGdef]` and its `[TitleBG ...]`, a
`BGDef`: one line around each BG in `BGDef.draw`) picked and dragged by
their `start`; the select screen (cells, names, cursors; portraits: the
mod wraps `start.f_animGet` and `debugAnimSource` gives the new animation
the source of the property's `AnimData`). Then storyboards: window
Storyboard, the one playing (`editor_storyboard.go`), reloaded at the same
scene and time in a few ms. Then the screens' animations (`[Begin Action]` in
their defs) in the Animation window: each element drawn says the action and
element it shows (`Animation.actionNumber`, set for actions compiled from a
text; `editorSpriteDrawn` records it), and a reload lets every animation go
on from the element it was at (`editorKeepPlayback`: old and new structures
walked together; fight screen and storyboard). Then the screens' sprites: the
frame geometry names each screen's SFF, the sprite picker and the Sprites
window open it (at the sprite the element picked shows: its action's
element, its `spr`, a BG's `spriteno`), and saving it reloads the screen
with the SFF read again (the reload functions' `sprites` argument; SND and
fonts still borrowed). The motif's animations and BGs, too, go on across a
reload (`editorKeepPlayback` before the merge; BGDefs: `editorKeepBgDef`).
Then the roster (`select.def`): window Roster, the grid with the
characters' portraits, cells moved, inserted, deleted, their parameters,
the extra stages; each change has the game add the roster again
(`editor_select.go` empties the engine's lists; the scripts' parsing and
grid became `main.f_loadSelectDef` and `start.f_makeGrid`, run again by the
mod's `ikdbg.reloadRoster`); the select screen's cells picked on the Game
window select them in Roster, and the other way (P1's cursor goes there):
the mod draws the cells' items one by one, each motif animation marked with
its cell (`debugAnimTag`: a source like its own, with a `tag`, which the
copy `batchDraw` queues keeps). Then fonts: window Fonts (`editor_font.go`
records every font loaded; a `.def` font's `[Def]` edited live, its glyphs
in the Sprites window, a text picked on the game selects its font). Then the
fight screen keys written with a selector (`p*.pos`, `p1|p3.pos`), found
and edited as the def has them. Then TrueType texts picked on the game
(the OpenGL renderers' `Printf` passes each glyph's quad to
`editorTtfGlyph`) and `.fnt` fonts edited (`src/fnt.cpp`: their text
replaced, their character map read as the engine does). Phase 3 is done:
every file of a screenpack the engine reads (`system.def`, `fight.def`,
storyboards, `select.def`, fonts, their SFFs and animations) is edited with
the game showing it.

Phase 4, the lab (testing a move the same way again and again), begun with
the Lab window (`editor_lab.go`; one line in `CommandList.InputUpdate`
hands the 14 buttons read for a player's root to `editorInputButtons`,
which records them or replaces them, back and forward in place of left
and right from `Char.fbFlip`, set just before): a dummy (the mod's
`ikdbg.dummy` holds its keys each tick with `debugInputHold`, guarding
when `inGuardDist()`, one tick late, which is enough), takes recorded and
played (`ikdbg.record`, `ikdbg.play`: after `resetRound()` they start on
the first tick of round state 2 seen after another; `sys.randseed` saved
at the start and given back), characters swapped (`debugSwapChar`: the
side's `sys.sel.selected` entry, `reloadCharSlot` and `reloadFlg`, as
MatchRestart does; the character added to the select list when not in
it); the attack of each hit taken found in the attacker's controller
history (Trace); explod and projectile counts in Players. Next for it: see
"Limits and next steps".

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
src/editor_input.go      engine side: players' input buffers and commands under way, for the geometry
src/editor_stage.go      engine side: where the stage's BGs were drawn, moving a BG, reloading the stage
src/editor_fight.go      engine side: where the fight screen's elements were drawn, reloading the fight screen
src/editor_motif.go      engine side: the same for the motif (system.def), reloaded in place
src/editor_storyboard.go engine side: the same for the storyboard playing
src/editor_select.go     engine side: select.def's characters and stages emptied to be added again
src/editor_font.go       engine side: every font loaded, a font's file read again in place
src/editor_objects.go    engine side: a character's explods and projectiles (debugObjects)
src/editor_lab.go        engine side: a player's keys held, recorded, played; a character swapped
tools/debugger/
  CMakeLists.txt         ImGui static lib + ikemen-debugger.exe; copies mod/ikdbg.lua next to the exe
  src/main.cpp           Win32 + OpenGL2 window and input for ImGui, DPI aware; test hooks (below)
  src/app.cpp            the debugger: state, commands, protocol handling, settings
  src/app_layout.cpp     the menu bar, each window's place and open flag, the two layouts (BeginTool)
  src/app_internal.h     what the app_*.cpp files share (state, each file's functions)
  src/app_game.cpp       Game window: frames, boxes over them, dragging, keys to the game
  src/app_anim.cpp       Animation window (AIR)
  src/app_sprites.cpp    Sprites window (SFF), file dialogs
  src/app_sounds.cpp     Sounds window (SND)
  src/app_code.cpp       Code window: source files, Apply
  src/app_cmd.cpp        Commands window: input, commands under way, the CMD file
  src/app_stage.cpp      Stage window, and the BGs on the Game window
  src/app_screen.cpp     Fight screen, Motif and Storyboard windows, and their elements on the Game window
  src/app_debug.cpp      Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
  src/bridge.cpp/.h      starts Ikemen_GO.exe with pipes, reads its stdout/stderr lines, writes stdin
  src/frame.cpp/.h       reads the shared frames, writes input events and commands
  src/editor.cpp/.hpp    the code editor widget (with marks and line backgrounds)
  src/air.cpp/.h         AIR files: actions, elements, Clsn boxes with their lines; text edits
  src/sff.cpp/.h         SFF files: read v1/v2, decode (raw, RLE8, RLE5, LZ5, PNG), write v2, PNG in and out
  src/snd.cpp/.h         SND files: read, write, WAV descriptions
  src/cmd.cpp/.h         CMD files: the [Command] sections with their lines; text edits; a motion's steps
  src/ini.cpp/.h         def files (stages, fight screens, motifs): sections and fields with their lines; text edits
  src/cns.cpp/.h         state files (CNS and ZSS): statedefs, controllers, lines of code (Analysis)
  src/make.cpp/.h        text written for new things: a ChangeState in [Statedef -1], a move's statedef (CNS or ZSS); a new character's states, actions, CMD
  src/app_create.cpp     Create window (the menu bar's Create): a move, a character
  third_party/lodepng    lodepng (commit e584a849, zlib licence) for PNG
  tests/                 air, sff, snd, cmd, ini, roster, fnt, cns, make _test.cpp (scripts/build.ps1 test)
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

- `scripts/build.ps1 test`: unit tests of the parts without a window
  (`tests/air_test.cpp`, AIR parsing and edits; `tests/sff_test.cpp`, every
  sprite of kfm.sff (v2) and ryusprite.sff (v1) decoded, written as v2 and
  read back with the same pixels, a sprite through PNG and back;
  `tests/snd_test.cpp`, kfm.snd read and written back to the same bytes;
  `tests/cmd_test.cpp`, CMD parsing and edits, kfm.cmd's 37 commands;
  `tests/ini_test.cpp`, def parsing and edits, stages/kfm.def's 7 BGs).
- Stage tests: `test_ui.ps1 -Fresh -IniExtra "Stage=kfm"` (later lines of the
  ini win). Back up the def first and restore it after a test that saves.
  The same for `data/fight.def` (`data` is not in git either). The Stage,
  Fight screen, Motif, Storyboard, Roster and Fonts windows are closed in the
  first layout: `click:123,11|wait:0.4|click:170,62` picks Layout > Stage and
  screens (`click:123,11|wait:0.4|click:137,41`: back to Debugging); Stage,
  Fight screen, Motif and Storyboard are then the top right tabs, Roster and
  Fonts the left ones (take a shot for the coordinates, the Game window's
  "Screenpack" checkbox too). Motif tests: `-P1 "" -P2 ""` starts in
  the menus (a demo match first: `click:740,350|key:RETURN` shows the title
  menu); the motif in use is `data/ikemen1/system.def` (`save/config.ini`).
  When the demo has not started yet that key picks Arcade instead: for a
  repeatable path, set `[Demo Mode] enabled = 0` in a backed-up copy, then
  `click:740,350|key:Z|wait:1.5|key:Z|wait:10` reaches the select screen (Z is
  P1's a, which confirms in menus; Enter does not). Two more Z (the first
  character, its colour) start an arcade match, whose first character's intro
  storyboard plays first (kfm_zss: `chars/kfm_zss/intro.def`).
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
  `build/ikemen-debugger.log`, so a test can read what Lua returned. Also
  `wheel:x,y,n` (the test mouse message takes 3/4 right button, 5/6 wheel),
  and `keys:+DOWN,+RIGHT,-DOWN,+A,-A,-RIGHT` (50 ms apart: every other
  action is followed by 150 ms, too slow for a motion within 15 frames). Keys
  reach the game only while Game has focus: click the picture first. P1's
  keyboard (`save/config.ini`): arrows, a b c = Z X C, x y z = A S D.
  Click coordinates depend on the layout: take a shot first.
- Lab tests: with `-IniExtra "AI1=0`nAI2=0"` nobody moves on their own;
  `lua:debugInputHold(1, 'F')` stands for held keys (they are recorded like
  real ones), `ikdbg.record(1, true)` and `ikdbg.stopTake()` record a take
  (it shows in the log as "Take N: P1, n ticks" and is saved in the ini),
  and a run without `-Fresh` keeps it to play from the Lab window (its tab
  is at `click:690,577` in the first layout; take a shot for its buttons).
  `-IniExtra "Dummy=2|1|1|1|0"` makes P2 a crouching dummy that guards.
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
- `loadSff` and `LoadSnd` borrow an SFF/SND already loaded under the same
  name (`findActiveSff`, `findActiveSnd`): `debugReloadChar` therefore keeps
  them; `debugReloadSprites` hides the old SFF's name while loading, then
  rebuilds the palettes as the loader does (`palettedata` from the SFF,
  `loadPalettes` for the ACT files) and compiles the animations again (they
  hold the SFF); `debugReloadSounds` uses `LoadSndFiltered`.

Input and commands (`input.go`):

- A player's `InputBuffer` (shared by all of its `cmd[]` lists) counts, per
  key, frames held (> 0) or released (< 0): `Ub Db Bb Fb` (B/F already
  follow the facing) and `ab`..`mb`. The keys held "since" = the smallest
  absolute count, so the app rebuilds an exact history even when it skips
  frames.
- `c.cmd[c.playerNo].Commands[Names[name]]` holds every definition of a
  name, in the CMD file's order, then the common commands
  (`sys.cfg.Common.Cmd`) appended after. `completed[i]` marks steps done
  (only the last one done stays set), `curtime` runs from the first step,
  `curbuftime` > 0 is what `command = "name"` tests.
- A ";" in a `command =` value starts a comment (kfm's SmashKFUpper has a
  second motion after one that is never read).

Triggers (`script_debugger.go`, `bytecode.go`):

- CNS: each `triggerall`/`triggerN` line is compiled alone (`fullExpression`)
  then joined with jumps, so `OC_debugrec <source>` appended to each piece
  records exactly the lines evaluated (short-circuits leave the rest
  unrecorded). Constant triggers (`trigger1 = 1`) are folded away and get
  no source. ZSS: the `if` expression of `subBlock`; its line is the one of
  the `{` (`c.i` after `readSentence`).
- `gameTime()` (`matchTime`) moves in `globalTick`, after the characters:
  the values are stamped with `gameTime()` when the frame is shared, which
  is the tick the mod reports for that frame. Snapshots (stdout) and frames
  (shared memory) can be a tick apart, so the app keeps values per tick.
- Sources of type "trigger" outnumber controllers (ryu_pots: 11321 sources
  in all); the mod still sends them all within the first seconds.

Stages (`stage.go`, `editor_stage.go`):

- `s.bg` follows the def's sections whose name's first word is "bg", in
  order (`[BGdef]`, `[BGCtrlDef x]` are not BGs; `[BG]` alone is one):
  `SectionName` cuts at the first space and lowercases. `s.bgc` follows the
  "bgctrl" and "bgctrl3d" sections; a BGCtrl without ctrlid acts on its
  BGCtrlDef's BGs, or all of them. `bgCtrlAction` runs before `tick`
  increments `stageTime`; a BGCtrl runs when `stageTime >= start` and,
  with a looptime, `(stageTime - start) % looptime <= end - start`. A positionlinked BG's
  `start` is added to its link's at load; `debugStageBgMove` moves the BGs
  linked to the one moved, as a reload would.
- `backGround.drawTransformed` ends in `Animation.Draw(x, y, xcs, ycs, ...)`
  then `RenderSprite`: the sprite's quad is where `initRenderSpriteQuad` puts
  it (`x = ±rp.x + rcx`, top `= -(±rp.y - rcy)`, in the game picture's
  pixels) and one unit of `start` is `xcs * bgscl * widthScale` pixels
  (`ycs`, `heightScale` for y; `bgscl` 0.5 for hires stages). Tiled BGs: the
  rectangle is the base sprite; rotated ones: the unrotated quad.
- Reloading: `loadStage(def, false)` (true would load the roundXdef stages
  again), then `sys.stage`, `sys.stageList`, `sys.cam.stageCamera`, and the
  stage copies in `roundBackup`/`matchBackup` (F4 and Reset round restore
  `*sys.stage` from them) are replaced, and `sys.cam.Reset()` takes the new
  bounds. Save state/Load state still bring the old stage back. 3D model
  stages are not handled.
- A BG's sprite (`anim.spr`) is only set when the stage steps
  (`Animation.Action`), and `Stage.draw` skips BGs without one: a stage
  loaded while paused draws nothing until `anim.UpdateSprite()` runs.
- Camera: `cam.action` gives (x, y, scale) for the game (characters'
  screen bounds); what is drawn comes from `zoom.computeCamera` (zoom
  effects) and `System.draw(x, y, scl)`, where `editorCamera` can replace
  it. `cam.Pos[1]` is premultiplied by the scale. The world (a character's
  `pos * localscl`) is at picture pixel `((X - x) * Scale + gameWidth/2) *
  widthScale, (Y * Scale - y + GroundLevel()) * heightScale`; the editor's
  geometry uses the camera of the frame drawn (it puts it in `sys.cam`
  while `DebugClsn.Add` runs), so boxes stay right under zoom effects.
- The stage's values are in its localcoord: world = value * `s.localscl`.
  How far the camera goes: x from `cam.minLeft` to `cam.maxRight` (screen
  edges at the zoomout), y from `boundH - above` to `boundLo + below` (above
  and below the ground line on screen).

Fight screen (`fightscreen.go`, `common.go`, `editor_fight.go`):

- `loadFightScreen` reads each section (`SectionName`: "[Simul Lifebar]" is
  name "simul " plus subname "Lifebar") into elements; every drawable one
  holds a `Layout` (read by `Layout.Read(pre, is)`, e.g. pre "p1.bg0.") and
  draws through `Layout.DrawAnim`, `DrawText` or `DrawFaceSprite`, which end
  in `RenderSprite` (sprites, sprite fonts' glyphs, portraits; TrueType text
  does not, so it has no rectangle yet). `Layout` is copied by value
  everywhere, hence the source index kept in the struct (`Layout.editor`).
  Sections missing from the def are appended empty ("[tag lifebar]"...) and
  their layouts point at the single/simul ones.
- Positions: an element's pos (`p1.pos`, `pos`...) plus its `offset`, in
  the fight screen's localcoord; one unit is `scale * widthScale` picture
  pixels across, `scale * heightScale` down (`scl` of the draw calls).
  Player-numbered keys can be written with operators (`p*.pos`, `p1|p3.pos`,
  `resolveLifebarPrefixOperators`, for the bases `p` and `team`; when both a plain key and a selector give one, which wins depends on Go's map order). The editor finds them (`Ini_DefKeyFor`: the plain key, else `p1|p3`, else `p*`) and edits the key the def has, which moves every element it selects.
- Reloading: `loadFightScreen(textFile)` then the runtime parts are carried
  over (as `FightScreen.reload` does, plus the round's display timers and
  fades, win icons, lifebar values, portraits); `reset()` rebuilds the
  layouts in use and the team order. Reading `fight.snd` (6 MB), `fight.sff`
  and 9 fonts takes ~220 ms; borrowing them (`findActiveSff`,
  `findActiveSnd`, `loadFnt`) leaves ~20 ms. Animations get no sprite until
  they step: after a reload, every `Animation` of the new fight screen is
  found by reflection and `UpdateSprite` runs (a paused game draws them).
- Save state/Load state bring the old fight screen back (`FightScreen.Clone`).
- `findActiveSff` does not look at the fight screen's, motif's or
  storyboard's SFF (only characters, stage, common FX): without `keep` a
  reload reads them again; with it, `editorKeptSff` lends them unless
  `edFight.newSff` is set.

Motif (`motif.go`, `iniutils.go`, `editor_motif.go`):

- `loadMotif` reads system.def into the `Motif` struct by its fields' `ini`
  tags (`title_info` is [Title Info], `menu.item` a prefix of its keys),
  then `PopulateDataPointers` walks it by reflection and creates the
  `AnimData *Anim`, `TextSpriteData *TextSprite`, `RectData`, `PalFX`, `Fade`
  of each property. Its map case drops the parent's path, hence the editor's
  own walk. The Lua function `loadMotif` turns the struct into the global
  `motif` table (userdata for those pointers); the scripts draw them every
  frame (`textImgDraw`, `animDraw`, which queue a copy: `ts.Copy()`, `*a`, so
  the source is a field the copy keeps) and read the table's values every
  frame (`m.pos`, `m.item.spacing`...), but set some things once when a
  screen starts (`textImgSetWindow` on the menu items, main.lua:3096).
- Reloading in place: the scripts' `loadMotif` is called again, the old
  objects take the new values (field by field: what the scripts changed and
  the new def did not stays) and replace the new ones in `sys.motif`, the new
  table is merged into the old (tables and userdata kept), and the runtime
  parts (`ch`, `co`, `me`..., fades) are the old ones. `loadMotif` costs
  ~350 ms of its own (inheritance between sections, every key set by
  reflection); reading the SFF and fonts again ~1.5 s more (borrowed).
- In a match the motif draws little (pause menu, dialogue, win screens).
- Storyboards (`storyboard.go`): `loadStoryboard` reads the def into the
  `Storyboard` struct the same way (`[Scene 1]` is the map key "scene_1", its
  `layer0.*` keys `Layer["layer0"]`); a scene's BG is a BGDef named by its
  `bg.name`. The scripts' `loadStoryboard` puts it in `sys.storyboard`, which
  `runStoryboard` steps and draws each frame (layers drawn as copies of their
  `AnimData`/`TextSpriteData`, at `layerall.pos + offset`); the scripts do
  not hold its objects, so a reload replaces it whole and copies the time,
  scene, typed text and BG times. `-storyboard path` plays one and exits
  before the mods are loaded (no bridge).
- A screen's BGs are a `BGDef` (`bgdef.go`): `loadBGDef(sff, model, def,
  bgname)` reads the def's sections whose first word is the bgname
  ("titlebg" for `[TitleBGdef]`), in order, as a stage reads its BGs; the
  BGDef does not keep that name (the editor takes it from the motif's field,
  `titlebgdef`). One unit of `start` is `scale * localscl * widthScale`
  picture pixels when drawn at scale 1 (menus).
- The roster (`main.lua`, `start.lua`, `system.go`): `select.def` is read
  once at start by the scripts (`main.f_loadSelectDef`, a top-level block
  until this work wrapped it, without reindenting), which add each
  character and stage to the engine (`addChar`, `addStage`: `sys.sel`'s
  `charlist`, `stagelist`) and to their tables (`main.t_selChars`,
  `t_selGrid`, `t_charDef`, `t_selStages`, the orders, slots, unlocks);
  `start.f_makeGrid` places them in the grid. A preload worker (goroutine)
  loads portraits into `charlist[ref]` by index: `debugSelectReset` waits
  until it loads none. The select screen keeps a draw list
  (`start.needUpdateDrawList`). From a select screen to its game mode's
  end, the characters chosen are in use: the mod wraps `start.f_selectMode`,
  `f_selectScreen` and `f_selectChallenger` (each runs its own loop) to know
  where the game is, and refuses a reload there.
- Fonts (`font.go`): `loadFnt(file, height)` reads a `.fnt` (MUGEN 1.0: a
  PCX and a char map in one file) or a `.def` (`[Def]`: type bitmap or
  truetype, size, spacing, offset, colors, banktype, file); a bitmap font's
  glyphs are its SFF's sprites (group 0, or every group with `banktype =
  sprite`; the number is the character's code), drawn by their axis from
  the pen, which moves by the sprite's width plus `spacing`; a space or a
  missing character moves by `size`. Texts hold the `*Fnt` (`TextSprite.fnt`,
  the fight screen's `fnt[]`), so reloading copies the new font into the old
  one. A `.fnt` with several palettes queues `sys.mainThreadTask`s while
  loading (a buffered channel, run each frame): its other banks fill in the
  next frame. TrueType glyphs are drawn by each renderer's `Printf` in the
  game's coordinates (y down, `gameWidth` x `gameHeight`): picture pixels
  are those times `widthScale`, `heightScale`. Vulkan's is not hooked
  (frames are not shared from it).
- A BGDef just loaded has no velocity nor sin (`bga` is zero): the
  `backGround.reset()` that a screen starting runs (`BGDef.Reset`, through
  the scripts) gives them from the def. A reload therefore resets the new
  BGs, then gives them the old ones' moved distance (`bga.pos`), sin time
  and visibility.

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
- Not in netplay or rollback matches (no stdin bridge there).
- Frames are shared with the OpenGL renderers only (not Vulkan).

Done since (mod and app only): a Watch window (`ikdbg.exprSet`: each
expression read with the snapshot, `w`); hits taken in Trace (a `got`
event when `getHitVar('frame')` is true: the engine sets it in hit
detection and clears it in the next tick's `actionFinish`, so the loop hook
sees it on the hit's tick; `ghv.damage` is still the pending damage then);
the watched entity's explods and projectiles in Inspector
(`editor_objects.go`, `debugObjects`: all of them in one call; an Explod
without an ID gets 0, so `numExplod` counts it too); move frame data
(Frame data, tab Moves: each tick's row says whether the element has a
Clsn1 box, `clsnVar('clsn1', 0, 'back')` not NaN; the moves are found in
the history by `FindMoves`, a tick counting as a frame when the previous
one had no hit pause left); an "only if" condition on controller
breakpoints (`ikdbg.ctrlBrkSet(src, on, cond)`, read after the tick on
the entity that ran it: a controller "runs" when its triggers held, as
`tracedSctrl.Run` records).

Phase 4, then: the dummy's reactions (`ikdbg.dummyReact`: a take started
early enough that its last tick is the first one the dummy can act on;
measured on KFM by a test hook recording the dummy's state, ctrl and hit
vars each tick: from a hit or guard stun with shake s > 0 that is
`s + hittime + 2` ticks away, with no shake left `hittime + 3` (hittime
counts down to -1 once the shake is 0, the hit state ends the next tick
giving ctrl, and [Statedef -1] sees ctrl the tick after); from getting up
(state 5120) `-AnimTime + 2`; a 3-tick take ending on a button hit on that
tick in all three cases); the keys held under each player on the Game
window; combo damage (summed while `GetHitVar(hitcount)` goes on) and
juggle points left (`jugglePoints(target)` on the attacker) in Trace; the
character each take was recorded with, and a filter for those in the
match.

Then: random guard (a chance rolled when `inGuardDist()` turns true, once
per attack) and a chance for the reaction (rolled per hit); the input
history on the Game window (runs of keys built from the frames' held
counters: the smallest is the ticks since the last change); getting up
recognised as any state after lying down whose movetype is no longer H,
without ctrl (5120 is `type L`, `movetype I`), the lead checked again on
ryu_pots (it uses the common 151 and 5120 too: ctrl on 1722, the move on
1723 after guarding; 3771 and 3772 getting up).

Phase 4 next steps: a character whose get up state does not end at its
animation's end would start its reversal late (by the fallback, once
control is back); none here to check it with.

Phase 5, the analysis of a character's files (app only, but for
`debugCharFiles` naming the common files), begun: `src/cns.cpp` reads CNS
and ZSS state files (statedefs, ZSS functions, controllers and their
parameters, every line of code with its state; `tests/cns_test.cpp`: the
counts match the engine's sources, but a Null controller the engine does
not compile), and window Analysis has the Lint (`app_check.cpp`: a model
of the character made again when a text changes), Variables (uses found
by a regex over every line of code, `:=` a write, and the setters'
parameters; names saved as `VarName=def|kind|index|name`) and States (the
graph from the model's state references). A duplicate statedef in one
file kept the engine on "NOW LOADING" in a test: Lint warns of it.

Then the Lint over a ZSS character (kfm_zss): its headers go over lines
("[StateDef 181;\n type: S; ...]"), a controller's triggers are the
conditions of the ifs around it (what `SelfAnimExist` guards), and the
negative statedefs of several files are put together, not duplicates.
kfm_zss gives the same as KFM: no error, the same 4 notes.

Phase 5 next steps: a CNS expression's numbers (`ifElse(..., 200, 210)`)
are not followed; the state graph knows nothing of the engine's own
changes (landing, hits).

