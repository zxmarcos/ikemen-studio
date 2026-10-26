package main

import (
	"fmt"
	"strconv"

	lua "github.com/yuin/gopher-lua"
)

// Stage editing for an external editor (with "-ikdbg"): where each BG of the
// stage was drawn in the last frame (added to the frame geometry, so the
// editor can show and pick them), moving a BG, and reloading the stage in the
// middle of a match.

type editorStageBg struct {
	index int
	rect  [4]float32 // x, y, w, h in the game picture's pixels: the sprite once, untiled
	k     [2]float32 // pixels per unit of the BG's start
	layer int32
	tiled bool
}

type EditorStage struct {
	stage *Stage
	cur   *backGround // the BG being drawn
	drawn []editorStageBg
}

var edStage EditorStage

// editorStageBgDraw runs before Stage.draw draws a BG.
func editorStageBgDraw(s *Stage, b *backGround) {
	if edFrame.mem == nil {
		return
	}
	if s != edStage.stage {
		edStage.stage = s
		edStage.drawn = edStage.drawn[:0]
	}
	edStage.cur = b
}

// editorSpriteDrawn runs in Animation.Draw before the sprite is rendered: for the BG being
// drawn, it records where the sprite went (as initRenderSpriteQuad places it) and the scale.
func editorSpriteDrawn(a *Animation, rp *RenderParams, xcs, ycs float32, reflection bool) {
	b := edStage.cur
	if b == nil || reflection || a != b.anim || edStage.stage == nil {
		return
	}
	edStage.cur = nil
	index := -1
	for i, bg := range edStage.stage.bg {
		if bg == b {
			index = i
			break
		}
	}
	if index < 0 {
		return
	}
	x, y := rp.x, rp.y
	if rp.xts >= 0 {
		x = -x
	}
	x += rp.rcx
	if rp.ys < 0 {
		y = -y
	}
	y -= rp.rcy
	w := float32(rp.size[0]) * Max(Abs(rp.xts), Abs(rp.xbs))
	if rp.xts < 0 {
		x -= w
	}
	h := float32(rp.size[1]) * Abs(rp.ys)
	bgscl := float32(1)
	if edStage.stage.hires {
		bgscl = 0.5
	}
	edStage.drawn = append(edStage.drawn, editorStageBg{
		index: index,
		rect:  [4]float32{x, -y, w, h},
		k:     [2]float32{xcs * bgscl * sys.widthScale, ycs * bgscl * sys.heightScale},
		layer: b.layerno,
		tiled: b.anim.tile.xflag != 0 || b.anim.tile.yflag != 0,
	})
}

// editorStageGeometry adds the stage to the frame geometry and starts the next frame's list:
// "stage": {"def", "bg": [[index, x, y, w, h, kx, ky, layer, tiled], ...]}.
func editorStageGeometry(b []byte) []byte {
	s := sys.stage
	if s == nil {
		edStage.drawn = edStage.drawn[:0]
		return b
	}
	b = append(b, `,"stage":{"def":`...)
	b = editorJSONString(b, s.def)
	b = append(b, `,"bg":[`...)
	if s == edStage.stage {
		for i, d := range edStage.drawn {
			if i > 0 {
				b = append(b, ',')
			}
			b = append(b, '[')
			b = strconv.AppendInt(b, int64(d.index), 10)
			for _, v := range [...]float32{d.rect[0], d.rect[1], d.rect[2], d.rect[3], d.k[0], d.k[1]} {
				b = append(b, ',')
				b = strconv.AppendFloat(b, float64(v), 'f', 3, 32)
			}
			b = append(b, ',')
			b = strconv.AppendInt(b, int64(d.layer), 10)
			b = append(b, ',')
			b = strconv.AppendBool(b, d.tiled)
			b = append(b, ']')
		}
	}
	edStage.drawn = edStage.drawn[:0]
	return append(b, "]}"...)
}

// editorMoveStageBg moves a BG (0-based index) by stage units, and the BGs positionlinked to it.
func editorMoveStageBg(index int, dx, dy float32) error {
	s := sys.stage
	if s == nil || index < 0 || index >= len(s.bg) {
		return fmt.Errorf("no BG %v", index)
	}
	for i := index; i < len(s.bg); i++ {
		if i > index && !s.bg[i].positionlink {
			break
		}
		s.bg[i].start[0] += dx
		s.bg[i].start[1] += dy
		if s.bg[index].positionlink {
			break // a linked BG moves alone
		}
	}
	return nil
}

// editorReloadStage loads the stage's def again and puts it in place of the one in use;
// the match goes on (the camera keeps its position, the stage its time).
func editorReloadStage() (err error) {
	old := sys.stage
	if old == nil {
		return fmt.Errorf("no stage")
	}
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	// Not as the main stage: that would load its roundXdef stages again
	ns, err := loadStage(old.def, false)
	if err != nil {
		return err
	}
	ns.mainstage = old.mainstage
	ns.stageTime = old.stageTime
	ns.reset()
	for k, v := range sys.stageList {
		if v == old {
			sys.stageList[k] = ns
		}
	}
	sys.stage = ns
	sys.cam.stageCamera = ns.stageCamera
	// What F4 and the next round go back to
	for _, bk := range []*RoundStartBackup{&sys.roundBackup, &sys.matchBackup} {
		if bk.stageBackup.def == old.def {
			bk.stageBackup = *ns
			bk.stageBackup.constants = make(map[string]TypedValue, len(ns.constants))
			for k, v := range ns.constants {
				bk.stageBackup.constants[k] = v
			}
			bk.stageBackup.attachedchardef = append([]string{}, ns.attachedchardef...)
		}
	}
	old.destroy()
	edStage.stage, edStage.cur = nil, nil
	return nil
}

func editorStageFunctions(l *lua.LState) {
	result := func(l *lua.LState, err error) int {
		if err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	}
	luaRegister(l, "debugStageBgMove", func(l *lua.LState) int {
		/*Move a BG of the stage (and the BGs positionlinked to it) until the stage is loaded again.
		@function debugStageBgMove
		@tparam int32 index 0 for the stage's first BG.
		@tparam float32 dx In units of the BG's start.
		@tparam float32 dy
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			return result(l, fmt.Errorf("needs -ikdbg"))
		}
		return result(l, editorMoveStageBg(int(numArg(l, 1)), float32(numArg(l, 2)), float32(numArg(l, 3))))
	})
	luaRegister(l, "debugReloadStage", func(l *lua.LState) int {
		/*Load the stage's def file again and use it at once; the match goes on.
		@function debugReloadStage
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			return result(l, fmt.Errorf("needs -ikdbg"))
		}
		return result(l, editorReloadStage())
	})
}
