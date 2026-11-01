# Ikemen GO debugger: state of the work

Handoff notes for whoever continues this work (people or Claude sessions).
[README.md](README.md) describes the debugger for its users.
Last updated 2026-11-01.

## Where it is going

The goal grew from a debugger into an editor of everything Ikemen GO lets
people customize (characters, stages, screenpacks...), with the engine showing
the result live and the things on screen clickable. Decisions taken with the
user (2026-10-25):

- **The editor stays a separate program**, and the game's frames are shown
  inside it (window Game) through shared memory; the editor draws its
  overlays and does the picking itself, from geometry the engine sends with
  each frame. Not ImGui inside the engine, not an overlay window.
- **A fork, not upstream** (2026-10-31): Ikemen GO's authors prefer this out of
  their tree, so it lives in its own repository, ikemen-studio, with the engine
  in it. Engine code still stays in new files (`src/editor_*.go`,
  `src/script_debugger.go`) with one-line calls elsewhere, inert without
  `-ikdbg`, now to keep merges easy (see "Syncing with upstream").
- **Characters first**, then stages, then screenpacks.

**The interface was rethought for a character maker** (2026-11-01; the user
found it functional but intimidating). It is organized by task, not by tool:
- `src/app_start.cpp`: the start page (what do you want to do? each choice a
  workspace; skipped with `--launch`, so test scripts do not see it) and the
  first steps, in a corner, each going on when the user has done it.
- Panels (`app_layout.cpp`): 27 windows became 12 areas. A row of `g_windows`
  with a `panel` is a tab of that window; `PanelFrame` begins the panel with its
  tabs on its menu bar (which does not scroll), and `BeginTool` of the tab shown
  begins the same ImGui window again, appending to it. So the windows did not
  change, and `ShowWindow("Sprites")` brings the panel and the tab. Layouts name
  panels only, and at most four areas each.
- Advanced tools (`ToolWindow::advanced`): tabs hidden until View > Advanced
  tools is on or `ShowWindow` asks for one. Lab and Frame data are not advanced:
  testing a move is a character maker's work.
- The workspaces are a rail at the left; the menu bar is File and View; the
  toolbar shows the match as one button (its fields in a popup).
- `SetPickMode` (Fighters, Stage, Screen): one choice on the Game window, set by
  the workspace, with a line saying what is selected and opening its editor. The
  right side of each workspace already is the editor of what is selected, so no
  separate Properties window was made.

