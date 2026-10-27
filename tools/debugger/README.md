# Ikemen GO debugger

A debugger for Ikemen GO characters. It is a separate program
(`ikemen-debugger.exe`, Dear ImGui, Windows) that starts the game and talks to
a Lua mod (`mod/ikdbg.lua`) through the game's stdin and stdout.

It works at two levels:

- **Any Ikemen GO** (tested with the v1.0.0 Windows release): players and
  helpers, pause and tick stepping, breakpoints on Lua conditions, frame
  history, trace of state changes and hits, a Lua console that also runs CNS and ZSS controllers.
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

1. Run `tools/debugger/build/ikemen-debugger.exe`. On the toolbar, set the
   characters, the stage, and which side the AI plays; its **Settings** has
   the path of `Ikemen_GO.exe` (by default the one of this checkout).
2. Press **Launch**. The debugger installs `ikdbg.lua` into the game's
   `external/mods` folder and starts the game with `-ikdbg 1`. Neither the mod
   nor the engine changes do anything without that flag, so normal play is
   not affected. `ikemen-debugger.exe --launch` starts the game at once with
   the saved settings.

Above the windows, the **toolbar**: Launch/Restart and Stop (F5 starts the
game, Shift+F5 stops it, Ctrl+Shift+F5 starts it again); the match the game
starts with (P1, P2, AI per side, stage), and **Matches** (matches saved by
name, picked again in a click); pause/continue (F5), a step
(F10), ten (F11), the speed, and **More** (run N ticks, save/load state,
reset round, reload the match from disk, Ikemen's collision boxes and debug
display); a breakpoint hit; **Settings** (the game's path, extra arguments,
its own window shown, the connection).

Below the windows, the **status bar**: what the game is doing (not running,
running, paused, stepping; tick and round) and the player selected, its
colour green while the game runs, yellow paused, red stopped at a breakpoint;
at the right, the problems (errors and warnings; a click shows Problems), the
file shown in Code with the cursor's line and column, the files not saved (a
click saves and applies), the workspace.

The editor works without the game too: **Explorer** lists the game's
characters (`chars/`), stages (`stages/`), the screenpack (the motif
`config.ini` names, its fight screen, select.def, storyboards and fonts) and
the common files (`config.ini` and its `[Common]` files), each with the files
its def names, found as the engine finds them (a missing one in red). The
characters in the match are marked P1, P2, the stage playing too. A click
opens a file in its window (text in Code, an SFF in Sprites, an SND in
Sounds). A click on a character (or one of its files) makes it the one
**worked on**: Analysis, Sounds, Commands (its CMD file) and Sprites show it,
read from disk when it is not in the match; a player picked in the game
(Game, Players, Trace) takes them back; with the game not running they show
the toolbar's P1 until another is picked. Right click on a character puts it in P1's or P2's place (the match
starts again) or makes it the toolbar's, on a stage makes it the toolbar's;
Show in folder, Copy path. Filter: words of the path, comma separated,
`-word` to hide.

**Go to** (the palette at the top, also in the File menu): Ctrl+P a file of
the projects by letters in order (`kfmcmd`); Ctrl+Shift+P a command (the
game, the workspaces, the windows, the themes, File); Ctrl+G a line of the
file in Code; Ctrl+Shift+O a section of it (`[Statedef 200]`,
`[Begin Action 0]`); Ctrl+Shift+F text in files (Search). **File**: Open...
(any file), Save and apply, Close the tab (Ctrl+W).

