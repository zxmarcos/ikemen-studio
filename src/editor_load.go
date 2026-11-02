package main

import (
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// A match that fails to load, for an external editor (with "-ikdbg"): the game's window is hidden,
// so its dialog asking to retry would look like a game stuck on its loading screen. The question
// goes to the editor instead, with the frame geometry; the game waits for its answer, frames going on.

type EditorLoadError struct {
	message string // why, "" none
	retry   bool
}

var edLoad EditorLoadError

// editorRetryLoad runs in ShowRetryDialog: whether to load again, and whether it was answered here.
func editorRetryLoad(message string) (retry, answered bool) {
	if !debuggerAttached() {
		return false, false
	}
	e := &edLoad
	if i := strings.Index(message, "\n\nThe file could not be loaded"); i >= 0 {
		e.message = strings.TrimSpace(message[:i])
	} else {
		e.message = strings.TrimSpace(message)
	}
	LogMessage("Error: %v", strings.ReplaceAll(e.message, "\n", " "))
	e.retry = false
	for e.message != "" && !sys.gameEnd {
		sys.await(sys.gameRenderSpeed())
	}
	return e.retry, true
}

// editorErrorDialog runs in ShowErrorDialog: true when the error went to the log, not to a dialog
// nobody would see, behind the hidden window, with the game waiting on it instead of closing.
func editorErrorDialog(message string) bool {
	if !debuggerAttached() {
		return false
	}
	LogMessage("Error: %v", strings.ReplaceAll(strings.TrimSpace(message), "\n", " "))
	return true
}

// editorLoadGeometry adds to the frame geometry "loadError": why a match could not load, while the
// game waits for an answer (debugRetryLoad).
func editorLoadGeometry(b []byte) []byte {
	if edLoad.message == "" {
		return b
	}
	b = append(b, `,"loadError":`...)
	return editorJSONString(b, edLoad.message)
}

func editorLoadFunctions(l *lua.LState) {
	luaRegister(l, "debugRetryLoad", func(l *lua.LState) int {
		/*Answer a match that could not load.
		@function debugRetryLoad
		@tparam boolean retry Load again (the files were fixed), else give up.*/
		edLoad.retry = boolArg(l, 1)
		edLoad.message = ""
		return 0
	})
}
