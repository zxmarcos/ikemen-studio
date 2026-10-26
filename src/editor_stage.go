package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

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
	stage  *Stage
	cur    *backGround // the BG being drawn
	drawn  []editorStageBg
	ran    []int // the BGCtrls that ran on the stage's last step
	ranAt  int32 // its stage time
	ranFor *Stage
	// The last stage model read, kept for the editor's reloads (a big glTF takes a while)
	reloading bool
	model     *Model
	modelFile string
	modelTime time.Time
}

// editorStageModel runs in loadStage for its model: while the editor reloads the stage, a model
// file read before and not changed since is not read again.
func editorStageModel(filename string) (*Model, error) {
	e := &edStage
	if !debuggerAttached() {
		return loadglTFModel(filename)
	}
	var mtime time.Time
	if st, err := os.Stat(filename); err == nil {
		mtime = st.ModTime()
	}
	if e.reloading && e.model != nil && e.modelFile == filename && !mtime.IsZero() && mtime.Equal(e.modelTime) {
		return e.model, nil
	}
	m, err := loadglTFModel(filename)
	if err == nil {
		e.model, e.modelFile, e.modelTime = m, filename, mtime
	}
	return m, err
}

// editorKeepStage, after a reload, has the new stage go on from where the old one was: its time,
// its BGs' movement (what their velocity and sin moved them), visibility and animations, its
// model's animations. Not when BGs were added or removed (which BG was which is not known).
func editorKeepStage(o, n *Stage) {
	n.stageTime = o.stageTime
	n.bga = o.bga
	if len(o.bg) == len(n.bg) {
		for i, b := range n.bg {
			ob := o.bg[i]
			b.bga.pos, b.bga.sintime, b.bga.sinoffset, b.bga.offset = ob.bga.pos, ob.bga.sintime, ob.bga.sinoffset, ob.bga.offset
			b.visible, b.enabled = ob.visible, ob.enabled
			if b.anim != nil && ob.anim != nil && len(b.anim.frames) > 0 && len(ob.anim.frames) > 0 {
				b.anim.SetAnimElem(ob.anim.curelem+1, ob.anim.curelemtime)
			}
		}
	}
}

// editorModelPlayback saves where a model's animations are, and puts them back (Stage.reset
// restarts them, and a kept model is the old stage's).
func editorModelPlayback(m *Model) func(*Model) {
	if m == nil {
		return func(*Model) {}
	}
	type state struct {
		time    float32
		enabled bool
		loop    int32
	}
	saved := make([]state, len(m.animations))
	for i, a := range m.animations {
		saved[i] = state{a.time, a.enabled, a.loop}
	}
	return func(n *Model) {
		if n == nil || len(n.animations) != len(saved) {
			return
		}
		for i, a := range n.animations {
			a.time, a.enabled, a.loop = saved[i].time, saved[i].enabled, saved[i].loop
		}
	}
}

// editorBgCtrlRan runs in Stage.bgCtrlAction for each BGCtrl that runs.
func editorBgCtrlRan(s *Stage, index int) {
	e := &edStage
	if e.ranFor != s || e.ranAt != s.stageTime {
		e.ranFor, e.ranAt = s, s.stageTime
		e.ran = e.ran[:0]
	}
	e.ran = append(e.ran, index)
}

var edStage EditorStage

// The camera frames are drawn with: the game's (zoom effects included), or the editor's free
// camera, which only changes what is drawn (the characters' screen bounds keep the game's).
type EditorCamera struct {
	free         bool
	x, y, scl    float32 // the free camera: world x, world y, scale
	drawX, drawY float32 // as System.draw takes them (y premultiplied by the scale)
	drawScl      float32
	drawSet      bool
}

var edCam EditorCamera

// editorCamera runs before each frame is drawn, with the camera it is drawn with.
func editorCamera(x, y, scl float32) (float32, float32, float32) {
	c := &edCam
	if c.free && debuggerAttached() {
		x, y, scl = c.x, c.y*c.scl, c.scl
	}
	c.drawX, c.drawY, c.drawScl, c.drawSet = x, y, scl, true
	return x, y, scl
}

