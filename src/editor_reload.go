package main

import (
	"fmt"
	"os"
	"regexp"
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// Live reload of one character for an editor (with "-ikdbg"): its files are
// loaded and compiled again into a scratch character, and only when that
// works are the new states, commands, constants, animations and sounds put in
// place of the old ones. The match goes on: players keep their state number,
// animation element and time, position, life and variables. Sprites are not
// reloaded (loadSff borrows the SFF in use). The animations alone can be
// compiled again from unsaved text (an editor dragging boxes).

func editorReloadChar(pn int) (err error) {
	if pn < 0 || pn >= len(sys.chars) || len(sys.chars[pn]) == 0 || sys.chars[pn][0] == nil {
		return fmt.Errorf("no player %v", pn+1)
	}
	root := sys.chars[pn][0]
	gi := &sys.cgi[pn]

	// Compile clears the player's string pool, which the running bytecode reads
	oldPool := sys.stringPool[pn]
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
		if err != nil {
			sys.stringPool[pn] = oldPool
		}
	}()

	tmp := newChar(pn, 0)
	ngi := &CharGlobalInfo{}
	ngi.sff = gi.sff
	if err = tmp.load(gi.def, ngi); err != nil {
		return err
	}
	states, err := newCharCompiler().Compile(tmp, gi.def, ngi)
	if err != nil {
		return err
	}

	// States: anything running an old state of this player moves to the new one
	oldStates := gi.states
	gi.states = states
	gi.callFuncs = ngi.callFuncs
	gi.mugenver, gi.ikemenver = ngi.mugenver, ngi.ikemenver
	for _, chars := range sys.chars {
		for _, c := range chars {
			if c == nil || c.ss.sb == nil || c.ss.sb.playerNo != pn || oldStates[c.ss.no] != c.ss.sb {
				continue
			}
			if sb, ok := states[c.ss.no]; ok {
				c.ss.sb = sb
				if n := len(sb.ctrlsps); len(c.ss.ps) != n {
					c.ss.ps = make([]int32, n)
				}
			}
		}
	}

	// Commands: new definitions, same input buffer (helpers share the root's lists)
	if root.cmd != nil && pn < len(root.cmd) && pn < len(tmp.cmd) {
		buf := root.cmd[pn].Buffer
		root.cmd[pn] = tmp.cmd[pn]
		root.cmd[pn].Buffer = buf
	}

	// Constants
	gi.constants = ngi.constants
	gi.data = ngi.data
	gi.velocity = ngi.velocity
	gi.movement = ngi.movement
	gi.attackBase, gi.defenceBase = ngi.attackBase, ngi.defenceBase
	gi.remapPreset = ngi.remapPreset
	gi.quotes = ngi.quotes
	root.size = tmp.size
	root.lifeMax, root.powerMax = tmp.lifeMax, tmp.powerMax
	root.dizzyPointsMax, root.guardPointsMax = tmp.dizzyPointsMax, tmp.guardPointsMax

	// Animations and sounds
	gi.animTable = ngi.animTable
	gi.snd = ngi.snd
	editorRebindAnims(pn)
	return nil
}

// editorRebindAnims gives whoever shows one of this player's animations the
// new one, at the same element and time.
func editorRebindAnims(pn int) {
	for _, chars := range sys.chars {
		for _, c := range chars {
			if c == nil || c.anim == nil || c.animPN != pn || c.animFfx != "" {
				continue
			}
			elem, elemTime, prev := c.anim.curelem+1, c.anim.curelemtime, c.prevAnimNo
			c.changeAnimEx(c.animNo, c.animPN, c.spritePN, c.animFfx)
			c.prevAnimNo = prev
			if c.anim != nil && len(c.anim.frames) > 0 {
				c.anim.SetAnimElem(elem, elemTime)
				c.updateCurFrame()
			}
		}
	}
}

// editorCharFiles reads the [Files] section of a character's def: key -> the
// files as the engine finds them (st, st0.. under "st" in order; a state file
// not found as named is the ".zss" one, as stateCompile does), and the common
// files every character gets (config.ini [Common]: "common.states", "common.air",
// "common.cmd").
func editorCharFiles(def string) map[string][]string {
	files := editorDefFiles(def)
	for key, list := range map[string]map[string][]string{"common.states": sys.cfg.Common.States,
		"common.air": sys.cfg.Common.Air, "common.cmd": sys.cfg.Common.Cmd} {
		for _, k := range SortedKeys(list) {
			for _, v := range list[k] {
				files[key] = append(files[key], editorFindFile(v, []string{def, sys.motif.Def, sys.fightScreen.def, "", "data/"}, key == "common.states"))
			}
		}
	}
	return files
}

