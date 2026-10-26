package main

import (
	"strconv"

	lua "github.com/yuin/gopher-lua"
)

// Support for external debuggers (started with the "-ikdbg" command line flag).
// The compiler records where each state controller and statedef comes from,
// and every state controller that runs can be traced. Lua scripts read the
// trace and the sources, and get a few setters the debug keys do not need.
// Without the flag, controllers are compiled exactly as before.

// SctrlSource is the place of a state controller (or of a statedef) in its file.
type SctrlSource struct {
	File  string
	Line  int32
	Type  string // controller type, or "statedef"
	State int32
}

// Upper bound of the trace buffer, in case nothing reads it.
const debugTraceMaxEntries = 1 << 20

type DebugTrace struct {
	checked  bool
	attached bool
	sources  []SctrlSource // index 0 is unused: 0 means "no source"
	on       bool
	entries  []int32 // pairs: char id, source index
	// Trigger values of one character, on the last tick it evaluated any
	trigWatch int32   // its id, -1 for none
	trigRaw   int32   // gameTime while they were evaluated
	trigNew   bool    // evaluated since the last frame shared
	trigTick  int32   // gameTime when that frame was shared, as Lua scripts see the tick
	trigVals  []int32 // pairs: source index, value (0 false, 1 true, 2 undefined)
}

var dbgTrace = DebugTrace{sources: make([]SctrlSource, 1), trigWatch: -1}

// debuggerAttached reports whether Ikemen was started with -ikdbg.
func debuggerAttached() bool {
	if !dbgTrace.checked {
		_, dbgTrace.attached = sys.cmdFlags["-ikdbg"]
		dbgTrace.checked = true
	}
	return dbgTrace.attached
}

// debugSource records a source position and returns its index (0 when no debugger is attached).
func debugSource(file string, line int32, typ string, state int32) int32 {
	if !debuggerAttached() {
		return 0
	}
	dbgTrace.sources = append(dbgTrace.sources, SctrlSource{File: file, Line: line, Type: typ, State: state})
	return int32(len(dbgTrace.sources) - 1)
}

// tracedSctrl runs a state controller and, while tracing, records that it ran.
type tracedSctrl struct {
	sc  StateController
	src int32
}

func (t tracedSctrl) Run(c *Char, ps []int32) bool {
	if dbgTrace.on && len(dbgTrace.entries) < debugTraceMaxEntries {
		dbgTrace.entries = append(dbgTrace.entries, c.id, t.src)
	}
	return t.sc.Run(c, ps)
}

// traced wraps a freshly compiled state controller for the debugger, if one is attached.
func (c *CharCompiler) traced(sctrl StateController, line int32, typ string) StateController {
	if !debuggerAttached() {
		return sctrl
	}
	return tracedSctrl{sc: sctrl, src: debugSource(c.currentFile, line, typ, c.stateNo)}
}

// debugTrigger makes a compiled trigger (a CNS trigger line, a ZSS if) record its value
// each time it is evaluated, under a source of type "trigger" at that line.
func (c *CharCompiler) debugTrigger(be BytecodeExp, line int32) BytecodeExp {
	if !debuggerAttached() || len(be) == 0 {
		return be
	}
	be.appendI32Op(OC_debugrec, debugSource(c.currentFile, line, "trigger", c.stateNo))
	return be
}

// debugTriggerRecord runs for OC_debugrec: the trigger's value is on top of the stack.
func debugTriggerRecord(c *Char, src int32) {
	if c == nil || c.id != dbgTrace.trigWatch {
		return
	}
	// A new tick starts over (gameTime only moves at the end of a tick, after the characters)
	if t := sys.gameTime(); t != dbgTrace.trigRaw || !dbgTrace.trigNew {
		dbgTrace.trigRaw = t
		dbgTrace.trigNew = true
		dbgTrace.trigVals = dbgTrace.trigVals[:0]
	}
	v := int32(2)
	if top := sys.bcStack.Top(); !top.IsUndefined() {
		v = Btoi(top.ToB())
	}
	if len(dbgTrace.trigVals) < 1<<16 {
		dbgTrace.trigVals = append(dbgTrace.trigVals, src, v)
	}
}