// editorDrawCamera: the camera of the last frame drawn, as sys.cam.Pos and sys.cam.Scale would give it.
func editorDrawCamera() (x, y, scale float32) {
	if !edCam.drawSet {
		return sys.cam.Pos[0], sys.cam.Pos[1], sys.cam.Scale
	}
	return edCam.drawX, edCam.drawY, edCam.drawScl * sys.cam.BaseScale()
}

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
// drawn, it records where the sprite went (as initRenderSpriteQuad places it) and the scale;
// for a screen's element, the action and element shown.
func editorSpriteDrawn(a *Animation, rp *RenderParams, xcs, ycs float32, reflection bool) {
	// A screen's element showing an action of its def (editor_fight.go)
	if e := &edFight; e.cur > 0 && a.filename != "" {
		e.drawn[e.cur-1].action, e.drawn[e.cur-1].elem = a.actionNumber, a.curelem+1
	}
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
	bgscl := float32(1)
	if edStage.stage.hires {
		bgscl = 0.5
	}
	edStage.drawn = append(edStage.drawn, editorStageBg{
		index: index,
		rect:  editorQuadRect(rp),
		k:     [2]float32{xcs * bgscl * sys.widthScale, ycs * bgscl * sys.heightScale},
		layer: b.layerno,
		tiled: b.anim.tile.xflag != 0 || b.anim.tile.yflag != 0,
	})
}

