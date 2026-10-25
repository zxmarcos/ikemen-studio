package main

import (
	"fmt"

	lua "github.com/yuin/gopher-lua"
)

// Live reload of one character for an editor (with "-ikdbg"): its files are
// loaded and compiled again into a scratch character, and only when that
// works are the new states, commands, constants, animations and sounds put in
// place of the old ones. The match goes on: players keep their state number,
// animation element and time, position, life and variables. Sprites are not
// reloaded (loadSff borrows the SFF in use).

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

	// Animations and sounds: whoever shows one of this player's animations gets
	// the new one, at the same element and time
	gi.animTable = ngi.animTable
	gi.snd = ngi.snd
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
	return nil
}

func editorReloadFunctions(l *lua.LState) {
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