// editorFindFile finds a file as the engine does; a state file not found is looked for with
// ".zss" added (common1.cns is data/common1.cns.zss).
func editorFindFile(v string, dirs []string, state bool) string {
	p := SearchFile(v, dirs, "")
	if state && FileExist(p) == "" {
		if z := SearchFile(v+".zss", dirs, ""); FileExist(z) != "" {
			return z
		}
	}
	return p
}

func editorDefFiles(def string) map[string][]string {
	files := map[string][]string{"def": {def}}
	str, err := LoadText(def)
	if err != nil {
		return files
	}
	lines, i := SplitAndTrim(str, "\n"), 0
	for i < len(lines) {
		is, name, _ := ReadIniSection(lines, &i)
		if name != "files" {
			continue
		}
		for _, k := range SortedKeys(is) {
			v := strings.TrimSpace(decodeShiftJIS(is[k]))
			if len(v) >= 2 && (v[0] == '"' || v[0] == '\'') && v[len(v)-1] == v[0] {
				v = v[1 : len(v)-1]
			}
			if v == "" {
				continue
			}
			key := k
			if regexp.MustCompile(`^st[0-9]*$`).MatchString(k) {
				key = "st"
			}
			dirs := []string{def, "", "data/"}
			if key == "stcommon" {
				dirs = []string{def, "", sys.motif.Def, "data/"}
			}
			files[key] = append(files[key], editorFindFile(v, dirs, key == "st" || key == "cmd" || key == "stcommon"))
		}
		break
	}
	return files
}

// editorReloadAnims compiles a player's animations again, the main AIR file
// read from text (when not empty: unsaved changes) instead of the disk.
func editorReloadAnims(pn int, text string) error {
	if pn < 0 || pn >= len(sys.chars) || len(sys.chars[pn]) == 0 || sys.chars[pn][0] == nil {
		return fmt.Errorf("no player %v", pn+1)
	}
	gi := &sys.cgi[pn]
	if gi.sff == nil || gi.palettedata == nil {
		return fmt.Errorf("player %v has no sprites", pn+1)
	}
	ac := newAnimCompiler(gi.sff, &gi.palettedata.palList, true)
	for _, f := range editorCharFiles(gi.def)["anim"] {
		str := text
		if str == "" {
			var err error
			if str, err = LoadText(f); err != nil {
				return err
			}
		}
		ac.compileText(f, str)
	}
	for _, key := range SortedKeys(sys.cfg.Common.Air) {
		for _, v := range sys.cfg.Common.Air[key] {
			LoadFile(&v, []string{gi.def, sys.motif.Def, sys.fightScreen.def, "", "data/"}, "", func(filename string) error {
				txt, err := LoadText(filename)
				if err == nil {
					ac.compileText(filename, txt)
				}
				return err
			})
		}
	}
	gi.animTable = ac.finalize()
	editorRebindAnims(pn)
	return nil
}

// editorReloadSprites reads a player's SFF again (an editor changed it) and
// gives it, with its palettes and animations, to every player using it. The
// animations are compiled from the AIR file on disk.
func editorReloadSprites(pn int) error {
	if pn < 0 || pn >= len(sys.chars) || len(sys.chars[pn]) == 0 || sys.chars[pn][0] == nil {
		return fmt.Errorf("no player %v", pn+1)
	}
	old := sys.cgi[pn].sff
	if old == nil || old.filename == "" {
		return fmt.Errorf("player %v has no SFF file", pn+1)
	}
	// loadSff borrows a loaded SFF of the same name: hide the old one meanwhile
	filename := old.filename
	old.filename = ""
	sff, err := loadSff(filename, SffTypeChar, false, false)
	old.filename = filename
	if err != nil {
		return err
	}
	for i := range sys.cgi {
		gi := &sys.cgi[i]
		if gi.sff != old || i >= len(sys.chars) || len(sys.chars[i]) == 0 || sys.chars[i][0] == nil {
			continue
		}
		gi.sff = sff
		// As Char.load and the loader do: the SFF's palettes, then the ACT files
		gi.palettedata = newPaldata()
		gi.palettedata.palList = PaletteList{
			palettes:   append([][]uint32{}, sff.palList.palettes...),
			paletteMap: append([]int{}, sff.palList.paletteMap...),
			PalTable:   make(map[[2]uint16]int),
			numcols:    make(map[[2]uint16]int),
			PalTex:     append([]Texture{}, sff.palList.PalTex...),
		}
		for key, value := range sff.palList.PalTable {
			gi.palettedata.palList.PalTable[key] = value
		}
		for key, value := range sff.palList.numcols {
			gi.palettedata.palList.numcols[key] = value
		}
		sys.chars[i][0].loadPalettes()
		if err := editorReloadAnims(i, ""); err != nil {
			return err
		}
	}
	return nil
}

