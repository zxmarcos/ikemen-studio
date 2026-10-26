package main

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	lua "github.com/yuin/gopher-lua"
)

// State controllers typed in an editor's console (with "-ikdbg"): the text is
// compiled with the character's compiler, as a negative statedef of its own
// that no character has, and run once on a player at once, as the negative
// states run: a ChangeState in it is taken at once and the new state's code
// runs on the next tick.

const editorConsoleState = -2000000000

var editorConsoleLine = regexp.MustCompile(`console:(\d+):`)

// The text as a state file: CNS controllers get a [State] header when they
// have none, and "trigger1 = 1" when they have no trigger; ZSS statements go
// under a statedef header. Also the lines added before the text.
func editorConsoleText(text string, zss bool) (string, int) {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	if zss {
		return fmt.Sprintf("[StateDef %d;]\n%s\n", editorConsoleState, text), 1
	}
	head, added := fmt.Sprintf("[Statedef %d]\n", editorConsoleState), 1
	lower := strings.ToLower(text)
	if !regexp.MustCompile(`(?m)^\s*\[\s*state\s`).MatchString(lower) {
		head += "[State console]\n"
		added++
	}
	if !regexp.MustCompile(`(?m)^\s*trigger(all|\d+)\s*=`).MatchString(lower) {
		text += "\ntrigger1 = 1"
	}
	return head + text + "\n", added
}

func editorRunController(c *Char, text string, zss bool) (err error) {
	if c == nil {
		return fmt.Errorf("no player")
	}
	pn := c.playerNo
	gi := &sys.cgi[pn]
	src, added := editorConsoleText(text, zss)
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
		if err != nil {
			// Line numbers of the text as typed
			err = fmt.Errorf("%s", editorConsoleLine.ReplaceAllStringFunc(err.Error(), func(m string) string {
				n, _ := strconv.Atoi(editorConsoleLine.FindStringSubmatch(m)[1])
				return fmt.Sprintf("line %d:", n-added)
			}))
		}
	}()

	comp := newCharCompiler()
	comp.playerNo = pn
	comp.targetGi = gi
	if root := sys.chars[pn][0]; root != nil && pn < len(root.cmd) {
		comp.cmdl = &root.cmd[pn]
	}
	for k, v := range gi.callFuncs {
		comp.funcs[k] = v
	}
	states := make(map[int32]*StateBytecode)
	if zss {
		err = comp.stateCompileZSS(states, "console", src, gi.constants)
	} else {
		err = comp.stateCompileCNS(states, "console", src, false, gi.constants)
	}
	if err != nil {
		return err
	}
	sb := states[editorConsoleState]
	if sb == nil {
		return fmt.Errorf("no controller")
	}

	// Run as a negative state, not traced (its lines are in no file)
	oldMinus, oldWs, oldTrace := c.minus, sys.workingState, dbgTrace.on
	c.minus, dbgTrace.on = -2, false
	sb.run(c)
	c.minus, sys.workingState, dbgTrace.on = oldMinus, oldWs, oldTrace
	c.stateChange2()
	return nil
}

func editorConsoleFunctions(l *lua.LState) {
	luaRegister(l, "debugRunController", func(l *lua.LState) int {
		/*Compile state controllers with the player's compiler and run them on it once.
		@function debugRunController
		@tparam string text CNS controllers (a [State] header and a trigger are added when missing) or ZSS statements.
		@tparam[opt] boolean zss The text is ZSS.
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		if err := editorRunController(sys.debugWC, strArg(l, 1), boolArg(l, 2)); err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
}
