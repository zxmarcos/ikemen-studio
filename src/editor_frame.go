package main

import (
	"encoding/binary"
	"math"
	"strconv"
	"sync/atomic"
	"unsafe"
)

// Frames for an external editor (started with "-ikdbg"): after each frame is
// drawn, the picture is copied into shared memory together with where the
// players and helpers are in it, so the editor shows the game in its own
// window and can tell what the mouse is over. The editor sends input back
// through the same memory (a ring of events read at the same point).
//
// Shared memory "Local\IkemenFrame<pid>" (Windows only for now):
//
//	header, 4096 bytes, little-endian uint32 values:
//	  0 magic "IKFR", 4 version, 8 slot size, 12 pixel bytes per slot,
//	  16 slot of the last finished frame, 20 frames finished so far,
//	  24 offset of the event ring, 28 its size in events,
//	  32 events written (by the editor), 36 events read (by the game)
//	  64 + 32*slot: sequence (odd while the slot is written), width, height,
//	  geometry length, frame counter
//	slot n at 4096 + n*slot size: width*height RGBA pixels, bottom row first,
//	then the geometry, JSON:
//	  {"f": frame, "vp": [x, y, w, h], "scr": [w, h], "e": [{"id", "pn", "h",
//	   "ax": [x, y], "c1": [l, t, w, h, ...], "c2": [...]}]}
//	  "vp" is where the game's picture is in the frame (top-left origin), "scr"
//	  the size of the game's picture; "ax" and boxes are in its pixels.
//	event ring after the slots: events of 4 int32 values, kind first:
//	  1 key: SDL key code, 1 pressed / 0 released, SDL key modifiers

const (
	editorFrameMagic    = 0x52464B49 // "IKFR"
	editorFrameVersion  = 1
	editorFrameMaxW     = 3840
	editorFrameMaxH     = 2160
	editorFrameHeader   = 4096
	editorFrameSlots    = 2
	editorFramePixels   = editorFrameMaxW * editorFrameMaxH * 4
	editorFrameGeomSize = 1 << 20
	editorFrameSlotSize = editorFramePixels + editorFrameGeomSize
	editorFrameRing     = editorFrameHeader + editorFrameSlots*editorFrameSlotSize
	editorFrameEvents   = 4096
	editorFrameMemSize  = editorFrameRing + editorFrameEvents*16
)

type EditorFrame struct {
	tried bool
	mem   []byte // nil when frames are not shared
	slot  int
	count uint32
	geom  []byte
	boxes DebugClsn
}

var edFrame EditorFrame

func (f *EditorFrame) u32(off int) *uint32 { return (*uint32)(unsafe.Pointer(&f.mem[off])) }

// editorFrameCapture shares the frame just drawn; it runs before the buffers are swapped.
func editorFrameCapture() {
	f := &edFrame
	if !f.tried {
		f.tried = true
		if !debuggerAttached() || len(gfx.GetName()) < 6 || gfx.GetName()[:6] != "OpenGL" {
			return
		}
		if f.mem = editorShmCreate(editorFrameMemSize); f.mem == nil {
			return
		}
		binary.LittleEndian.PutUint32(f.mem[4:], editorFrameVersion)
		binary.LittleEndian.PutUint32(f.mem[8:], editorFrameSlotSize)
		binary.LittleEndian.PutUint32(f.mem[12:], editorFramePixels)
		binary.LittleEndian.PutUint32(f.mem[24:], editorFrameRing)
		binary.LittleEndian.PutUint32(f.mem[28:], editorFrameEvents)
		atomic.StoreUint32(f.u32(0), editorFrameMagic)
	}
	if f.mem == nil {
		return
	}
	f.readEvents()
	w, h := sys.window.GetDrawablePixelSize()
	if w <= 0 || h <= 0 || w > editorFrameMaxW || h > editorFrameMaxH {
		return
	}
	f.slot ^= 1
	head := 64 + 32*f.slot
	data := f.mem[editorFrameHeader+f.slot*editorFrameSlotSize:][:editorFrameSlotSize]
	seq := f.u32(head)
	atomic.StoreUint32(seq, *seq|1)
	gfx.ReadPixels(data[:4*w*h], int(w), int(h))
	geom := editorFrameGeometry(int32(w), int32(h))
	n := copy(data[editorFramePixels:], geom)
	binary.LittleEndian.PutUint32(f.mem[head+4:], uint32(w))
	binary.LittleEndian.PutUint32(f.mem[head+8:], uint32(h))
	binary.LittleEndian.PutUint32(f.mem[head+12:], uint32(n))
	binary.LittleEndian.PutUint32(f.mem[head+16:], uint32(sys.frameCounter))
	atomic.StoreUint32(seq, *seq+1)
	f.count++
	atomic.StoreUint32(f.u32(16), uint32(f.slot))
	atomic.StoreUint32(f.u32(20), f.count)
}