// editorReloadSounds reads a player's SND again (an editor changed it) and gives
// it to every player using it.
func editorReloadSounds(pn int) error {
	if pn < 0 || pn >= len(sys.cgi) || sys.cgi[pn].snd == nil || sys.cgi[pn].snd.filename == "" {
		return fmt.Errorf("player %v has no SND file", pn+1)
	}
	old := sys.cgi[pn].snd
	// LoadSnd would borrow the loaded one
	snd, err := LoadSndFiltered(old.filename, func(gn [2]int32) bool { return gn[0] >= 0 && gn[1] >= 0 }, 0)
	if err != nil {
		return err
	}
	snd.filename = old.filename
	for i := range sys.cgi {
		if sys.cgi[i].snd == old {
			sys.cgi[i].snd = snd
		}
	}
	return nil
}

func editorReloadFunctions(l *lua.LState) {
	luaRegister(l, "debugReloadSounds", func(l *lua.LState) int {
		/*Read a player's SND again and give it to every player using it.
		@function debugReloadSounds
		@tparam int32 pn Player number (1 for P1).
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		if err := editorReloadSounds(int(numArg(l, 1)) - 1); err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
	luaRegister(l, "debugReloadSprites", func(l *lua.LState) int {
		/*Read a player's SFF again and give it to every player using it (with palettes and animations).
		@function debugReloadSprites
		@tparam int32 pn Player number (1 for P1).
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		if err := editorReloadSprites(int(numArg(l, 1)) - 1); err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
	luaRegister(l, "debugCharFiles", func(l *lua.LState) int {
		/*A player's files, as the engine finds them.
		@function debugCharFiles
		@tparam int32 pn Player number (1 for P1).
		@treturn table files Key of the def's [Files] section (and "def") -> array of paths.*/
		pn := int(numArg(l, 1)) - 1
		t := l.NewTable()
		if pn >= 0 && pn < len(sys.cgi) && sys.cgi[pn].def != "" {
			for k, v := range editorCharFiles(sys.cgi[pn].def) {
				a := l.NewTable()
				for _, f := range v {
					a.Append(lua.LString(f))
				}
				t.RawSetString(k, a)
			}
		}
		l.Push(t)
		return 1
	})
	luaRegister(l, "debugReloadAnims", func(l *lua.LState) int {
		/*Compile a player's animations again, keeping where everyone is in them.
		@function debugReloadAnims
		@tparam int32 pn Player number (1 for P1).
		@tparam[opt] string textFile A file holding the text to use in place of the main AIR file.
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		text := ""
		if !nilArg(l, 2) {
			b, err := os.ReadFile(strArg(l, 2))
			if err != nil {
				l.Push(lua.LFalse)
				l.Push(lua.LString(err.Error()))
				return 2
			}
			text = string(b)
		}
		if err := editorReloadAnims(int(numArg(l, 1))-1, text); err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
	luaRegister(l, "debugReloadChar", func(l *lua.LState) int {
		/*Load and compile a player's files again during the match (states, commands,
		constants, animations, sounds), keeping where everyone is. Nothing changes if it fails.
		@function debugReloadChar
		@tparam int32 pn Player number (1 for P1).
		@treturn boolean ok
		@treturn string error The compiler's message when it failed.*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		if err := editorReloadChar(int(numArg(l, 1)) - 1); err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
}
