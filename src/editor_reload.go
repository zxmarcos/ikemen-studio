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
// files as the engine finds them (st, st0.. under "st" in order).
func editorCharFiles(def string) map[string][]string {
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
			files[key] = append(files[key], SearchFile(v, []string{def, "", "data/"}, ""))
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

func editorReloadFunctions(l *lua.LState) {
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