// readEvents applies the input the editor sent since the last frame.
func (f *EditorFrame) readEvents() {
	written := atomic.LoadUint32(f.u32(32))
	read := atomic.LoadUint32(f.u32(36))
	if written-read > editorFrameEvents {
		read = written - editorFrameEvents // the editor got too far ahead: drop the oldest
	}
	for ; read != written; read++ {
		ev := f.mem[editorFrameRing+16*int(read%editorFrameEvents):][:16]
		a := int32(binary.LittleEndian.Uint32(ev[4:]))
		b := int32(binary.LittleEndian.Uint32(ev[8:]))
		c := int32(binary.LittleEndian.Uint32(ev[12:]))
		switch binary.LittleEndian.Uint32(ev) {
		case 1:
			if b != 0 {
				OnKeyPressed(Key(a), ModifierKey(c))
			} else {
				OnKeyReleased(Key(a), ModifierKey(c))
			}
		}
	}
	atomic.StoreUint32(f.u32(36), read)
}

// editorFrameGeometry writes where the players and helpers are, in the game picture's pixels.
func editorFrameGeometry(winW, winH int32) []byte {
	f := &edFrame
	b := f.geom[:0]
	num := func(v float32) {
		if math.IsNaN(float64(v)) || math.IsInf(float64(v), 0) {
			v = 0
		}
		b = strconv.AppendFloat(b, float64(v), 'f', 1, 32)
	}
	vx, vy, vw, vh := sys.window.GetScaledViewportSize()
	b = append(b, `{"f":`...)
	b = strconv.AppendInt(b, int64(sys.frameCounter), 10)
	b = append(b, `,"vp":[`...)
	b = strconv.AppendInt(b, int64(vx), 10)
	b = append(b, ',')
	b = strconv.AppendInt(b, int64(winH-vy-vh), 10)
	b = append(b, ',')
	b = strconv.AppendInt(b, int64(vw), 10)
	b = append(b, ',')
	b = strconv.AppendInt(b, int64(vh), 10)
	b = append(b, `],"scr":[`...)
	b = strconv.AppendInt(b, int64(sys.scrrect[2]), 10)
	b = append(b, ',')
	b = strconv.AppendInt(b, int64(sys.scrrect[3]), 10)
	b = append(b, `],"e":[`...)

	// The same transform as DebugClsn.draw, from game units to the game picture's pixels
	sx, sy := sys.widthScale, sys.heightScale
	toX := func(x float32) float32 { return (x + sys.gameWidth/2) * sx }
	first := true
	for _, chars := range sys.chars {
		for _, c := range chars {
			if c == nil || c.helperIndex < 0 || c.scf(SCF_disabled) {
				continue
			}
			if !first {
				b = append(b, ',')
			}
			first = false
			x := c.pos[0]*c.localscl + c.offsetX()*c.localscl
			y := c.pos[1]*c.localscl + c.offsetY()*c.localscl
			b = append(b, `{"id":`...)
			b = strconv.AppendInt(b, int64(c.id), 10)
			b = append(b, `,"pn":`...)
			b = strconv.AppendInt(b, int64(c.playerNo+1), 10)
			b = append(b, `,"h":`...)
			b = strconv.AppendBool(b, c.helperIndex > 0)
			b = append(b, `,"ax":[`...)
			num(toX((x - sys.cam.Pos[0]) * sys.cam.Scale))
			b = append(b, ',')
			num(((y*sys.cam.Scale - sys.cam.Pos[1]) + sys.cam.GroundLevel()) * sy)
			b = append(b, ']')
			for group := int32(1); group <= 2; group++ {
				f.boxes.rects = f.boxes.rects[:0]
				f.boxes.Add(c.getClsnWorld(group), x, y, c.facing)
				b = append(b, `,"c`...)
				b = strconv.AppendInt(b, int64(group), 10)
				b = append(b, `":[`...)
				for i, r := range f.boxes.rects {
					if i > 0 {
						b = append(b, ',')
					}
					num(toX(r[4] + r[0]))
					b = append(b, ',')
					num((r[5] + r[1]) * sy)
					b = append(b, ',')
					num(r[2] * sx)
					b = append(b, ',')
					num(r[3] * sy)
				}
				b = append(b, ']')
			}
			b = append(b, '}')
		}
	}
	b = append(b, "]}"...)
	f.geom = b
	return b
}
