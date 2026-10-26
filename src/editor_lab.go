package main

import (
	"fmt"
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// The lab of an external editor (with "-ikdbg"): a player's keys held by the editor (a dummy),
// its input recorded tick by tick and played back, and a player's character swapped in the
// middle of a match.

// A tick of input: the 14 buttons as InputUpdate reads them (U, D, L, R, a, b, c, x, y, z, s, d,
// w, m), one bit each, with back and forward in place of left and right (bits 2 and 3), so that
// a take works facing either way.
type editorInputTick uint16

const editorInputKeys = "UDBFabcxyzsdwm"

func editorTickOf(buttons [14]bool, flip bool) (t editorInputTick) {
	for i, on := range buttons {
		if on {
			t |= 1 << editorTickBit(i, flip)
		}
	}
	return
}

func (t editorInputTick) buttons(flip bool) (b [14]bool) {
	for i := range b {
		b[i] = t&(1<<editorTickBit(i, flip)) != 0
	}
	return
}

// editorTickBit: the bit of button i (L is back unless the player's back and forward are flipped).
func editorTickBit(i int, flip bool) int {
	if flip && (i == 2 || i == 3) {
		return 5 - i
	}
	return i
}

type editorLabPlayer struct {
	holding   bool
	hold      editorInputTick // keys held while holding
	recording bool
	take      []editorInputTick
	play      []editorInputTick // the take being played
	playAt    int
	loop      bool
}

type EditorLab struct {
	p    [MaxPlayerNo]editorLabPlayer
	seed int32 // the random seed when the last recording started
}

var edLab EditorLab

// editorInputButtons runs in CommandList.InputUpdate once a player's buttons are read: they are
// recorded, or replaced by the editor's (keys held, a take played).
func editorInputButtons(c *Char, buttons [14]bool) [14]bool {
	if c == nil || c.helperIndex != 0 || c.playerNo < 0 || c.playerNo >= MaxPlayerNo || !debuggerAttached() {
		return buttons
	}
	p := &edLab.p[c.playerNo]
	if p.play != nil && p.playAt >= len(p.play) {
		if p.loop {
			p.playAt = 0
		} else {
			p.play = nil
		}
	}
	if p.play != nil {
		buttons = p.play[p.playAt].buttons(c.fbFlip)
		p.playAt++
	} else if p.holding {
		buttons = p.hold.buttons(c.fbFlip)
	}
	if p.recording {
		p.take = append(p.take, editorTickOf(buttons, c.fbFlip))
	}
	return buttons
}

// editorSwapChar puts another character in player pn's place (0-based); the match starts again
// with it (as the MatchRestart controller's reload).
func editorSwapChar(pn int, def string, pal int) error {
	if !sys.gameRunning {
		return fmt.Errorf("only during a match")
	}
	if pn < 0 || pn >= MaxPlayerNo {
		return fmt.Errorf("no player %v", pn+1)
	}
	s := &sys.sel
	team, member := pn&1, pn>>1
	sys.selMutex.RLock()
	n := len(s.selected[team])
	sys.selMutex.RUnlock()
	if member >= n {
		return fmt.Errorf("player %v is not in the match", pn+1)
	}
	// The character's entry in the select list: added, or the one there already
	added := len(s.charlist)
	if s.AddChar(def) == nil {
		s.charlist = s.charlist[:added]
		return fmt.Errorf("cannot load %v", def)
	}
	idx := added
	for i := 0; i < added; i++ {
		if strings.EqualFold(s.charlist[i].def, s.charlist[added].def) {
			idx = i
			s.charlist = s.charlist[:added]
			break
		}
	}
	if pal < 1 {
		pal = 1
	}
	sys.selMutex.Lock()
	s.selected[team][member] = [2]int{idx, s.ValidatePalette(idx, pal)}
	sys.selMutex.Unlock()
	sys.reloadCharSlot[pn] = true
	sys.reloadFlg = true
	return nil
}

func editorLabFunctions(l *lua.LState) {
	player := func(l *lua.LState) *editorLabPlayer {
		pn := int(numArg(l, 1))
		if pn < 1 || pn > MaxPlayerNo {
			l.RaiseError("\nInvalid player number: %v\n", pn)
		}
		return &edLab.p[pn-1]
	}
	luaRegister(l, "debugInputHold", func(l *lua.LState) int {
		/*Hold keys for a player, in place of its own input (a dummy), until called without them.
		@function debugInputHold
		@tparam int pn Player number (1-based).
		@tparam[opt] string keys Letters of UDBFabcxyzsdwm (B and F: back and forward); "" holds none;
		  nil gives the player its input back.*/
		p := player(l)
		p.holding = !nilArg(l, 2) && debuggerAttached()
		p.hold = 0
		if p.holding {
			for _, k := range strArg(l, 2) {
				if i := strings.IndexRune(editorInputKeys, k); i >= 0 {
					p.hold |= 1 << i
				}
			}
		}
		return 0
	})
	luaRegister(l, "debugInputRecord", func(l *lua.LState) int {
		/*Start or stop recording a player's input, tick by tick.
		@function debugInputRecord
		@tparam int pn Player number (1-based).
		@tparam boolean on
		@treturn table take When stopping: one number per tick, a bit per key of UDBFabcxyzsdwm.
		@treturn int32 seed The random seed when the recording started.*/
		p := player(l)
		on := boolArg(l, 2) && debuggerAttached()
		if on {
			p.take, p.recording = nil, true
			edLab.seed = sys.randseed
			return 0
		}
		p.recording = false
		t := l.CreateTable(len(p.take), 0)
		for i, v := range p.take {
			t.RawSetInt(i+1, lua.LNumber(v))
		}
		l.Push(t)
		l.Push(lua.LNumber(edLab.seed))
		return 2
	})
	luaRegister(l, "debugInputPlay", func(l *lua.LState) int {
		/*Play a take as a player's input (an empty one, or none, stops it).
		@function debugInputPlay
		@tparam int pn Player number (1-based).
		@tparam[opt] table take As debugInputRecord gives it.
		@tparam[opt] boolean loop
		@tparam[opt] int32 seed Set the random seed first (the one of the recording, for the same random numbers).*/
		p := player(l)
		p.play, p.playAt, p.loop = nil, 0, false
		if nilArg(l, 2) || !debuggerAttached() {
			return 0
		}
		t := tableArg(l, 2)
		for i := 1; i <= t.Len(); i++ {
			if v, ok := t.RawGetInt(i).(lua.LNumber); ok {
				p.play = append(p.play, editorInputTick(v))
			}
		}
		p.loop = !nilArg(l, 3) && boolArg(l, 3)
		if !nilArg(l, 4) {
			sys.randseed = int32(numArg(l, 4))
		}
		return 0
	})
	luaRegister(l, "debugInputState", func(l *lua.LState) int {
		/*What the editor does with a player's input.
		@function debugInputState
		@tparam int pn Player number (1-based).
		@treturn boolean recording
		@treturn int32 recorded Ticks recorded so far.
		@treturn boolean playing
		@treturn int32 at Ticks of the take played so far.
		@treturn int32 length The take's length.*/
		p := player(l)
		l.Push(lua.LBool(p.recording))
		l.Push(lua.LNumber(len(p.take)))
		l.Push(lua.LBool(p.play != nil))
		l.Push(lua.LNumber(p.playAt))
		l.Push(lua.LNumber(len(p.play)))
		return 5
	})
	luaRegister(l, "debugSwapChar", func(l *lua.LState) int {
		/*Put another character in a player's place; the match starts again with it.
		@function debugSwapChar
		@tparam int pn Player number (1-based).
		@tparam string def The character, as select.def names one ("kfm", "kfm/kfm.def").
		@tparam[opt] int pal Palette (1 by default).
		@treturn boolean ok
		@treturn string error*/
		var err error
		if !debuggerAttached() {
			err = fmt.Errorf("needs -ikdbg")
		} else {
			pal := 1
			if !nilArg(l, 3) {
				pal = int(numArg(l, 3))
			}
			err = editorSwapChar(int(numArg(l, 1))-1, strArg(l, 2), pal)
		}
		if err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
}
