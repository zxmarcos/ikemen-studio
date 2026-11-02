# Ikemen GO debugger

To get it with the engine and a screenpack, ready to run: **Actions > Build > Run workflow** on
GitHub, then the package among that run's artifacts, or a release, made by a tag `v*`.

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
(`scripts/msys_setup.ps1` installs everything into a fresh MSYS2), and the
environment variable `MSYS2_ROOT` set to MSYS2's folder:

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

1. Run `tools/debugger/build/ikemen-debugger.exe`. The **start page** asks what
   you want to do (test a character, edit its animations and sprites, a stage,
   the screenpack; or start a new character or stage) and opens that workspace;
   **File > Start page** shows it again, and it can be turned off. The first
   time, **Getting started** follows in a corner of the window: three steps,
   each going on when you have done it (also in File).
2. Set the match with the button on the toolbar that shows it ("kfm vs kfm on
   stage0"): characters, stage, which side the AI plays. The gear has the path
   of `Ikemen_GO.exe` (by default the one of this checkout).
3. Press **Launch**. The debugger installs `ikdbg.lua` into the game's
   `external/mods` folder and starts the game with `-ikdbg 1`. Neither the mod
   nor the engine changes do anything without that flag, so normal play is
   not affected. `ikemen-debugger.exe --launch` starts the game at once with
   the saved settings.

Above the windows, the **toolbar**: Launch/Restart and Stop (F5 starts the
game, Shift+F5 stops it, Ctrl+Shift+F5 starts it again); the match the game
starts with, a button that opens its players, AI per side, stage and **Saved
matches** (picked again in a click); pause/continue (F5) and a step (F10); **...**
(the speed, ten ticks, also F11, N ticks, save/load state, reset round, reload
the match from disk, Ikemen's collision boxes and debug display); a breakpoint
hit; the **gear** (the game's path, extra arguments, its own window shown, the
connection).

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
**worked on**: Analysis, Animation, Sounds, Commands (its CMD file), Sprites and
File > New > A move show it,
read from disk when it is not in the match; a player picked in the game
(Game, Players, Trace) takes them back; with the game not running they show
the toolbar's P1 until another is picked. Right click on a character puts it in P1's or P2's place (the match
starts again) or makes it the toolbar's, on a stage makes it the toolbar's;
Show in folder, Copy path. Filter: words of the path, comma separated,
`-word` to hide.

**Go to** (the palette at the top, also in the File menu): Ctrl+P a file of
the projects by letters in order (`kfmcmd`); Ctrl+Shift+P a command (the
game, the workspaces, the windows, the themes, File, New); Ctrl+G a line of the
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

The windows are arranged by **workspace**, one per task, picked on the bar at
the left (or Ctrl+1 to Ctrl+5); each keeps what is changed in it (sizes, tabs
moved, windows opened, closed or floating), and **View > Restore** puts it back
as it was at first. The Game window is in all of them.

Most windows are tabs of a **panel**, so that a workspace shows at most four
areas, each with one job:

| Panel | Its tabs |
|---|---|
| Project | Explorer, Outline |
| Output | Problems, Search, Console |
| Fighters | Players, Watch, Breakpoints |
| Testing | Lab, Frame data, Trace, Analysis, Performance |
| Assets | Sprites, Sounds, Commands |
| Stage editor | Stage, Model |
| Screenpack | Fight screen, Motif, Storyboard, Roster, Fonts |

Game, Code, Inspector, Animation and Scenes stand alone. Each panel remembers
its tab; without room for every name, the tabs show their icons.

- **Test**: Fighters and Project on the left; Game, with Testing, Output and
  Code below it; the Inspector on the right.
- **Code**: Project on the left; Code in the middle, Output below it; Game on
  the right.
- **Character**: Project and Fighters on the left; Animation in the middle
  (Code and Output in tabs); Game above Assets on the right.
- **Stage**: Project on the left; Game, with Animation, Code (the stage's def)
  and Output below it; Stage editor above Assets on the right.
- **Screenpack**: Project on the left; Game, with Animation, Scenes, Code (the
  fight screen's def) and Output below it; Screenpack above Assets on the right.

Animation and Sprites work on stages and screenpacks as on characters: the Stage,
Fight screen, Motif and Storyboard windows list every `[Begin Action]` of their def,
on screen or not; a background picked on the game shows its action and sprite; a
right click in Explorer opens any def's animations or an AIR file such as
`fightfx.air`, with the game running or not. In those windows a field naming a
sprite or an action shows it beside its value (a click picks another), one naming a
sound plays it, and Ctrl+Z undoes. Sounds opens any SND file (`fight.snd`,
`system.snd`, `common.snd`) from Explorer. Stage, Fight screen and Motif work with
the game stopped too, on the stage picked in Explorer and the screenpack of
`config.ini`, and their Checks say which files, actions, sprites and sounds are
missing.

**Go to** on the Game window puts the game on a screen and keeps it there: the
title, options, select, versus, hiscore, storyboards, victory, results, continue, or
a moment of a match (round call, Fight!, KO, Double KO, Perfect, Time over, Winner,
Draw, the pause menu), with the teams chosen for it (Single, Simul, Turns, Tag), so
the fight screen's team sections can be seen too. **Let it go on** goes on from there.
The first time, the game starts again in its menus. **Show > Screen shapes** draws what
16:9 and 4:3 screens show of the picture.

A right click on a def in Explorer gives **Package as zip** (the def and every file it
names, saying what is missing), and on the motif **Copy as a new screenpack**
(`data/<name>/`, which the next launch uses). A match that cannot load shows why on
the Game window, with Retry for after the file is fixed.

**Storyboards** work with or without the game. Pick one in the Storyboard window (the
motif's and the characters'), or **Open in Storyboard** in Explorer's menu for any
other. **Scenes** shows a preview drawn from the def, its scenes in a strip as long as
they last, and the timeline of the one selected: drag a layer or a sound, a layer's
edges for its `starttime` and `endtime`, the fades' edges and the red line for
`end.time`. Click or drag on the ruler to move through the scene, and on the preview
to pick a layer and move it (Alt: that layer alone). The strip's right click adds,
duplicates, moves and deletes scenes, and **+ Animation**, **+ Text** and **+ Sound**
add rows. The preview is close, not exact: texts use this program's font, and BG videos
and shaders are not drawn. Undo and Save are on the row of those buttons too, and Ctrl+Z
and Ctrl+S work there. **Play in the game** has the game show the same scene and
time, stopped, with your unsaved text; from then on the ruler moves the game and every
edit shows there. A scene reached with the ruler plays neither its sounds nor its music.

In the Stage window, a BGCtrl or BGCtrlDef shows each BGCtrl's time on a bar, with the
game or without it: drag the bar, its edges (`time`'s start and end) or the light line
(its looptime). A click on a BGCtrlDef's bar opens that BGCtrl.

**Add layer**, above its list, makes a new background behind the fighters or in front
of them, after the selected one. It shows a sprite, or an image that becomes one, or an
animation: from several images, one frame each in the order of their names, from the
sprites of a group, or one the stage has. An animation made here is a new action of the
def, which Animation then opens; images go in the stage's SFF, saved at once. The buttons beside it duplicate,
move and delete the selected one: lower in the list is drawn later, so in front. A
background moves with those that have `positionlink` after it, and everything that
moves keeps going from where it was. All of it works with the game or without it, and
Undo takes it back; the def is written with Save.

The Motif and Storyboard windows have the same buttons, for the group of backgrounds of
the section selected: pick a `[TitleBG ...]` or `[TitleBGdef]`, or a scene with `bg.name`.
In Scenes, **+ Animation** makes the animation of a new layer the same way, and every
field naming an action, in any of these windows, has **A new animation** in the menu of
its thumbnail.

**View > Windows** opens and closes each one (one opened where the workspace
does not have it goes next to its neighbours). In Stage and Screenpack, Code
shows that def; in the others it follows the selected player's state.

**View > Advanced tools** shows what looks inside a character rather than at
it: Watch, Breakpoints, Trace, Analysis, Performance, Console and Model. Off
(the default) they stay out of the way, but anything that needs one still shows
it (Ctrl+Shift+P lists them all).

On the Game window, **Select** says what a click on the picture picks:
**Fighters**, the **Stage**'s backgrounds and limits, or the **Screen**'s
elements (and the 3D model's nodes, on a stage that has one). Each workspace
sets it: Stage selects backgrounds, Screenpack elements, the others fighters.
The line above the picture says what is selected and opens its editor, or what
a click would do. **Show** has what is drawn over the game: collision boxes,
axes, keys, input history, only the selected one, following it when zoomed in.

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
| Game (engine side) | The game itself: its frames, with the collision boxes and axes over them; hover a player or helper to see it, click to select it, drag it to move it. The mouse wheel zooms (the game's pixels, kept sharp), the right button pans, the Show menu's "Follow it" keeps the selected one in view, "Keys" shows under each player the keys it holds (its own, the AI's, a dummy's or a take's), "Input history" P1's last keys down the left side and P2's down the right, each with the ticks it lasted. While the window has focus, the keyboard goes to the game (but F5, F10, F11). The game's own window is hidden unless the gear on the toolbar says otherwise |
| Players | Every player and helper: state, previous state, animation, element, time, types, ctrl, position, velocity, life, power, how many explods and projectiles it has |
| Animation | Laid out as an editor: the action (its number, Follow, an Action menu: show on the player, copy, renumber, delete) and Save; a strip of its elements (their sprites and times, the loop start, a dot on the one the game shows; right click: duplicate, delete, move, loop from here); the preview beside the element's panel (sprite, offset, time, its Clsn2 and Clsn1 boxes), one under the other when the window is narrow. A character picked in Explorer that is not in the match: its AIR file from disk, edited the same way (no game to show it on); its Explorer menu has Open in Animation. For every action, a **Preview** of the element (its sprite at its offset from the axis, its Clsn2 and Clsn1 boxes), or the action played at 60 ticks a second, looping from its loop start; arrows go to the action before or after. On the preview, the element is edited directly, the game not needed: with **Drag: boxes**, drag an edge, a corner or the inside of a box, the arrow keys move the box under the mouse (Shift: by 10); with **Drag: sprite**, a drag anywhere moves the element's offset, and so do the arrow keys; right click adds a Clsn2 or Clsn1 box at the mouse or deletes the box under it (Delete too); wheel: zoom, middle drag: pan, double click: fit. The **Onion skin** menu: the element before (its sprite tinted orange) and the one after (green; after the last, the loop start's), their sprites, their boxes, drawn faint on the preview and on the game. Engine side: the selected entity's animation element in its AIR file: the elements (a click shows one on the game), the element's sprite, offset and time, its Clsn1 and Clsn2 boxes (own, or the action's default and how many elements share it), add and delete boxes. The action can be shown on the player, copied, renumbered or deleted; an element duplicated, deleted, moved or made the loop start. With **On the game, drag its boxes**, the boxes are drawn on the Game window with handles: drag an edge, a corner or the inside; with **the sprite**, a drag near the player moves the element's offset (the preview's Drag choice is the same one). Every change goes to the game at once (only the animations are compiled again); **Save** writes the AIR file, which the Code window edits too. A screen's element picked on the Game window (fight screen, motif, storyboard) that shows an action of its def (`[Begin Action]`) takes the window instead: the same editing of its elements (no boxes; the sprite picker shows the screen's SFF), the def loaded again at each change, the animations going on from where they were. **Undo** and **Redo** (Ctrl+Z, Ctrl+Y) take changes back one at a time. Ctrl and Shift select several elements, which are duplicated or deleted together, take the fields of the one shown, or move their offsets and boxes. A bar under the strip shows the action along its ticks. **Draw: Clsn1, Clsn2**: a drag on the preview draws a box, a right click stops. The "..." of a kind of boxes copies and pastes them, takes them from the element before, puts them on every element with that sprite, swaps them or deletes them in the action. **Effects** holds flip, blending, scale and angle, its header saying which the element uses, and **Smooth from the element before**, which sets `Interpolate` lines, played on the preview as on the game. The Action menu also makes a new action, imports actions from another AIR with their sprites, names one (the list searches by number or name), scales offsets and boxes, turns boxes alike into defaults (Optimize) and saves a GIF. Right click on an element: select those after it, keep it as a ghost over the preview |
| Sprites | The SFF file (v1 or v2) of the character worked on (the selected player's, or one picked in Explorer), or of the screen whose element was picked last on the Game window (`fight.sff`, the motif's `system.sff`, the storyboard's; it opens at the sprite that element shows). Three columns: the sprites, filtered by group, as a list (a small picture, the numbers, the size) or a grid of pictures, the arrows moving from one to the next; the preview (wheel: zoom at the mouse, drag or middle drag: pan, Ctrl+drag: move the axis, double click: fit; under the mouse, where it is from the axis and its colour); the **Sprite** and **Palette** tabs. **Palette** above the preview: a character's palettes (its def's `palN` ACT files, else its SFF's 1,N; the player's at first), as on the game in place of the shared 1,1, or any other palette of the SFF; the pictures of the list follow it. **Sprite**: renumber, axis, size, format, its palette, a linked picture; replace with a PNG, save as a PNG, duplicate, delete; the actions of the character showing it (a click shows one in Animation). **Palette**: the colours of the palette the sprite is drawn with; a click on the sprite picks the colour under the mouse (Alt+click from the Sprite tab, Shift adds it), the colours selected flash on the sprite; Shift and Ctrl and a drag select several; the colour picked edited, the colours selected shifted in hue, saturation and brightness, made a gradient, copied and pasted; import or export an ACT file; a copy as the character's next palette (SFF 1,N). With the game running, every change of a character's palette is on the game at once (`debugSetPalette`, every player using that SFF). Add PNG files (to the character's palette 1,1, or with palettes of their own). **Save** writes the ACT files changed and SFF v2 (the first time the old files are kept as `.bak`) and the game reads it again at once (a screen: its def is loaded again with the new SFF, the round or the menu going on). The Animation window picks an element's sprite among thumbnails and can show the sprites of the elements before and after faint, on its preview and on the game. **Undo** and **Redo** (Ctrl+Z, Ctrl+Y) take changes to sprites and palettes back one at a time, a palette's on the game too. Ctrl and Shift select several sprites, Ctrl+A all those shown, a right click a group: **Crop** the empty borders, **Resize** (nearest, bilinear, EPX), move or set their axes (Ctrl+drag and the arrows on the preview too), renumber, start or extend an action, **Export** PNG files with a list of numbers and axes, delete. **Add PNG** also reads PCX, BMP and GIF, takes the numbers from the file names, a custom axis and a crop, can keep an indexed image's indices, and reads an export or another SFF. **View**: lines through the axis, rulers, a ghost of another sprite. **Palette** also reads PAL, ACO, ASE, GPL and images, writes PAL, ACO and GPL, moves colours (Alt+drag), reverses, fills, sorts them by tone, shifts them in RGB and gives them effects; **Sprites and palette** points the sprites selected to a palette, adapts them to it or makes one from them; **All the palettes** lists the file's, to copy, delete, renumber, clean up and merge; **PalFX** tries an effect on the preview and copies it as a controller. **Paint**: pen, eraser, bucket, colour picker, rectangle selection (drag it to move its pixels), flips, clear, copy and paste, more room around the picture; sprites with a palette only |
| Sounds | The SND file of the character worked on (the selected player's, or one picked in Explorer): every sound with its length and format, played here (Play, Stop); renumber, replace with a WAV file, save as a WAV file, delete, add WAV files. **Save** writes the file (the first time the old one is kept as `.bak`) and the game reads it again at once |
| Commands (engine side) | The selected player's input as the game reads it: the keys held now (hover one: for how many frames) and before, with how many frames each lasted. The [Command] sections of its CMD file (of a character picked in Explorer too, from disk, without the input): the steps done of each command under way, green while `command = "name"` is true (fading for a second after), how many times each was completed. The selected command's name, keys, time, buffer.time and steptime are edited here (empty: the default); duplicate, delete, add, show in Code. **Motion**: its steps as a row of chips (arrows and buttons, the prefixes above); the step picked edited on a direction pad, buttons pressed together (a b c x y z s d w m), held (`/`), released (`~`, with a charge time), `$` (the diagonals count), `>` (nothing else in between); moved, deleted, one added after it; or start from a motion (quarter and half circles, dragon punch, dashes, charges, hold down). A step written otherwise (`a|b`, `D+~a`, `/30F`, N, L, R) is shown as text and edited in the command's text. **Start a state with this command**: a ChangeState to a state, standing, crouching, in the air or any, with control or not, put at the top of the `[Statedef -1]` the engine reads first (CNS or ZSS, as that file is written; a new one in the CMD file without any), shown in Code. **Apply** (Ctrl+S) saves and the game compiles the characters again, as in Code |
| Stage (engine side) | The stage's def file: its sections, and the fields of the one selected, edited here (change, remove, add). Each change is shown at once: the game loads the stage again from the text, without restarting the match (Reset round keeps it) nor the stage (its time, its BGs' movement and animations, a 3D model's animations go on; it starts over when BGs are added or removed), its SFF and 3D model not read again (a 3D stage's 12 MB model: 0.5 s per change before); **Save** (Ctrl+S) writes the file, **Revert** goes back to it. With **Stage** checked on the Game window (or "Pick on the Game window" here), the mouse picks the stage's BGs instead of the players: hover shows which, a click selects its section, a drag moves it (its `start`, whole units; BGs positionlinked to it follow). **Limits** draws on the game how far the camera goes (boundleft/right/high/low), the players' bounds, the floor and the starting positions (P1-P4, the camera's); in pick mode their handles drag those fields. **Free camera**: the wheel zooms and the right button pans what is drawn (the game's camera and the players' bounds stay as they are); **Fit the stage** shows all of it. The stage's time is shown, **Restart the stage** sets it back to 0; BGCtrls that ran on the stage's last step are green in the list; a BGCtrl (or a BGCtrlDef, with all of its BGCtrls) shows when it runs on a time bar, and its BGs are outlined on the game. a stage with `[BGdef] model` has its 3D model in the Model window; **Checks** of the def, again at each change of its text or sprites (a line clicked shows it in Code): errors (the SFF, a model, a video, an attached character not found; a sprite or an action a BG or a BGCtrl names that does not exist; boundleft right of boundright), warnings (a music file not found; BGs and BGCtrls the engine skips, without a type or of an unknown one; BGCtrls on ids no BG has; P1 and P2 starting further apart than the screen holds, or past where the camera can show; sections the engine does not read, or read once), notes (keys the engine does not read; sprites nothing uses) |
| Model (engine side) | The stage's 3D model (a stage with `[BGdef] model`, glTF or glb). **Placement**: the def's `[Model] offset` and `scale`, the camera's `fov`, `near` and `far` (and `environmentintensity` with an `environment`), dragged with the game following at once. **Nodes**: each scene's tree (scene 1 is drawn in front of the players; nodes in no scene are not drawn), filtered by name, each node shown or hidden until the round starts again (Shift: with its children); the node selected: its position, rotation (degrees about X, then Y, then Z) and scale at rest (an animation moving it still wins), its mesh (vertices, triangles, materials), Ikemen's extras (blend, layer, shadows, depth test and write, outline) and its light. **Materials**: base colour, emission, metallic, roughness, occlusion, unlit, both faces, alpha (opaque, mask and its cut, blend), its textures, the nodes using it. **Animations**: on (from its start) or off (what it moves back at rest), its loop, where it is, dragged to put it at a time; its name clicked shows its **keys** beside the list: a line for each channel moving a node (position, rotation, scale) with its keys and the animation's time; a click puts the animation there, a key clicked is selected (the animation at its time, the preview paused), dragged moves it in time (Ctrl: by ticks), right-clicked deleted, a double click adds one (with the value the channel has there); the key selected: its time, value, the channel's interpolation (linear, step, cubic spline), the keys before and after it; **Key** adds keys for the node selected where the animation is. With the Animations tab in front and the animation on, the handles of a node it moves change its key at the animation's time (one is added at the first move when there is none) instead of its place at rest. **Lights** (`KHR_lights_punctual`): colour, intensity, range, a spot's cone. The game shows every change at once; **Save** (Ctrl+S) writes the model's changes into its file (a `.bak` of it the first time; its buffers and images as they were, but for the keys changed: over their data, or at the end of a `.glb`'s binary chunk when keys were added; a `.gltf` with its own `.bin` keeps its keys) and the def when the placement changed; **Revert** has the game read the file again. On the Game window the node selected is outlined (its box in the 3D view); with **3D model** checked there (or "Pick on the Game window" here) the mouse shows the node under it (by its triangles) and a click selects it; the node selected has handles: **Move** (arrows along its parent's axes; the node itself or the square in the middle, dragged, moves along the screen, Shift: nearer or farther), **Turn** (circles about its parent's axes, and a white one turning it as the screen turns; Ctrl: by 15 degrees), **Scale** (along its own axes; the square in the middle or the node itself scales the three together); the right button during a drag puts it back; its values at rest change (an animated node keeps its animation's place). **Undo** and **Redo** (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z with this window focused) take back the changes one gesture at a time (a field or a node dragged from the press to the release), the match's model and the one from disk each with their own; the placement is the def's text, not in them. **Without the game** (or for a `.glb`/`.gltf` opened in Explorer that is not the match's) the model is read from its file and drawn by the debugger (marked "from disk"): a preview as **the game's camera** sees it at its start (the def's `[Model]` offset and scale, the fov) or turning **around** it (drag; right or middle drag moves; wheel; double click or **Fit** shows all of it), its animations played (**Pause**), unlit when the stage has neither light nor environment map as the game draws it; a click on it selects the node under the mouse, outlined, and the node selected is dragged as on the Game window (elsewhere a drag turns the camera). The same lists and inspector change the model in memory, and **Save** writes the file (and the def) |
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
| Code | An editor of the game's text files, one tab each (syntax colours, CNS's and ZSS's: `#` comments, `[StateDef n; key: value]`, `controller{key: value}`, `if`/`for`/`switch`, `$locals`; undo). **Completion** of what fits where the cursor is, with its documentation: in a `[State]` block the parameters of its controller (`type =`), in a statedef its parameters, after `type =` or at the start of a ZSS statement the controllers (ZSS: `velSet{}`, the cursor inside with its parameters listed), in an expression the triggers, in `GetHitVar(`, `Const(` and the like the names they take; a parameter taken gets its ` = ` or `: `; it opens after two letters (one for a parameter, none after `(` or `{`), Ctrl+Space shows all that fit. The names are Ikemen GO's own: the controllers with the parameters each one reads, the triggers with the names they take, read from the engine's compiler (`scripts/gen_names`, `src/ikemen_names.inc`), documented when MUGEN's; ZSS gets them as ZSS writes them (`changeState`, `animElemTime`). Hovering a trigger, a controller or a parameter shows its documentation; a `var(n)` or `fvar(n)` its name (Analysis) and how often it is written and read; a state or action number where it is a state or an action, where it is. The file shown goes in the preview tab (dimmed), which the next one takes, unless it is kept: double-clicked, edited, or opened to be kept (a double click in Explorer, Go to file); a tab with changes asks before closing (Save, Don't save, Cancel); the tabs kept are reopened with the debugger. **Save and apply** (Ctrl+S) saves the changed files and the game loads and compiles the characters again without restarting the match (engine side); when that fails, the game keeps the previous version and the line of the error turns red. Engine side, also: the controllers that ran on the tick, in the order they ran (all states: -4, -3, -2, -1, the current state, +1, and any state changed into during the tick); in the file, lines that ran are green, a dot marks each controller, clicking the dot sets a breakpoint. After each trigger line (CNS `trigger`/`triggerall`, ZSS `if`) its value on the tick shown: true, false, undefined, or nothing when it was not evaluated (a trigger after one that held, a state that did not run); after a controller that did not run, how many ticks ago it last did. The dot of a statedef sets a breakpoint on entering that state. Follows the selected entity's state, but not while you type |
| Breakpoints | A Lua condition over Ikemen's triggers, for anyone, one player, or one player and its helpers; the game pauses on the tick the condition becomes true (what is already true when it is set waits until it turns false and true again). Entering a state is the condition `stateNo() == n`, set from the Code window. Engine side: breakpoints on state controllers, the game pauses after a tick in which one ran (with **Only if**, a Lua condition, only when it is true then for the one that ran it). Both are saved with the settings |
| Analysis | The character worked on (the selected player's, or one picked in Explorer, read from disk) read from its files as the engine compiles them (its `st` files, `cmd`, `stcommon`, then config.ini's common states: the first statedef of a number is the one used), CNS and ZSS. **Lint**: errors (states, actions, sounds and commands named that do not exist; a controller without a type, or without trigger1), warnings (a statedef given twice, the AIR's sprites missing from the SFF), notes (states 200 to 4999 nothing changes to by number, commands no trigger names); only numbers written as numbers are checked, and an action guarded by `SelfAnimExist` of it, or shown in a state the character only gives to others, is not. It runs again when a file changes (on disk or in Code); a click shows the line in Code. **Variables**: each var, fvar, sysvar, sysfvar with where it is written (VarSet, VarAdd, VarRandom, VarRangeSet, ParentVarSet/Add, `:=`) and read, by state (a click shows the line), the selected entity's value now, and a name kept per character (shown in the Inspector and when Code's mouse is on it); a click on a var or fvar renumbers it in the character's files (every `var(n)` outside comments, and `v = n`/`fv = n` of VarSet, VarAdd, VarRandom, ParentVarSet/Add; the new number must be free (a VarRangeSet resetting it with others does not count); the common files are left, and the files left unsaved). **States**: the graph around a state (it follows the player, or a state clicked): the states that change to it on the left, those it changes to on the right, 1 to 4 steps, by numbers written in ChangeState, SelfState, p1stateno (dashed: p2stateno, TargetState, Helper); the path the player came by green, states that do not exist red; double-click shows a statedef in Code. Changes the engine makes by itself (landing, getting hit) are not in it |
| File > New (engine side) | A dialog for each, also from the start page and the palette: it blocks the windows behind it, the game goes on; Esc, Cancel or its X leave it with what was filled in kept for the next time, until it is made. **A move**: an attack for the character worked on (the selected player's, or one picked in Explorer or in the dialog: read from disk and written into its files, the game running or not), written as KFM writes its own (CNS or ZSS, as the character's first state file is): name, state (one taken, or a common state's number, is pointed out), standing, crouching or in the air, normal, special or hyper (1000 power), light, medium or hard (the HitDef's values after KFM's punches, each editable: damage, pausetime, spark, slide and hit times, velocities, animtype); the action (its first element with a Clsn1 box arms the HitDef, the spark at the middle of that box; without the action, another one is copied as it); a sound at the start (played here); the command that starts it (a ChangeState at the top of `[Statedef -1]`, with control or not, a hyper's with `power >= 1000`). **Create the move** puts the statedef at the end of the first state file, saves and applies, and Code shows it; the dialog stays, to **Try state N**, which puts the player in it (a character not in the match: **Put it in P1's place**, the game running), or **Show in Code**: each closes it, and it opens again on a new move with the next free state. The buttons to the Animation and Commands windows close it too. The text it writes is shown before. **A character** from another one (`chars/<folder>/<folder>.def`, its name and author): **its basics** (the sprites and sounds renamed after the folder, the other files of `[Files]` as they are, the constants, the statedefs below 200 it gives itself (intro, win poses, taunt) with every ZSS `[Function]`, in a `.cns` or a `.zss` as they were written, the actions of the common states, a new CMD file with the commands the engine needs and the taunt; a character that stands, walks, jumps, guards and gets hit like its base, with no attack yet) or **a full copy**, moves included (the folder copied, the def renamed); added to `select.def` or not (the game's, or without it the one the motif names); put in P1's or P2's place (the match starts again; without the game, the match it starts with), or nowhere; it becomes the character worked on, so Animation, Sprites and **A move** go on with it; the dialog closes once it is made, its report in the Console. **A stage** of PNG images (`stages/<file>.def` and `.sff`, name, author, localcoord: one pixel of the images is one unit), from the back: each one a sprite of the SFF (its own palette, or true colour past 255 colours) and a BG centred with its bottom on the screen's bottom, its delta set here (1 like the floor, less far away); the camera goes as far as every image still covers the screen; a preview shows the images as the camera sees them at its start, the camera's range, the screen and the floor (`zoffset`, dragged) with the players' places; music; added to `select.def`'s `[ExtraStages]`; put in the match's place at once (the match goes on) and made the toolbar's stage (without the game, only the toolbar's); the dialog grows with the images up to the window's height, then the fields scroll, and closes once it is made, an error staying in it in red |
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
`Animation.Draw` tell it), moves BGs and reloads the stage; `editor_model.go`
adds the stage model's node selected and the one under the mouse, their boxes
projected with the matrices `Stage.drawModel` drew with (one line tells it),
finds the node under a point by its triangles and writes the model's changes
into its glTF file; `editor_fight.go`
adds where each element of the fight screen was drawn (`Layout` keeps the
section and prefix it was read from; its draws and `RenderSprite` tell it)
and reloads the fight screen (`findActiveSff`, `findActiveSnd` and
`loadFnt` lend it the files in use); `editor_motif.go` does the same for the
motif (`Anim` and `TextSprite` keep their source; `Anim.Draw` and
`TextSprite.Draw` tell where they went).

New Lua functions: `debugTraceEnable(on)`, `debugTraceRead()` (character id,
source index pairs since the last call), `debugSourceCount()`,
`debugSources(from, count)`, `stateSource([stateNo])`, `setVar`, `setFvar`, `debugSwapStage(def)` (another stage in place of the one in use, the match going on), `debugModel()`, `debugModelAnim(i, on)`, `debugModelAnimTime(i, seconds)`, `debugModelAnimKeys(i)`, `debugModelAnimSampler(anim, sampler, interp, times, values)`, `debugModelNode(i, visible[, children])`, `debugModelNodeTRS(i, translation, rotation, scale)`, `debugModelNodeProps(i, trans, layer, castShadow, zTest, zWrite, outline)`, `debugModelMaterial(i, ...)`, `debugModelLight(i, r, g, b, intensity[, range, inner, outer])`, `debugModelSelect(i)`, `debugModelPick(x, y[, select])`, `debugModelSave()`, `debugModelRevert()` (the stage's 3D model), `debugRunController(text[, zss])` (controllers run once on the player of `playerId`), `debugPerf()` (the frames' timings since the last call),
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