In Code: **Ctrl+Wheel** over the text makes it bigger or smaller (every
editor; kept with the settings, shown on the status bar when not 100%),
**Ctrl+0** sets it back. **Ctrl+F** finds in the file (every match on a background; Enter or
F3 the next, Shift the one before; case, whole words), **Ctrl+H** replaces
(one, or all as one step of undo). **F12** or **Ctrl+click** goes to what a
number or a name stands for, read from the character's files as the engine
compiles them: a state named by ChangeState, SelfState, HitDef p1stateno and
the like, or by a trigger (`stateno = 200`) goes to its statedef; an action
(ChangeAnim, a statedef's `anim`, `anim = 200`, `SelfAnimExist(200)`) to its
`[Begin Action]`; `command = "x"` to the `[Command]`; a ZSS function to its
`[Function]`. On a statedef, or with **Shift+F12** on a state's number, the
places changing to that state are listed in Search.

The windows are arranged by **workspace**, one per task, picked by the tabs
on the menu bar (or Ctrl+1 to Ctrl+5); each keeps what is changed in it
(sizes, tabs moved, windows opened, closed or floating), and
**Workspace > Restore** puts it back as it was at first. The Game window is
in all of them.

- **Debug**: Players (Explorer in a tab) above Watch and Breakpoints on the
  left; Game and the Inspector; below, Trace, Frame data, Lab, Console and
  Performance, then Code.
- **Code**: Explorer on the left; Code in the middle, Analysis, Console and
  Trace below it; Game and the Inspector on the right.
- **Character**: Explorer above Players on the left; Animation in the
  middle, Code and Console below it; Game above Sprites, Sounds, Commands and
  Create on the right.
- **Stage**: Explorer on the left; Game, with Code (the stage's def) and
  Console, Performance below it; Stage on the right.
- **Screenpack**: Explorer above Roster and Fonts on the left; Game, with Code
  (the fight screen's def) and Console below it; Fight screen, Motif and
  Storyboard on the right.

The menu bar's **Windows** opens and closes each window (one opened where
the workspace does not have it goes next to its neighbours: Performance with
Console, Create with Sprites...). In Stage and Screenpack, Code shows that
def; in the others it follows the selected player's state.

A line asked from another window (Show in Code, a Lint row, a Trace HitDef)
brings Code to the front, opening it if it is closed; with the game running,
Code then stops following the state, which would leave that line at once.

**Theme** colours the debugger: Dear ImGui's dark (the default) and light, and
the twelve themes TheAncientOwl posted in
[ocornut/imgui#707](https://github.com/ocornut/imgui/issues/707#issuecomment-4107169777)
(Dark, Forest green, Amethyst, Sapphire, Amber yellow, Dracula, Catppuccin
Mocha, Gruvbox hard, Crimson Vesuvius, Rose quartz, Cyberpunk, Paper and ink).
With a light one (Dear ImGui light, Paper and ink) the code editor is light too,
and the coloured texts are darker. The theme is kept with the settings.

| Window | What it does |
|---|---|
| Game (engine side) | The game itself: its frames, with the collision boxes and axes over them; hover a player or helper to see it, click to select it, drag it to move it. The mouse wheel zooms (the game's pixels, kept sharp), the right button pans, "Follow it" keeps the selected one in view, "Keys" shows under each player the keys it holds (its own, the AI's, a dummy's or a take's), "History" P1's last keys down the left side and P2's down the right, each with the ticks it lasted. While the window has focus, the keyboard goes to the game (but F5, F10, F11). The game's own window is hidden unless the toolbar's Settings say otherwise |
| Players | Every player and helper: state, previous state, animation, element, time, types, ctrl, position, velocity, life, power, how many explods and projectiles it has |
| Animation | A character picked in Explorer that is not in the match: its AIR file from disk, edited the same way (no game to show it on); its Explorer menu has Open in Animation. For every action, a **Preview** of the element (its sprite at its offset from the axis, its Clsn2 and Clsn1 boxes), or the action played at 60 ticks a second, looping from its loop start; arrows go to the action before or after. Engine side: the selected entity's animation element in its AIR file: the elements (a click shows one on the game), the element's sprite, offset and time, its Clsn1 and Clsn2 boxes (own, or the action's default and how many elements share it), add and delete boxes. The action can be shown on the player, copied, renumbered or deleted; an element duplicated, deleted, moved or made the loop start. With **On the game, drag its boxes**, the boxes are drawn on the Game window with handles: drag an edge, a corner or the inside; with **the sprite**, a drag near the player moves the element's offset. The previous element's boxes can be shown faint. Every change goes to the game at once (only the animations are compiled again); **Save** writes the AIR file, which the Code window edits too. A screen's element picked on the Game window (fight screen, motif, storyboard) that shows an action of its def (`[Begin Action]`) takes the window instead: the same editing of its elements (no boxes; the sprite picker shows the screen's SFF), the def loaded again at each change, the animations going on from where they were |
| Sprites | The SFF file (v1 or v2) of the character worked on (the selected player's, or one picked in Explorer), or of the screen whose element was picked last on the Game window (`fight.sff`, the motif's `system.sff`, the storyboard's; it opens at the sprite that element shows): the sprites, filtered by group; a preview (wheel: zoom at the mouse, drag or middle drag: pan, Ctrl+drag: move the axis, double click: fit) with any of the palettes; renumber, axis, replace with a PNG, save as a PNG, delete; add PNG files (to the character's palette 1,1, or with palettes of their own). **Save** writes SFF v2 (the first time the old file is kept as `.bak`) and the game reads it again at once (a screen: its def is loaded again with the new SFF, the round or the menu going on). The Animation window picks an element's sprite among thumbnails and can show the previous element's sprite faint on the game |
| Sounds | The SND file of the character worked on (the selected player's, or one picked in Explorer): every sound with its length and format, played here (Play, Stop); renumber, replace with a WAV file, save as a WAV file, delete, add WAV files. **Save** writes the file (the first time the old one is kept as `.bak`) and the game reads it again at once |
| Commands (engine side) | The selected player's input as the game reads it: the keys held now (hover one: for how many frames) and before, with how many frames each lasted. The [Command] sections of its CMD file (of a character picked in Explorer too, from disk, without the input): the steps done of each command under way, green while `command = "name"` is true (fading for a second after), how many times each was completed. The selected command's name, keys, time, buffer.time and steptime are edited here (empty: the default); duplicate, delete, add, show in Code. **Motion**: its steps as a row of chips (arrows and buttons, the prefixes above); the step picked edited on a direction pad, buttons pressed together (a b c x y z s d w m), held (`/`), released (`~`, with a charge time), `$` (the diagonals count), `>` (nothing else in between); moved, deleted, one added after it; or start from a motion (quarter and half circles, dragon punch, dashes, charges, hold down). A step written otherwise (`a|b`, `D+~a`, `/30F`, N, L, R) is shown as text and edited in the command's text. **Start a state with this command**: a ChangeState to a state, standing, crouching, in the air or any, with control or not, put at the top of the `[Statedef -1]` the engine reads first (CNS or ZSS, as that file is written; a new one in the CMD file without any), shown in Code. **Apply** (Ctrl+S) saves and the game compiles the characters again, as in Code |
| Stage (engine side) | The stage's def file: its sections, and the fields of the one selected, edited here (change, remove, add). Each change is shown at once: the game loads the stage again from the text, without restarting the match (Reset round keeps it) nor the stage (its time, its BGs' movement and animations, a 3D model's animations go on; it starts over when BGs are added or removed), its SFF and 3D model not read again (a 3D stage's 12 MB model: 0.5 s per change before); **Save** (Ctrl+S) writes the file, **Revert** goes back to it. With **Stage** checked on the Game window (or "Pick on the Game window" here), the mouse picks the stage's BGs instead of the players: hover shows which, a click selects its section, a drag moves it (its `start`, whole units; BGs positionlinked to it follow). **Limits** draws on the game how far the camera goes (boundleft/right/high/low), the players' bounds, the floor and the starting positions (P1-P4, the camera's); in pick mode their handles drag those fields. **Free camera**: the wheel zooms and the right button pans what is drawn (the game's camera and the players' bounds stay as they are); **Fit the stage** shows all of it. The stage's time is shown, **Restart the stage** sets it back to 0; BGCtrls that ran on the stage's last step are green in the list; a BGCtrl (or a BGCtrlDef, with all of its BGCtrls) shows when it runs on a time bar, and its BGs are outlined on the game. **3D model** (a stage with `[BGdef] model`): its animations (on from their start, or off with what they move back at rest; where each one is in its duration, its loops), its nodes as a tree with their meshes and lights (shown or hidden until the round starts again; Shift: with their children), its lights (`KHR_lights_punctual`: colour, intensity, range, changed at once; a light's own animation still wins); **Checks** of the def, again at each change of its text or sprites (a line clicked shows it in Code): errors (the SFF, a model, a video, an attached character not found; a sprite or an action a BG or a BGCtrl names that does not exist; boundleft right of boundright), warnings (a music file not found; BGs and BGCtrls the engine skips, without a type or of an unknown one; BGCtrls on ids no BG has; P1 and P2 starting further apart than the screen holds, or past where the camera can show; sections the engine does not read, or read once), notes (keys the engine does not read; sprites nothing uses) |
| Fight screen (engine side) | The fight screen's def (`fight.def`: lifebars, power, face, name, time, combo, round...): its sections, the elements of the one selected that are on screen, and the fields of the element selected (keys without its prefix; or all of the section's), edited here; the element's place (its group's `pos`, its own `offset`) can be dragged. Each change is shown at once: the game loads the fight screen again from the text (in about 20 ms: the SFF, SND and fonts in use are kept; **Reload** reads them again), the match and the round going on; **Save** (Ctrl+S) writes the file, **Revert** goes back to it. With **Screenpack** checked on the Game window (or "Pick on the Game window" here), the mouse picks the fight screen's and the motif's elements instead of the players: hover shows which (the smallest under the mouse: a bar's fill before its frame), a click selects it, a drag moves it with its group (`p1.pos`, `team1.pos`, `pos`...), Alt+drag moves it alone (its `offset`) |
| Motif (engine side) | The motif's def (`system.def`: title, select and versus screens, menus, options...), edited as the fight screen's, in menus too: the elements on screen (texts, animations, the screens' BGs: `[TitleBG ...]` and the like, dragged by their `start`; the select screen's cells, names, portraits (made from the characters' sprites, they take their property's source through the mod)) are picked on the Game window, a click selects one (the window's tab comes to the front, the keys stay with the game), a drag moves it with its group (`menu.pos`, `p1.pos`...; without one, its `offset`) or alone with Alt. Reloading the motif takes about 400 ms (Ikemen's `loadMotif`), so a drag shows the outlines moving and writes when the button is let go; animations and BGs go on from where they were. The scripts keep the objects and tables they hold, with the new values in them; what a screen reads once when it starts (as a menu's clipping window) shows when it starts again |
| Storyboard (engine side) | The storyboard playing (an intro, an ending, the logo...): its def edited as the motif's, its layers and its scenes' BGs picked on the Game window; a layer's group is its scene's `layerall.pos` (Alt: its `offset`). The game loads it again at each change (a few ms), at the same scene and time, typed text kept; it works paused too |
| Roster (engine side) | The motif's `select.def`: its characters in the select screen's grid as the motif lays it out (`[Select Info]` rows, columns, cells skipped), each with its portrait (sprite 9000,0); the cells past the grid and the excluded ones (`exclude = 1`) after it. Drag a cell to another to move it; right click: insert a character (from `chars/`, or `randomselect`, `emptyslot`, `skipslot`) or delete; the cell selected: its line as text, its stages, `order`, `hidden`, `exclude`, `music`, `includestage`, `ai`. Tab Stages: `[ExtraStages]`, moved, added from `stages/`, deleted. Each change has the game add the roster again (~0.1 s) and the select screen shows it at once; it is refused in a game mode under way (from its select screen to its end). A cell clicked on the Game window (Screenpack on) is selected here; one clicked here is outlined on the game, and P1's cursor goes to it. Save writes the file |
| Fonts (engine side) | The font files the game loaded; the font of a text clicked on the Game window (Screenpack on) is selected. A `.def` font: its `[Def]` (`size`, `spacing`, `offset` dragged; `type`, `banktype`, `colors`, `file`), each change read again by the game into the fonts in use, the texts changing at once; Save writes the def. A bitmap font: a sample text drawn as the engine draws it, and its glyphs (its SFF's group 0, the number a character's code); a glyph clicked opens in the Sprites window, whose Save has the game read the font again. A `.fnt` (MUGEN 1.0: a PCX image and a text in one file): its `[Def]` and its whole text (`[Map]`: a character, its x in the image, its width) edited live, the sample and the glyph sheet shown; Save rewrites the text part (the old file kept as `.bak`). TrueType texts are picked on the Game window too |
| Inspector | The selected entity in full, `var`/`fvar` (changes of the last tick in yellow), and actions: SelfState, ChangeAnim, set life, full life/power, stand, toggle AI. Engine side: double-click a `var`/`fvar` to change it, set position and velocity; its explods (ID, action and element, time, remove time, position, bind time, layer) and projectiles (the same, velocity, hits left) |
| Frame data | Ticks: tick by tick history of the selected entity (1200 ticks); state changes and hits are highlighted, an element with a Clsn1 box is red. Engine side: the number of controllers that ran; click a tick to see them in Code. Moves: its attacks in that history (from entering a movetype A state until control is back; a state change after its Clsn1 frames begins another): startup (the first frame with a Clsn1 box), active (its runs, `2+3`), recovery, total, frames counted out of hit pause; whiff, hit or guarded, the damage, and the advantage (the tick the one hit has control back less the attacker's; none when it was hit again first) |
| Trace | State changes and hits of every entity: an attack that hit or was guarded, and on the one hit, by whom (its state after the tick), the damage (taken on the next tick), hit time (or guard time), shake (its hit pause), slide, ctrl time and whether it falls, the combo's hits and damage so far, the juggle points its attacker has left on it when it falls; hover for its attr and anim type. Engine side: the attack's controller (the HitDef, Projectile or ReversalDef the attacker ran last), a click shows it in Code |
| Lab (engine side) | **Dummy:** a player whose keys the game holds (its AI turned off): it stands, crouches or jumps; guards never, always (it holds back while an attack is near enough to guard), randomly (a chance per attack), or after a first hit (the first hit goes in, then whatever the dummy can guard, until 90 ticks go by without a hit: the gaps of a combo); its life and power kept full once each combo is over; once it can act again after guarding, a hit, getting up or any of them, it plays a take, each time or with a chance (a reversal: the take starts so that its last tick is the first one the dummy can act on, from the stun or the get up animation left). **Takes:** a player's input recorded tick by tick (back and forward as it faces, so a take works on either side), from the start of the fight after a round reset or from now; played back as P1 or P2, from a reset, with the same random numbers, in a loop; each shown as its runs of keys (`DF+a 3`), the one playing highlighted, with the character it was recorded with (only those of the characters in the match, when asked); saved with the settings. **Characters:** another character from `chars/` in P1's or P2's place, the match starting again |
| Watch | Lua expressions over Ikemen's triggers (`life(), var(3)`, or statements that return values), read by the game with each tick, on the selected entity or on a player; a value that changed on the tick shown is yellow, hover for when it last changed. Saved with the settings |
| Explorer | The characters, stages, screenpack and common files as trees of the files their defs name, read from disk (see above) |
| Outline | The sections of the file shown in Code, with the comment just above each: a state file's statedefs and ZSS functions, an AIR file's actions, a def's sections; the one the cursor is in selected; a click goes there; a filter |
| Problems | Everything found wrong, in one list: the Lint of the character worked on, the checks of the stage (the match's, or the toolbar's), the last Apply that failed, the warnings and errors the engine logged since it started (`WARNING: ... in kfm.cns, line 1476`, the same text once with how many times); errors, warnings, notes shown or not; a row clicked shows its line in Code |
| Search | Text found (Ctrl+Shift+F; case, whole words) in the files of the character worked on, of every project, or of the files open, by file; the places changing to a state (Shift+F12 in Code). A line clicked is shown in Code, the match selected |
| Code | An editor of the game's text files, one tab each (syntax colours, completion and documentation of triggers and controllers, undo). The file shown goes in the preview tab (dimmed), which the next one takes, unless it is kept: double-clicked, edited, or opened to be kept (a double click in Explorer, Go to file); a tab with changes asks before closing (Save, Don't save, Cancel); the tabs kept are reopened with the debugger. **Save and apply** (Ctrl+S) saves the changed files and the game loads and compiles the characters again without restarting the match (engine side); when that fails, the game keeps the previous version and the line of the error turns red. Engine side, also: the controllers that ran on the tick, in the order they ran (all states: -4, -3, -2, -1, the current state, +1, and any state changed into during the tick); in the file, lines that ran are green, a dot marks each controller, clicking the dot sets a breakpoint. After each trigger line (CNS `trigger`/`triggerall`, ZSS `if`) its value on the tick shown: true, false, undefined, or nothing when it was not evaluated (a trigger after one that held, a state that did not run); after a controller that did not run, how many ticks ago it last did. The dot of a statedef sets a breakpoint on entering that state. Follows the selected entity's state, but not while you type |
| Breakpoints | A Lua condition over Ikemen's triggers, for anyone, one player, or one player and its helpers; the game pauses on the tick the condition becomes true (what is already true when it is set waits until it turns false and true again). Entering a state is the condition `stateNo() == n`, set from the Code window. Engine side: breakpoints on state controllers, the game pauses after a tick in which one ran (with **Only if**, a Lua condition, only when it is true then for the one that ran it). Both are saved with the settings |
| Analysis | The character worked on (the selected player's, or one picked in Explorer, read from disk) read from its files as the engine compiles them (its `st` files, `cmd`, `stcommon`, then config.ini's common states: the first statedef of a number is the one used), CNS and ZSS. **Lint**: errors (states, actions, sounds and commands named that do not exist; a controller without a type, or without trigger1), warnings (a statedef given twice, the AIR's sprites missing from the SFF), notes (states 200 to 4999 nothing changes to by number, commands no trigger names); only numbers written as numbers are checked, and an action guarded by `SelfAnimExist` of it, or shown in a state the character only gives to others, is not. It runs again when a file changes (on disk or in Code); a click shows the line in Code. **Variables**: each var, fvar, sysvar, sysfvar with where it is written (VarSet, VarAdd, VarRandom, VarRangeSet, ParentVarSet/Add, `:=`) and read, by state (a click shows the line), the selected entity's value now, and a name kept per character (shown in the Inspector). **States**: the graph around a state (it follows the player, or a state clicked): the states that change to it on the left, those it changes to on the right, 1 to 4 steps, by numbers written in ChangeState, SelfState, p1stateno (dashed: p2stateno, TargetState, Helper); the path the player came by green, states that do not exist red; double-click shows a statedef in Code. Changes the engine makes by itself (landing, getting hit) are not in it |
| Create (engine side; the menu bar's Create) | **A move**: an attack for the selected player's character, written as KFM writes its own (CNS or ZSS, as the character's first state file is): name, state (one taken, or a common state's number, is pointed out), standing, crouching or in the air, normal, special or hyper (1000 power), light, medium or hard (the HitDef's values after KFM's punches, each editable: damage, pausetime, spark, slide and hit times, velocities, animtype); the action (its first element with a Clsn1 box arms the HitDef, the spark at the middle of that box; without the action, another one is copied as it); a sound at the start (played here); the command that starts it (a ChangeState at the top of `[Statedef -1]`, with control or not, a hyper's with `power >= 1000`). **Create the move** puts the statedef at the end of the first state file, saves and applies; then **Try state N** puts the player in it, and Code shows it; the text it writes is shown before. **A character** from another one (`chars/<folder>/<folder>.def`, its name and author): **its basics** (the sprites and sounds renamed after the folder, the other files of `[Files]` as they are, the constants, the statedefs below 200 it gives itself (intro, win poses, taunt) with every ZSS `[Function]`, in a `.cns` or a `.zss` as they were written, the actions of the common states, a new CMD file with the commands the engine needs and the taunt; a character that stands, walks, jumps, guards and gets hit like its base, with no attack yet) or **a full copy**, moves included (the folder copied, the def renamed); added to `select.def` or not; put in P1's or P2's place, or nowhere. **A stage** of PNG images (`stages/<file>.def` and `.sff`, name, author, localcoord: one pixel of the images is one unit), from the back: each one a sprite of the SFF (its own palette, or true colour past 255 colours) and a BG centred with its bottom on the screen's bottom, its delta set here (1 like the floor, less far away); the camera goes as far as every image still covers the screen; a preview shows the images as the camera sees them at its start, the camera's range, the screen and the floor (`zoffset`, dragged) with the players' places; music; added to `select.def`'s `[ExtraStages]`; put in the match's place at once (the match goes on) and made the toolbar's stage |
| Console | **Lua**: any Lua, run in the game (`player(2); return stateNo(), var(3)`). **Controllers**: CNS or ZSS state controllers (ZSS when the character's first state file is), compiled with the character's compiler and run once on the selected player with Ctrl+Enter, as a negative state runs (a ChangeState is taken at once); a `[State]` header and `trigger1 = 1` are added when missing, several `[State]` blocks run in order; examples (ChangeState, VelSet, PosSet, VarSet, LifeAdd, PowerAdd, PalFX, Explod, EnvShake, HitDef); an error shows the line of the text. Ikemen's own log is shown too |
| Performance | The game's frames during a match, measured by the engine while the window is shown: each phase (the game; the motif and Lua, the debugger's mod among it; sounds and input; the draw; showing the frame, with the debugger's capture of it; the wait) for the last 600 frames as stacked columns under the frame's time at the game's speed, frames not drawn marked, a frame's phases on hover; average, peak (red past the frame's time) and share of each; Freeze, Clear. Below, the debugger's own frame |

Settings, breakpoints, the theme, Code's tabs and each workspace's arrangement are saved in
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
`loadFnt` lend it the files in use); `editor_motif.go` does the same for the
motif (`Anim` and `TextSprite` keep their source; `Anim.Draw` and
`TextSprite.Draw` tell where they went).

New Lua functions: `debugTraceEnable(on)`, `debugTraceRead()` (character id,
source index pairs since the last call), `debugSourceCount()`,
`debugSources(from, count)`, `stateSource([stateNo])`, `setVar`, `setFvar`, `debugSwapStage(def)` (another stage in place of the one in use, the match going on), `debugModel()`, `debugModelAnim(i, on)`, `debugModelNode(i, visible[, children])`, `debugModelLight(i, r, g, b, intensity[, range])` (the stage's 3D model), `debugRunController(text[, zss])` (controllers run once on the player of `playerId`), `debugPerf()` (the frames' timings since the last call),
`setSysVar`, `setSysFvar`, `setPos(x, y[, z])`, `setVel(x, y[, z])`, `debugMoveBy(dx, dy)` (pixels of the game picture), `debugReloadChar(pn)` (`editor_reload.go`: load and compile a player's files again in the middle of a match), `debugReloadAnims(pn[, textFile])` (the animations alone, optionally from unsaved text), `debugCharFiles(pn)`, `debugSetAnimElem(elem[, time])`, `debugReloadSprites(pn)` and `debugReloadSounds(pn)` (the SFF or SND read again, for every player using it), `debugTriggerWatch(id)` (record that character's trigger values; they go out with the frames), `debugStageBgMove(index, dx, dy)`, `debugReloadStage([textFile])`, `debugStageReset()`, `debugReloadFight([textFile[, keep[, sprites]]])`, `debugReloadMotif([textFile[, keep[, sprites]]])`, `debugReloadStoryboard([textFile[, keep[, sprites]]])` (keep: the SFF, SND and fonts in use are borrowed; sprites: the SFF is read again all the same), `debugAnimSource(anim, from)`, `debugReloadFont(file[, textFile])` (`editor_font.go`: every font loaded is recorded; the file, `.def` or `.fnt`, read again into each font loaded from it), `debugAnimTag(anim, tag)` (the copies of a motif animation drawn next say where: a select screen's cell), `debugSelectReset()` (`editor_select.go`: empties the characters and stages added, for the mod's `ikdbg.reloadRoster([textFile])`, which runs `main.f_loadSelectDef` and `start.f_makeGrid` again), `debugCamera(on[, x, y, scale])` (the editor's free camera: one line in `system.go` passes the camera each frame is drawn with through `editor_stage.go`), `debugObjects()` (`editor_objects.go`: a character's explods and projectiles), and the Lab's `debugInputHold(pn[, keys])`, `debugInputRecord(pn, on)`, `debugInputPlay(pn[, take, loop, seed])`, `debugInputState(pn)` and `debugSwapChar(pn, def[, pal])` (`editor_lab.go`: one line in `input.go` lets the editor record a player's buttons or replace them).

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