// editorStageGeometry adds the stage to the frame geometry and starts the next frame's list:
//
//	"stage": {"def", "free": the editor's camera is on,
//	  "cam": [x, y, s, gl, gameWidth, widthScale, heightScale]: a point (X, Y) of the world (a
//	    character's pos * localscl) is at picture pixel ((X - x) * s + gameWidth/2) * widthScale,
//	    (Y * s - y + gl) * heightScale,
//	  "lim": in the world: [stage localscl, camera boundL, boundR, boundH, boundLo, minLeft,
//	    maxRight, above and below the ground on screen, players' leftbound, rightbound, camera
//	    startx, starty],
//	  "start": [[x, y], ...] P1, P2, P3, P4 starting positions in the world,
//	  "time": the stage's time, "ranAt": the stage time of its last step,
//	  "bgc": [[ran on that step, starttime, endtime, looptime, [BG indices]], ...] the BGCtrls
//	    (the def's [BGCtrl ...] sections, in order),
//	  "bg": [[index, x, y, w, h, kx, ky, layer, tiled], ...]}
func editorStageGeometry(b []byte) []byte {
	s := sys.stage
	if s == nil {
		edStage.drawn = edStage.drawn[:0]
		return b
	}
	nums := func(vals ...float32) {
		b = append(b, '[')
		for i, v := range vals {
			if i > 0 {
				b = append(b, ',')
			}
			b = strconv.AppendFloat(b, float64(v), 'f', 3, 32)
		}
		b = append(b, ']')
	}
	c := &sys.cam
	b = append(b, `,"stage":{"def":`...)
	b = editorJSONString(b, s.def)
	b = append(b, `,"free":`...)
	b = strconv.AppendBool(b, edCam.free)
	cx, cy, cs := editorDrawCamera()
	b = append(b, `,"cam":`...)
	nums(cx, cy, cs, c.GroundLevel(), sys.gameWidth, sys.widthScale, sys.heightScale)
	l := s.localscl
	above := c.zoff - c.aspectcorrection
	b = append(b, `,"lim":`...)
	nums(l, c.boundL, c.boundR, c.boundH, c.boundLo, c.minLeft, c.maxRight, above, sys.gameHeight-above,
		s.leftbound*l, s.rightbound*l, float32(c.startx)*l, float32(c.starty)*l)
	b = append(b, `,"start":[`...)
	for i := 0; i < 4 && i < len(s.p); i++ {
		if i > 0 {
			b = append(b, ',')
		}
		nums(float32(s.p[i].startx)*l, float32(s.p[i].starty)*l)
	}
	b = append(b, `],"time":`...)
	b = strconv.AppendInt(b, int64(s.stageTime), 10)
	b = append(b, `,"ranAt":`...)
	b = strconv.AppendInt(b, int64(edStage.ranAt), 10)
	b = append(b, `,"bgc":[`...)
	for i := range s.bgc {
		bgc := &s.bgc[i]
		if i > 0 {
			b = append(b, ',')
		}
		ran := false
		if edStage.ranFor == s {
			for _, r := range edStage.ran {
				ran = ran || r == i
			}
		}
		b = append(b, '[')
		b = strconv.AppendBool(b, ran)
		for _, v := range [...]int32{bgc.starttime, bgc.endtime, bgc.looptime} {
			b = append(b, ',')
			b = strconv.AppendInt(b, int64(v), 10)
		}
		b = append(b, ",["...)
		n := 0
		for _, target := range bgc.bg {
			for j, bg := range s.bg {
				if bg == target {
					if n > 0 {
						b = append(b, ',')
					}
					b = strconv.AppendInt(b, int64(j), 10)
					n++
				}
			}
		}
		b = append(b, "]]"...)
	}
	b = append(b, `],"bg":[`...)
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
// the match goes on (the camera keeps its position, the stage goes on: editorKeepStage; its
// SFF and model are not read again). textFile, when not empty, is read instead of the def, then
// deleted: unsaved text, written next to it (its files are found from where it is).
func editorReloadStage(textFile string) (err error) {
	old := sys.stage
	if old == nil {
		return fmt.Errorf("no stage")
	}
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	from := old.def
	if textFile != "" {
		from = textFile
		defer os.Remove(textFile)
	}
	// Not as the main stage: that would load its roundXdef stages again
	modelPlayback := editorModelPlayback(old.model)
	edStage.reloading = true
	defer func() { edStage.reloading = false }()
	ns, err := loadStage(from, false)
	if err != nil {
		return err
	}
	ns.def = old.def
	ns.mainstage = old.mainstage
	ns.reset()
	editorKeepStage(old, ns)
	modelPlayback(ns.model)
	// The BGs' sprites are set when the stage steps; a paused game would draw none
	for _, b := range ns.bg {
		b.anim.UpdateSprite()
	}
	for k, v := range sys.stageList {
		if v == old {
			sys.stageList[k] = ns
		}
	}
	sys.stage = ns
	// The camera takes the new bounds and zoom limits, and stays where it is
	sys.cam.stageCamera = ns.stageCamera
	sys.cam.Reset()
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
		@tparam[opt] string textFile Read this file instead, then delete it (unsaved text, in the def's folder).
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			return result(l, fmt.Errorf("needs -ikdbg"))
		}
		textFile := ""
		if !nilArg(l, 1) {
			textFile = strArg(l, 1)
		}
		return result(l, editorReloadStage(textFile))
	})
	luaRegister(l, "debugStageReset", func(l *lua.LState) int {
		/*Start the stage over: its time back to 0, its BGs where they start (BGCtrls run again).
		@function debugStageReset*/
		if s := sys.stage; s != nil && debuggerAttached() {
			s.reset()
			for _, b := range s.bg {
				b.anim.UpdateSprite()
			}
		}
		return 0
	})
	luaRegister(l, "debugCamera", func(l *lua.LState) int {
		/*The editor's free camera, which only changes what is drawn (the game's camera goes on).
		@function debugCamera
		@tparam boolean on
		@tparam[opt] float32 x World x of the middle of the screen (default: where the camera is).
		@tparam[opt] float32 y World y of the ground line's place on screen.
		@tparam[opt] float32 scale*/
		c := &edCam
		on := boolArg(l, 1) && debuggerAttached()
		if on && !c.free {
			c.x, c.y, c.scl = sys.cam.Pos[0], sys.cam.Pos[1], 1
			if c.drawSet && c.drawScl > 0 {
				c.x, c.y, c.scl = c.drawX, c.drawY/c.drawScl, c.drawScl
			}
		}
		c.free = on
		if !nilArg(l, 2) {
			c.x = float32(numArg(l, 2))
		}
		if !nilArg(l, 3) {
			c.y = float32(numArg(l, 3))
		}
		if !nilArg(l, 4) {
			c.scl = Clamp(float32(numArg(l, 4)), 0.05, 8)
		}
		return 0
	})
}