Still heavy, for a next pass: Animation's and Lab's many controls and terms
(Clsn, onion...), and Code's state panel beside the editor.

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
tests it; later a canvas of its own, `AnimPreview`, and an onion skin, see
stage D); sprites (done: window
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

The shape of an IDE around the live editing (decided with the user on
2026-10-26, branch `ide-shell` off `debugger-bridge`): the projects come
from disk, the game being the live preview, and nothing live is replaced:
with the game running everything works as before, the IDE parts adding to
it; without it, what can be done from the files works. Stage A, done:
Explorer (`app_explorer.cpp`: characters, stages, screenpack, common files,
each def's files found as the engine finds them, the ones in the match
marked, a character put in the match from it), Code's tabs (a preview tab,
kept ones saved with the settings, Ctrl+W asking about changes), the palette
(`app_palette.cpp`: Ctrl+P files, Ctrl+Shift+P commands, Ctrl+G line,
Ctrl+Shift+O section) and the File menu, the status bar (game state with its
colour, Code's cursor, files not saved). Stage B, done: the character worked
on (`WorkPn` in `app_explorer.cpp`): the selected player's, or the one
picked in Explorer since (a player picked takes it back; offline, the
toolbar's P1). One not in the match is read from disk as the player
`kDiskPn` (1000), its files listed as `editorCharFiles` lists them (the
[Files] keys sorted, st0..st9 as "st", a state file not found as its .zss;
config.ini's common states, AIR, CMD), so `CharFile`, `FilesOf`, `ModelOf`,
`SndOf`, `SpritesOf` work unchanged (they go through `FilesFor`; the loops
over `g_charFiles` stay the match's players). Analysis, Sounds, Commands (its
CMD part) and Sprites use it; Animation and Create's Move too since (stage
D and after). Stage C, done: navigation. The editor (`editor.cpp`) got
highlighted ranges, Ctrl+click, a selection that does not take the focus, and
`Editor_Replace` (an edit from outside as one undo step, the caller's
version bumped so Sync keeps the undo). Code: the find bar (Ctrl+F/H, F3);
F12, Ctrl+click and Shift+F12 through `GoToDefinition` (`app_check.cpp`: the
word at the offset, the parameter on that line in the model's parse
(`ParamAt`) and the `StateParams`/`ActionParams` tables, or the identifier
before it in a trigger; the model of the character the file is of,
`ModelWith`). `app_navigate.cpp`: Search (find in files, references) and
Outline. Opening a file to keep it, going to a line and a result clicked
give the editor the keyboard (`g_codeFocus`). Outline and Search are in the
default arrangements, not in ones saved before (Windows menu, or Workspace >
Restore). Stage D, done: Problems (`app_problems.cpp`: Lint
(`LintProblems`), the stage's checks (`StageProblems`, `StageChecks` kept
apart from the Stage window), Apply's error lines, the engine's `WARNING:`
and `Error:` lines kept by `Log` through `EngineLine`, their file a name the
players' files resolve), counted on the status bar (gathered at most every
0.3 s: the Lint's model hashes the character's files). Animation works on a
character from disk (`GetAnimTarget` with `WorkPn() == kDiskPn`), with a
preview drawn by itself (`AnimPreview`: the element's sprite and boxes, the
action played). F5 launches, Shift+F5 stops, Ctrl+Shift+F5 restarts;
matches saved by name (`Match=` lines). Launch no longer empties `g_files`:
changes made with the game not running stay. Not done: the Sprites window's
files as tabs (an SFF opened from Explorer shows; one at a time).

Then Animation and Create without the character in the match. The preview
edits everything the Game window's drag did: its **Drag** choice is
`g_animDrag` (boxes, or the sprite's offset, dragged anywhere), the arrow
keys move the offset or the box under the mouse (the preview child has
`NoNavInputs`, so they do not move ImGui's focus), and a line marks the
ground. The onion skin draws the element before and the one after
(`OnionElem`: the loop start's after the last), sprites tinted
(`kOnionBefore`, `kOnionAfter`) and boxes, on the preview and on the game.
Create's Move works on `WorkPn()`, so a character from disk gets its move
written (`StatedefFile`, `AddCommandEntry` and `CommandNames` already went
through `FilesFor`); without the match its button is Put it in P1's place.
A character created becomes the one worked on (`WorkOn`), and without the
game it is put in the match the game starts with (`g_settings.p1/p2`, as
Explorer does); a stage, the toolbar's. `SelectDef()` gives the motif's
select.def from disk when the game has not named it. Tested without the
game: a character made from kfm, action 0 copied as 200, a Clsn1 box added
on the preview, the move created; then launched with it as P1.

Then Code's completion and documentation by context, and ZSS. `editor.cpp`
knows Ikemen GO's names from `ikemen_names.inc`, written by
`scripts/gen_names` (Go, `go/ast`) from the engine's compiler: `scmap`'s
controllers, each with the parameters its method reads from its section
(the calls taking `is`: `paramValue`, `stateParam`, `is["x"]`..., followed
into the methods it hands `is` to, a `prefix` parameter carried: `palfx.`,
`afterimage.`, `shader.`), `expValue`'s cases of `switch c.token` as triggers
with the names a nested `switch c.token` takes (GetHitVar's, Const's), the
statedef's parameters, the spellings of `script.go`'s Lua functions. Run it
again when the engine's compiler changes. `ContextAt` says what fits where:
CNS by the section and its `type =` line, ZSS by the `{` the cursor is in
(a controller's or a block's), `;` and `:` before it, the start of a
statement; `Editor_SetZss` (Code sets it for `.zss`) gives `#` comments and
`HighlightZss`. `Editor_SetHoverNote` lets the app add to a word's tooltip:
`CodeHoverNote` in `app_check.cpp` (a variable's name and uses, the state or
action a number names). Analysis > Variables renumbers a var or fvar
(`PlanRenumber`: per line, outside comments; VarRangeSet uses are marked
`range` and do not make a number used). Not done: completing `$locals`, ZSS
functions' names, a command's name in `command = "`.

## Where things are

Branch `main` of ikemen-studio, merged from upstream `develop`. The first
commits, from when it was `debugger-bridge` on top of e9bfe112:

| Commit | What |
|---|---|
| d0910b9c | Engine side: `src/script_debugger.go` (new, ~210 lines), and one or two lines each in `compiler.go`, `bytecode.go`, `script.go` |
| ec0c018c | `tools/debugger`: the debugger program, the mod, the scripts and these notes |
| 5738b9aa | `CLAUDE.md`: rules and environment for Claude sessions in this clone |


Files:

```
src/script_debugger.go   engine side: controller sources and trace, setters, Lua functions
src/editor_frame.go      engine side: frames, geometry and input events in shared memory
src/editor_shm_*.go      the shared memory (Windows; a stub elsewhere)
src/editor_reload.go     reloading a character during a match (debugReloadChar)
src/editor_palette.go    a palette's colours put in place during a match (debugSetPalette), a player's palette (debugPalNo)
src/editor_input.go      engine side: players' input buffers and commands under way, for the geometry
src/editor_stage.go      engine side: where the stage's BGs were drawn, moving a BG, reloading the stage
src/editor_fight.go      engine side: where the fight screen's elements were drawn, reloading the fight screen
src/editor_motif.go      engine side: the same for the motif (system.def), reloaded in place
src/editor_storyboard.go engine side: the same for the storyboard playing
src/editor_select.go     engine side: select.def's characters and stages emptied to be added again
src/editor_font.go       engine side: every font loaded, a font's file read again in place
src/editor_objects.go    engine side: a character's explods and projectiles (debugObjects)
src/editor_lab.go        engine side: a player's keys held, recorded, played; a character swapped
src/editor_model.go      engine side: the stage's 3D model (nodes, materials, animations, lights), the glTF names kept, nodes outlined and picked, Save
tools/debugger/
  CMakeLists.txt         ImGui static lib + ikemen-debugger.exe; copies mod/ikdbg.lua next to the exe
  src/main.cpp           Win32 + OpenGL2 window and input for ImGui, DPI aware; test hooks (below)
  src/app.cpp            the debugger: state, commands, protocol handling, settings
  src/app_layout.cpp     the menu bar, the workspaces (their arrangements, kept and restored), BeginTool, the status bar
  src/app_explorer.cpp   Explorer window: the projects' files from disk; OpenPath (a file to the window that edits it)
  src/app_start.cpp      the start page, the first steps, EmptyNote
  src/app_palette.cpp    the palette (Ctrl+P, Ctrl+Shift+P, Ctrl+G, Ctrl+Shift+O) and the File menu
  src/app_navigate.cpp   Search (find in files, a state's references) and Outline windows
  src/app_problems.cpp   Problems window: Lint, stage checks, Apply, the engine's warnings
  src/app_perf.cpp       Performance window: the game's frames by phase (debugPerf), this program's frame
  src/app_theme.cpp      the Theme menu: the themes, Ink() (status colours by theme), the editor's palette
  src/app_internal.h     what the app_*.cpp files share (state, each file's functions)
  src/app_game.cpp       Game window: frames, boxes over them, dragging, keys to the game
  src/app_anim.cpp       Animation window (AIR)
  src/app_sprites.cpp    Sprites window (SFF), file dialogs
  src/app_sffpal.cpp     palettes: a character's (ACT files, SFF 1,N), the Palette tab, sent to the game live
  src/app_sounds.cpp     Sounds window (SND)
  src/app_code.cpp       Code window: source files, Apply
  src/app_cmd.cpp        Commands window: input, commands under way, the CMD file
  src/app_stage.cpp      Stage window, and the BGs on the Game window
  src/app_screen.cpp     Fight screen, Motif and Storyboard windows, and their elements on the Game window
  src/app_debug.cpp      the toolbar, Players, Inspector, Frame data, Trace, Breakpoints, Watch, Console
  src/bridge.cpp/.h      starts Ikemen_GO.exe with pipes, reads its stdout/stderr lines, writes stdin
  src/frame.cpp/.h       reads the shared frames, writes input events and commands
  src/editor.cpp/.hpp    the code editor widget (with marks and line backgrounds); CNS and ZSS, completion by context
  src/ikemen_names.inc   the engine's controllers (with their parameters), triggers, statedef parameters (scripts/gen_names)
  src/air.cpp/.h         AIR files: actions, elements, Clsn boxes with their lines; text edits
  src/sff.cpp/.h         SFF files: read v1/v2, decode (raw, RLE8, RLE5, LZ5, PNG), write v2, PNG in and out
  src/snd.cpp/.h         SND files: read, write, WAV descriptions
  src/cmd.cpp/.h         CMD files: the [Command] sections with their lines; text edits; a motion's steps
  src/ini.cpp/.h         def files (stages, fight screens, motifs): sections and fields with their lines; text edits
  src/cns.cpp/.h         state files (CNS and ZSS): statedefs, controllers, lines of code (Analysis)
  src/stagecheck.cpp/.h  a stage def's checks (Stage window): files, sprites, actions, ids, bounds, keys
  src/make.cpp/.h        text written for new things: a ChangeState in [Statedef -1], a move's statedef (CNS or ZSS); a new character's states, actions, CMD
  src/app_create.cpp     Create window (the menu bar's Create): a move, a character, a stage of PNGs
  src/app_model.cpp      Model window: the stage's 3D model (the match's, or read from disk), its placement, its nodes on the Game window
  src/app_modelview.cpp  the Model window's preview: a glTF drawn by OpenGL's fixed pipeline into a framebuffer, textures by WIC
  src/gltf.cpp/.h        a glTF or glb read as model.go reads it, posed, picked, written back (its JSON edited)
  third_party/lodepng    lodepng (commit e584a849, zlib licence) for PNG
  tests/                 air, sff, snd, cmd, ini, roster, fnt, cns, make, stagecheck, gltf _test.cpp (scripts/build.ps1 test)
  src/json.h             small JSON parser for the protocol
  mod/ikdbg.lua          the in-game side (Lua mod, installed into external/mods at launch)
  third_party/imgui      Dear ImGui 1.92.9b docking (only the files used)
  third_party/fontawesome Font Awesome Free 6.5.2 Solid (fonts OFL, icons CC BY 4.0): CMake embeds the TTF (build/fonts.cpp), main.cpp merges it into the interface's font; src/icons.h has the ICON_* names (UTF-8, generated from the font's CSS). Use them by literal concatenation: ICON_SAVE " Save", and measure a button by that same label; windows' titles stay plain text (they are the layout's ids)
  third_party/fonts      Roboto 2.138 (Apache 2.0) for the interface, JetBrains Mono 2.304 (OFL) for code: embedded the same way, size 14 (main.cpp). Roboto over Inter because its digits are all as wide, so numbers that change each tick stay still; stb_truetype reads no OpenType features and no variable fonts, hence static TTFs. dbg::Editor_Font() gives the code font: PushFont(dbg::Editor_Font(), 0.0f) for code outside the editor (Console, Create's preview). Neither has kana or kanji
  scripts/               build.ps1/build.sh, msys_setup.ps1, test_ui.ps1, probe.ps1, gen_names (Go: ikemen_names.inc)
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

## Build environment

- MSYS2 at `$env:MSYS2_ROOT`, UCRT64: gcc 16.2, SDL2 2.32.10, libxmp
  4.7.4, cmake, ninja. Installed for this work by `scripts/msys_setup.ps1`.
- Go 1.27.3 at `C:\Program Files\Go` (the build scripts put it on the PATH).
- `scripts/build.ps1 [ikemen] [debugger] [test]` runs `scripts/build.sh` in
  MSYS2, and stops if `MSYS2_ROOT` (or `-Msys`) is not an MSYS2. Ikemen's `build/build.sh Win64` also builds libvpx and ffmpeg the first
  time (long), copies their DLLs to `lib/`, and writes `./Ikemen_GO.exe`.
- PowerShell 5.1 strips the inner quotes of native arguments: run MSYS2 work
  as script files, never as `bash -lc "..."` with quotes inside.
- The debugger also builds with Visual Studio 2022 (x64 dev shell, `cmake -G
  Ninja`); the MSYS2 build links statically, so the exe needs no DLLs.
- Release assets (`chars`, `stages`, `data`, `font`, `sound`, `video`) were
  copied from the v1.0.0 Windows release; git ignores them. Test
  characters in `chars/`: kfm, kfm720, kfm_zaxis, kfm_zss, ryu_pots.

## Syncing with upstream

Remotes: `origin` is ikemen-studio; `upstream` is ikemen-engine/Ikemen-GO, with
push disabled. `develop` mirrors upstream and is not pushed. `main` follows
upstream `develop`, not `release/1.0`, and takes it by merge: rebasing would
rewrite all of this history at every sync.

- `scripts/sync_upstream.ps1 [ref]` fetches and merges (default
  `upstream/develop`). Then `scripts/build.ps1`, `scripts/build.ps1 test` and a
  match through `scripts/test_ui.ps1`: the hooks can stop working without any
  conflict or compile error.
- Upstream's workflows and `dependabot.yml` are removed from the fork: they use
  its tokens, publish its releases, and changed 44 times in the year to
  2026-11-01. The script takes them out of every merge, changed or new.
- `.github/workflows/upstream-check.yml` tries the merge every Monday, builds
  and runs the unit tests. It pushes nothing; a failed run is the warning that
  upstream changed something `editor_*.go` uses, which is a compile error and
  not a conflict.
- The upstream diff is what can conflict: `git diff upstream/develop main --
  src external ":!src/editor_*.go" ":!src/script_debugger.go"`. Calls stay on
  lines of their own, next to upstream's. Still edited in place: the model call
  in `stage.go` (a hook in `loadglTFModel` would add more lines than it saves)
  and `local t_storyModeList` in `main.lua`, which a function could not leave
  inside it.
- `rerere` is on in this clone: a conflict resolved once is resolved again.
- Only `main` is pushed (`git push origin main`), never `--all`.

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
  Fight screen, Motif, Storyboard, Roster and Fonts windows are in the Stage
  and Screenpack workspaces: `ctrl:4` and `ctrl:5` show them (`ctrl:1`: back
  to Debug, `ctrl:2` Code, `ctrl:3` Character); Stage, Fight screen, Motif
  and Storyboard are the right column's tabs, Roster and Fonts the left ones
  (take a shot for the coordinates, the Game window's
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
- File dialogs: with the environment variable `IKDBG_TEST_PICK` set (paths
  separated by `|`) before `test_ui.ps1`, every "Open" dialog (`PickFiles`)
  returns those files instead of opening. Create tests write new folders and
  files (`chars/<folder>`, `stages/<file>.*`) and add to `data/select.def`:
  delete the ones made and restore select.def after.
- Lab tests: with `-IniExtra "AI1=0`nAI2=0"` nobody moves on their own;
  `lua:debugInputHold(1, 'F')` stands for held keys (they are recorded like
  real ones), `ikdbg.record(1, true)` and `ikdbg.stopTake()` record a take
  (it shows in the log as "Take N: P1, n ticks" and is saved in the ini),
  and a run without `-Fresh` keeps it to play from the Lab window (its tab
  is a bottom tab of the Debug workspace; take a shot for its buttons).
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

The windows were then put in order (`app_layout.cpp`): every window goes
through `BeginTool` (closed from its tab or the menu bar's Windows, opened
again with the others of its place), and Layout puts them in Debugging or
Stage and screens; a line asked from another window brings Code to the
front.

Phase 6, creation (the app writes text, `src/make.cpp`, tested in
`tests/make_test.cpp` by reading it back with the parsers): the Commands
window shows a command's motion as steps edited on a pad (`Cmd_Steps`;
what Ikemen allows beyond them stays as text) and puts a ChangeState on a
command at the top of the first `[Statedef -1]` (`AddCommandEntry`, CNS or
ZSS); the Create window (menu bar's Create) makes a move (its statedef at
the end of the first state file, as KFM writes its attacks, with its
command; tried with `selfState`), a character from another one (its
basics: constants, states below 200 and ZSS functions, the common states'
actions, a new CMD; or a full copy; `SwapChar` puts it in the match, and
`OnFiles` drops the old character's files a swap may still bring back),
and a stage of PNG images (`Sff_SpritesOfPngs`, the def's camera bounds from
the images, `debugSwapStage` in `editor_stage.go` putting it in the match).
Tested on kfm and kfm_zss: the moves hit, the characters stand, walk and
pass the Lint, the stage is played on at once.

Then the 3D stage tab (the Stage window's "3D model", `app_model.cpp`, asking
`ikdbg.model()` while it is open): `editor_model.go` gives the stage model's
animations, nodes and lights, and turns them on and off or changes them;
the engine keeps no glTF names, so a line at the end of `loadglTFModel`
hands the document to `editorModelLoaded` (names kept by file, matched with
`edStage.modelFile`). Hidden nodes come back when a round starts
(`Model.reset`). No model at hand had lights: the test made a copy of
stage3d.glb with two (a directional and a point light) and its main material
not unlit, deleted after. A stage put in the match (`debugSwapStage`, or a
reload whose model file changed) now sends its model's vertices to the GPU
(the engine does it when a round starts, `handleStageSwap`) and swapping
restarts the camera (`cam.Init`); the fight's aspect stays the match's.

The 3D model then became a window of its own (Model, `app_model.cpp`, next
to Stage in the Stage workspace). `debugModel` gives the scenes (their
roots), every node's rest translation, rotation and scale and Ikemen extras,
the meshes' primitives, the materials and the lights' cones; changes go
through `debugModelNodeTRS`, `debugModelNodeProps`, `debugModelMaterial`,
`debugModelLight` and `debugModelAnimTime` (an animation put at a time: the
others held, `Model.step` from a tick before), each one remembered in
`edModel` for Save. Changing a rest value changes nothing while an animation
moves it (`GLTFAnimatableProperty`). The cached model shares its nodes with
the stage's copy (`*s.model = *model`), so the changes survive the def's
reloads. On the Game window: `editorModelDrawn` (one line in
`Stage.drawModel`) keeps proj * view; the selected and hovered nodes' boxes
(their mesh's box in their space, else the world box of the meshes under
them, else their position) go into the frame geometry's `stage.mdl` as
segments in picture pixels, cut at w = 0.001 so a box around the camera
still draws. `debugModelPick(x, y)` casts the ray from the near plane to the
far one (its parameter is the same in every node's space), tests each
visible mesh node of a scene, box first, then its triangles (positions at
`vertexBufferOffset + 4 * numVertices`, the element buffer per primitive;
skins and morph targets at rest). Save (`editorModelSave`) decodes the file
again with qmuntal/gltf without a file system (external buffers are neither
read nor written), sets the nodes' TRS (matrix dropped), extras (`trans`,
`layerNumber`, `castShadow` "0", `disableZTest`/`disableZWrite` "1",
`meshOutline`, as `loadglTFModel` reads them: a JSON false would count as
true), materials (unlit through `KHR_materials_unlit`) and lights (the raw
`KHR_lights_punctual` JSON), and writes it (glb kept glb; a .bak first), its
time then taken as the cache's. Revert zeroes that time and reloads the
stage. Placement edits the def's `[Model]` and `[Camera]` (a missing section
is added before `[BGdef]`). Tested on stage3d (kept and restored): picking
(the sky sphere, the gong stand), a node moved, a material tinted, an
animation scrubbed, Save then a fresh launch with the saved file, Revert,
`trans = "ADD"` written to the node's extras, fov dragged. Lights and `.gltf`
files with external buffers were tested later (see the animation keys).

Then the model without the game: `gltf.cpp` reads a .glb or .gltf with the
app's own JSON reader (which can now edit and write: fields keep their
order, numbers are written as the shortest text reading back the same, a
float edited here as the shortest that reads back as that float), its
accessors (any component type, normalized, strided; sparse ones read as
zeros), strips and fans made lists, images decoded by WIC. It follows
model.go: a node's `matrix` is not read, Ikemen's extras are compared as
loadglTFModel compares them (`castShadow` "0"/"false" strings only;
`disableZTest` set unless "0"/"false"). Gltf_Save writes the same changes as
editorModelSave into the JSON tree and the file (a glb's binary chunk as it
was; a .gltf indented by 2), the rest of the JSON untouched;
`tests/gltf_test.cpp` checks that on a copy of stage3d.glb. The Model window
is live when the model shown is the match's (the one opened in Explorer, or
the stage's), else "from disk": the same lists and inspector work on a
`ModelView` filled from the file (`g_diskView`; `g_view` points at the one
shown) and their changes go into the `GltfDoc`. The preview
(`app_modelview.cpp`): the debugger's context is OpenGL 2 compatibility
(ImGui's OpenGL2 backend), so the fixed pipeline with client arrays, into a
framebuffer (`glGenFramebuffers` and the rest from `wglGetProcAddress`),
every attribute pushed and popped around it; base colour times its texture,
emission, alpha mask and blend, Ikemen's add/sub/mul (`glBlendEquation`),
depth test and write, both faces; lit by the model's lights (up to 4,
directional, point, spot) only when it has some, else unlit as the engine
draws a stage with no light and no environment map. "The game's camera" is
the engine's matrices at the camera's start (view = translate(offset) *
scale(scale), perspective fov, near, far), which with stage3d looks as the
game does. Tested without the game: stage3d drawn as in the game, a node
picked on the preview and outlined, turned around, a node rotated and
saved; the game then loaded the saved file with that node turned (the file
restored after).

Dragging the node selected (Game window in "3D model" mode, and the
preview): the frame geometry's "mdl" also carries, with a node selected,
the view-projection matrix the model was drawn with, the world matrix of
the node's parent and the picture's size; `DragTranslation` unprojects the
press and the mouse at the node origin's depth and adds the difference,
taken into the parent's space, to its translation at rest (Shift: along the
line of sight, the mouse's height), sent as any change of the inspector
(`debugModelNodeTRS`, or the `GltfDoc`). The matrices are the press's, so
the camera moving during the drag does not pull the node. Tested on
stage3d: the gong's stand followed the mouse on the game and on the
preview, within 3 pixels; the gong itself is animated, so only its place at
rest changes (the tooltip says so). The Game window's toolbar now goes on
in a second line when it does not fit (`SameLineFits`), which hid "3D
model" on narrow windows.

Then handles on the node selected (a gizmo drawn by the app, not ImGuizmo:
`MakeGizmo`, `GizmoHit`, `DrawGizmo`, `StepDrag` in `app_model.cpp`), in
three modes picked on the Game window's toolbar, the Model window's header
or the preview's: Move (arrows along the parent's axes, so an arrow changes
one number of the translation: the mouse's ray and the axis' line, their
nearest points), Turn (circles about the parent's axes and a white one about
the line of sight; the angle the mouse goes around the node's origin on the
screen, the other way when the axis points away; the turn put before the
rotation, `q * r`, as the parent's space has it), Scale (along the node's own
axes, the ratio of the nearest points' distances; the middle: the three
together). The frame geometry's "mdl" also carries "nw", the node's world
matrix (where it is drawn, animated or not). Undo and redo: every change goes
through `SendTRS`, `SendProps`, `SendMaterial` or `SendLight` (and
`SendLiveTRS` from the Game window), which `Record` it with the value it had
before the gesture (a copy of the nodes, materials and lights taken each
frame while no gesture goes on); a gesture ends once no field is active and
the left button is up (the Game window's picture is an Image, never active),
its changes becoming a step (none when they put everything back). Two
histories, the match's model and the one from disk, forgotten when the file
shown changes or on Revert. The Model window's header goes on in more lines
when narrow (its Save and Undo were cut off at the Stage workspace's width),
and the preview takes the height left under its toolbar. Tested on stage3d,
live and from disk: the stand moved along its X arrow (only x changed),
turned 90 degrees by the green circle and by the white one, scaled by 2
along X, each undone and done again (buttons and Ctrl+Z). Not tested: Ctrl's
15 degrees and Shift (the test script cannot hold them during a drag).

Then animation keys. The engine keeps a sampler's times in
`Model.animationTimeStamps` by the input accessor's index (samplers may share
them) and its values in the sampler: `debugModelAnimKeys(i)` gives an
animation's channels moving nodes (node, path, sampler, interpolation, times,
values; the numbers as strings of the shortest float32 text, since the mod's
JSON writes 6 digits), `debugModelAnimSampler` gives a sampler other keys (its
times under a key of its own, 0x80000000 | anim << 16 | sampler, so the ones it
shared stay; the duration found again; the animation posed again at its time
when on), remembered in `edModel.samplers`. Save (`editorWriteKeys`): an
accessor holding the same numbers is kept, one used by this sampler only and
of the same size is written over (float, not sparse), else a new one goes at
the end of the binary buffer (`modeler.WriteAccessor`; an input's min and
max set). `gltf.cpp` does the same without the game (`WriteKeys`, its JSON
and `doc.bin`; `GltfAnim::Channel` now knows its sampler; `doc.keys`). A
`.gltf` whose buffers are files of their own cannot have its keys written
(neither side writes those files). The app: `ikdbg.modelKeys(i)` answers
`@IKMKEYS` (`OnModelKeys`), asked when an animation's keys are first shown;
from disk the document's channels are used as they are. The keys show in the
inspector's column while the Animations tab is in front (the list's column was
too narrow). Undo knows keys too (`eKeys`: `RecordKeys` before a sampler's
keys change). With the Animations tab in front, the animation shown on and a
channel of it moving the node selected the mode's way, the handles change the
key at the animation's time (`DragKey`; added at the first move when there is
none; the right button takes it back) instead of the place at rest. Numbers
sent to the game for a node, material or light now have 9 digits (`%g` gave
6, so a save from the game rounded them). Tested on stage3d (6 animations,
250 keys a channel, baked): from disk, a key picked (the preview paused at
it), the gong dragged at it, undone; live, the keys asked from the game, the
gong dragged at key 36 on the Game window (the key's x 0 to 2.166), saved
(written over), read back from disk; a key added from Lua and saved (the
channel's accessors new, 251 keys read back); `tests/gltf_test.cpp` changes a
value (the binary chunk the same size) and adds a key (posed between keys, the
node where it was). The release's file restored after.

Lights and external buffers, on models made for the test from stage3d.glb
(a script in a scratch folder, deleted after; none at hand has lights): one
with three `KHR_lights_punctual` lights (directional, point, spot; the
materials no longer unlit) and one as a `.gltf` with its buffer in a `.bin`,
each with a copy of stage3d.def. Lights: the preview lit by them, the game
too; the red lamp's intensity changed and its node moved, saved from disk
(`Gltf_Save`) and from the game (`editorModelSave`), the file read back with
the values. External buffers: the game and the debugger read it; a node moved
and saved from disk and from the game, the `.bin` the same and the `uri` kept;
a key deleted and saved from disk gave "its buffers are files of their own:
animation keys are written into a .glb only", the file left as it was.

Themes (`app_theme.cpp`, the menu bar's Theme, `Theme=` in the settings):
Dear ImGui's dark and light and TheAncientOwl's twelve from imgui issue
#707, as posted but for the colours renamed since (TabActive, NavHighlight:
the build disables the obsolete names). A theme starts from Dear ImGui's
sizes and dark or light colours, then the screen's scale. Text coloured on a
window goes through `Ink(kRed...)`, darker on a light theme; drawings with
their own dark background (the game, sprites, the command chips, the roster
cells) keep their colours; grid borders and labels drawn on a window use the
theme's text colour. The code editor has a dark and a light palette
(`Editor_SetLight`).

The Console runs state controllers too (`editor_console.go`,
`debugRunController`): the text goes under a statedef of its own
(-2000000000), with a `[State]` header and `trigger1 = 1` added to CNS when
missing, and is compiled by `stateCompileCNS` or `stateCompileZSS` of a new
compiler set for the player (its constants, command list and ZSS
functions); it runs once with `c.minus = -2`, untraced, and a ChangeState
in it is taken at once (`stateChange2`; the new state's code runs on the
next tick). Error lines are the text's.

The Performance window (`app_perf.cpp`, `editor_perf.go`): one-line marks
in `system.go` end each phase of a match frame (top of the loop, after
`action`, after `uiAction`, before `cueDraw`, after `renderFrame`, and in
`await` once the frame is shown); `editorFrameCapture` times itself. The
engine measures only while the window asks (`debugPerf` every 0.25 s; it
stops 5 s after the last call) and keeps up to 600 frames between reads.
First result: the debugger's capture of the frame (`glReadPixels` before
the swap) takes about 5-6 ms a frame on this machine's Intel UHD, a third of
a 60 fps frame, with peaks past 50 ms; the game itself (kfm, ryu_pots) well
under 1 ms, the motif and Lua (the mod) about 1 ms. Rollback matches
(`hijackRunMatch`) are not measured. A frame of more than a second (a load) is
not kept. With it, the layouts' windows no longer take the focus as they
appear (`NoFocusOnAppearing`): the last one begun took its place's front tab
(Performance, measuring all the time); each place's first window is put in
front for a few frames after a layout, and a window opened from the menu or
by `ShowWindow` likewise.

Then the workspaces (`app_layout.cpp`), in place of the two layouts: Debug,
Code, Character, Stage and Screenpack, picked on the menu bar or with
Ctrl+1..5. Each is an arrangement written as text, `X<ratio>(left)(right)`,
`Y<ratio>(top)(bottom)`, `L:a,b*,c` for a node's tabs (* in front) and
`|name@x,y,w,h;` for the floating windows; it is built in the one dock
space with the DockBuilder (inactive dock spaces lose their empty nodes,
so each workspace is not a dock space of its own). Leaving a workspace
writes its dock space as that text (split ratios from the nodes' sizes,
tabs in their order); coming back builds it again, and the settings keep
every workspace's text (`WorkspaceLayout=Debug|...`) and the one shown
(`Workspace=`), which ImGui's own [Docking] lines restore at start. Tabs are
put in the arrangement's order for the first frames (the windows already
shown join a new node first, and ImGui's DockOrder follows the tabs as they
are). Session and Control became the toolbar above the dock space (the
dock space's host window draws it). Code shows the stage's def in Stage
and the fight screen's in Screenpack (`CodeShow`, once the game has told
them), and follows the state in the others. A window opened where the
workspace does not have it goes next to its first open neighbour (a list
per window), else with Console, Code or Game.
The Stage window's Checks (`stagecheck.cpp`, `tests/stagecheck_test.cpp`):
the def as `loadStage`, `readBackGround` and `bgCtrl.read` read it. A BG's
type is its first letter (n, a, p, v, d) and one without a type is skipped;
a normal BG with a spriteno uses it, else its actionno (which must exist,
or the stage does not load); BGCtrlDef and BGCtrl `ctrlid`s must name a BG's
id; start positions are checked at localcoord width over zoomout, with
screenleft and screenright (15 at 320 by default). Keys are known per
section from `stage.go` (`portraitscale` is read by the select screen;
`versiondate` is MUGEN's); [Music] and [Shaders] are not looked at. Files
are found next to the def, in the game's folder, data/, stages/, sound/
and video/. The release's stages give no error (the test runs them):
kfm.def's `sound/kfm.mid` is not in this copy, as the engine's log says too.
Not done: elements never on screen, and the
screen showing nothing at the camera's limits (they need the BGs placed
for each camera position).

