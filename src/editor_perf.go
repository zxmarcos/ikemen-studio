package main

import (
	"time"

	lua "github.com/yuin/gopher-lua"
)

// Frame timings for an editor's Performance window (with "-ikdbg"): the match
// loop marks the end of each phase of a frame (editorPerfMark, one line at
// each point in runMatch and await), and the phases' times are kept, in
// microseconds, until the editor reads them. Nothing is measured until the
// editor asks (debugPerf), nor after it stops asking for a few seconds.

const (
	perfFrame   = iota // the top of the match loop: the previous frame ends
	perfGame           // after sys.action: the characters, the stage, the fight
	perfUI             // after sys.uiAction: the motif, storyboards, common Lua (the debugger's mod)
	perfInput          // before the draw: sounds, the console, round resets
	perfDraw           // after renderFrame: the frame's draw calls
	perfPresent        // in await, after the frame is shown (EndFrame, the editor's capture, SwapBuffers)
	perfPhases         // the rest of a frame: the wait for its time, and events
)

const perfKeep = 600 // frames kept between two reads

type editorPerfState struct {
	on        bool
	asked     time.Time // the editor's last read
	last      time.Time
	cur       [perfPhases + 2]int32 // the phases, the wait, the capture
	skipped   bool
	frames    []int32 // perfPhases + 2 values a frame: the phases, the wait, the capture, skipped (0 or 1)
	capture   time.Duration
	lostCount int
}

var edPerf editorPerfState

// editorPerfMark ends a phase of the frame.
func editorPerfMark(phase int) {
	if !edPerf.on {
		return
	}
	now := time.Now()
	if edPerf.last.IsZero() {
		// The first mark: a frame starts at the next top of the loop
		if phase == perfFrame {
			edPerf.last = now
		}
		return
	}
	if phase == perfFrame {
		edPerf.cur[perfPhases] = int32(now.Sub(edPerf.last) / time.Microsecond)
		total := int32(0)
		for _, v := range edPerf.cur[1 : perfPhases+1] {
			total += v
		}
		// A frame of more than a second is a load (a round, a match), not a frame
		if total < 1000000 {
			edPerf.cur[perfPhases+1] = int32(edPerf.capture / time.Microsecond)
			skipped := int32(0)
			if edPerf.skipped {
				skipped = 1
			}
			if len(edPerf.frames) >= perfKeep*(perfPhases+2) {
				edPerf.frames = edPerf.frames[perfPhases+2:]
				edPerf.lostCount++
			}
			edPerf.frames = append(edPerf.frames, edPerf.cur[1:]...)
			edPerf.frames = append(edPerf.frames, skipped)
		}
		edPerf.cur = [perfPhases + 2]int32{}
		edPerf.capture, edPerf.skipped = 0, false
		// The editor went away: stop measuring
		if now.Sub(edPerf.asked) > 5*time.Second {
			edPerf.on, edPerf.frames, edPerf.last = false, nil, time.Time{}
			return
		}
	} else {
		edPerf.cur[phase] += int32(now.Sub(edPerf.last) / time.Microsecond)
		if phase == perfDraw {
			edPerf.skipped = sys.frameSkip
		}
	}
	edPerf.last = now
}

func editorPerfFunctions(l *lua.LState) {
	luaRegister(l, "debugPerf", func(l *lua.LState) int {
		/*The frames' timings since the last call, and starts measuring.
		@function debugPerf
		@treturn table {frames = {...}, values = n, lost = n}: per frame, in microseconds, the game,
		the motif and Lua, sounds and input, the draw, showing the frame, the wait; then the
		editor's capture (inside showing it) and 1 when the frame was not drawn.*/
		if !debuggerAttached() {
			return 0
		}
		t := l.NewTable()
		if !edPerf.on {
			edPerf.on, edPerf.last = true, time.Time{}
		}
		edPerf.asked = time.Now()
		frames := l.NewTable()
		frames.RawSetString("_array", lua.LTrue)
		for _, v := range edPerf.frames {
			frames.Append(lua.LNumber(v))
		}
		t.RawSetString("frames", frames)
		t.RawSetString("values", lua.LNumber(perfPhases+2))
		t.RawSetString("lost", lua.LNumber(edPerf.lostCount))
		t.RawSetString("fps", lua.LNumber(sys.gameRenderSpeed()))
		edPerf.frames, edPerf.lostCount = edPerf.frames[:0], 0
		l.Push(t)
		return 1
	})
}