// debugTriggerGeometry adds the watched character's trigger values to the frame geometry:
// "tg": {"id", "t": the tick as Lua's gameTime() tells it after the tick, "v": [source index,
// value, ...]}.
func debugTriggerGeometry(b []byte) []byte {
	if dbgTrace.trigWatch < 0 {
		return b
	}
	if dbgTrace.trigNew {
		dbgTrace.trigNew = false
		dbgTrace.trigTick = sys.gameTime()
	}
	b = append(b, `,"tg":{"id":`...)
	b = strconv.AppendInt(b, int64(dbgTrace.trigWatch), 10)
	b = append(b, `,"t":`...)
	b = strconv.AppendInt(b, int64(dbgTrace.trigTick), 10)
	b = append(b, `,"v":[`...)
	for i, v := range dbgTrace.trigVals {
		if i > 0 {
			b = append(b, ',')
		}
		b = strconv.AppendInt(b, int64(v), 10)
	}
	return append(b, "]}"...)
}

func debuggerFunctions(l *lua.LState) {
	editorReloadFunctions(l)
	editorStageFunctions(l)
	editorFightFunctions(l)
	editorMotifFunctions(l)
	editorStoryboardFunctions(l)
	editorSelectFunctions(l)
	editorFontFunctions(l)
	editorObjectsFunctions(l)
	luaRegister(l, "debugTriggerWatch", func(l *lua.LState) int {
		/*Record the trigger values of one character (shared with the frames, see editor_frame.go).
		@function debugTriggerWatch
		@tparam int32 id The character's id; -1 for none.*/
		dbgTrace.trigWatch = int32(numArg(l, 1))
		dbgTrace.trigVals = dbgTrace.trigVals[:0]
		return 0
	})
	luaRegister(l, "debugTraceEnable", func(l *lua.LState) int {
		/*Start or stop recording the state controllers that run.
		@function debugTraceEnable
		@tparam boolean on
		@treturn boolean available `false` unless Ikemen was started with `-ikdbg`.*/
		dbgTrace.on = boolArg(l, 1) && debuggerAttached()
		if !dbgTrace.on {
			dbgTrace.entries = dbgTrace.entries[:0]
		}
		l.Push(lua.LBool(debuggerAttached()))
		return 1
	})
	luaRegister(l, "debugTraceRead", func(l *lua.LState) int {
		/*Return the state controllers that ran since the last call, and forget them.
		@function debugTraceRead
		@treturn table entries Flat array: character id, source index, character id, source index...*/
		t := l.CreateTable(len(dbgTrace.entries), 0)
		for i, v := range dbgTrace.entries {
			t.RawSetInt(i+1, lua.LNumber(v))
		}
		dbgTrace.entries = dbgTrace.entries[:0]
		l.Push(t)
		return 1
	})
	luaRegister(l, "debugSourceCount", func(l *lua.LState) int {
		/*Number of recorded sources (the highest source index).
		@function debugSourceCount
		@treturn int32 count*/
		l.Push(lua.LNumber(len(dbgTrace.sources) - 1))
		return 1
	})
	luaRegister(l, "debugSources", func(l *lua.LState) int {
		/*Return the sources from an index on.
		@function debugSources
		@tparam int32 from First source index (1 or more).
		@tparam[opt] int32 count Most sources to return (all by default).
		@treturn table sources Array of {file, line, type, state}; element 1 is source `from`.*/
		from := int(numArg(l, 1))
		if from < 1 {
			from = 1
		}
		to := len(dbgTrace.sources)
		if !nilArg(l, 2) {
			to = Min(to, from+int(numArg(l, 2)))
		}
		t := l.NewTable()
		for i := from; i < to; i++ {
			s := dbgTrace.sources[i]
			e := l.NewTable()
			e.RawSetString("file", lua.LString(s.File))
			e.RawSetString("line", lua.LNumber(s.Line))
			e.RawSetString("type", lua.LString(s.Type))
			e.RawSetString("state", lua.LNumber(s.State))
			t.Append(e)
		}
		l.Push(t)
		return 1
	})
	luaRegister(l, "stateSource", func(l *lua.LState) int {
		/*[redirectable] Source index of the character's current statedef, or of state `stateNo` of the
		owner of its current state. 0 when unknown.
		@function stateSource
		@tparam[opt] int32 stateNo
		@treturn int32 source*/
		c := sys.debugWC
		src := int32(0)
		if sb := c.ss.sb; sb != nil {
			src = sb.src
			if !nilArg(l, 1) {
				src = 0
				if other, ok := sys.cgi[sb.playerNo].states[int32(numArg(l, 1))]; ok {
					src = other.src
				}
			}
		}
		l.Push(lua.LNumber(src))
		return 1
	})
	luaRegister(l, "setVar", func(l *lua.LState) int {
		/*[redirectable] Set var(index).
		@function setVar
		@tparam int32 index
		@tparam int32 value*/
		sys.debugWC.varSet(int32(numArg(l, 1)), int32(numArg(l, 2)))
		return 0
	})
	luaRegister(l, "setFvar", func(l *lua.LState) int {
		/*[redirectable] Set fvar(index).
		@function setFvar
		@tparam int32 index
		@tparam float32 value*/
		sys.debugWC.fvarSet(int32(numArg(l, 1)), float32(numArg(l, 2)))
		return 0
	})
	luaRegister(l, "setSysVar", func(l *lua.LState) int {
		/*[redirectable] Set sysvar(index).
		@function setSysVar
		@tparam int32 index
		@tparam int32 value*/
		sys.debugWC.sysVarSet(int32(numArg(l, 1)), int32(numArg(l, 2)))
		return 0
	})
	luaRegister(l, "setSysFvar", func(l *lua.LState) int {
		/*[redirectable] Set sysfvar(index).
		@function setSysFvar
		@tparam int32 index
		@tparam float32 value*/
		sys.debugWC.sysFvarSet(int32(numArg(l, 1)), float32(numArg(l, 2)))
		return 0
	})
	luaRegister(l, "setPos", func(l *lua.LState) int {
		/*[redirectable] Move the character, in the units posX/posY/posZ return.
		@function setPos
		@tparam float32 x
		@tparam float32 y
		@tparam[opt] float32 z*/
		c := sys.debugWC
		c.setPosX(float32(numArg(l, 1))+sys.cam.Pos[0], true)
		c.setPosY(float32(numArg(l, 2)), true)
		if !nilArg(l, 3) {
			c.setPosZ(float32(numArg(l, 3)), true)
		}
		return 0
	})
	luaRegister(l, "debugMoveBy", func(l *lua.LState) int {
		/*[redirectable] Move the character by pixels of the game's picture (an editor dragging it).
		@function debugMoveBy
		@tparam float32 dx
		@tparam float32 dy*/
		c := sys.debugWC
		kx := sys.cam.Scale * sys.widthScale * c.localscl
		ky := sys.cam.Scale * sys.heightScale * c.localscl
		if kx > 0 && ky > 0 {
			c.setPosX(c.pos[0]+float32(numArg(l, 1))/kx, true)
			c.setPosY(c.pos[1]+float32(numArg(l, 2))/ky, true)
		}
		return 0
	})
	luaRegister(l, "debugSetAnimElem", func(l *lua.LState) int {
		/*[redirectable] Go to an element of the character's current animation (helpers too).
		@function debugSetAnimElem
		@tparam int32 elem 1 for the first element.
		@tparam[opt=0] int32 time Ticks into the element.*/
		c := sys.debugWC
		if c.anim != nil {
			time := int32(0)
			if !nilArg(l, 2) {
				time = int32(numArg(l, 2))
			}
			c.setAnimElem(int32(numArg(l, 1)), time)
		}
		return 0
	})
	luaRegister(l, "setVel", func(l *lua.LState) int {
		/*[redirectable] Set the character's velocity.
		@function setVel
		@tparam float32 x
		@tparam float32 y
		@tparam[opt] float32 z*/
		c := sys.debugWC
		c.vel[0] = float32(numArg(l, 1))
		c.vel[1] = float32(numArg(l, 2))
		if !nilArg(l, 3) {
			c.vel[2] = float32(numArg(l, 3))
		}
		return 0
	})
}
