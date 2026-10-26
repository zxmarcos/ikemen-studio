package main

import (
	"fmt"

	lua "github.com/yuin/gopher-lua"
)

// Roster editing for an external editor (with "-ikdbg"): the characters and stages of
// select.def, which the scripts add one by one at start (addChar, addStage), are emptied so
// that the scripts can add them again from the edited text (the mod's ikdbg.reloadRoster).

// editorSelectReset empties the characters and stages added, once the preload worker is not
// loading any of them (it writes into the lists by index).
func editorSelectReset() error {
	if sys.gameRunning {
		return fmt.Errorf("not during a match")
	}
	s := &sys.sel
	s.initPreloadSync()
	s.preloadMu.Lock()
	defer s.preloadMu.Unlock()
	loading := func() bool {
		for _, e := range s.charPreload {
			if e.State == PS_Loading {
				return true
			}
		}
		for _, e := range s.stagePreload {
			if e.State == PS_Loading {
				return true
			}
		}
		return false
	}
	for loading() {
		s.preloadCond.Wait()
	}
	s.charlist, s.stagelist = nil, nil
	s.charPreload, s.stagePreload = nil, nil
	s.selected = [2][][2]int{}
	s.selectedStageNo = -1
	return nil
}

func editorSelectFunctions(l *lua.LState) {
	luaRegister(l, "debugSelectReset", func(l *lua.LState) int {
		/*Empty the characters and stages added (select.def), so that the scripts add them again.
		Not during a match.
		@function debugSelectReset
		@treturn boolean ok
		@treturn string error*/
		var err error
		if !debuggerAttached() {
			err = fmt.Errorf("needs -ikdbg")
		} else {
			err = editorSelectReset()
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
