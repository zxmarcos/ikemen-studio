package main

import (
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
}

var dbgTrace = DebugTrace{sources: make([]SctrlSource, 1)}

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

func debuggerFunctions(l *lua.LState) {
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
