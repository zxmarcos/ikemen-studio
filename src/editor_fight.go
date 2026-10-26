package main

import (
	"fmt"
	"os"
	"reflect"
	"strconv"
	"strings"
	"unsafe"

	lua "github.com/yuin/gopher-lua"
)

// Fight screen editing for an external editor (with "-ikdbg"): which section and prefix of the
// fight screen's def each Layout was read from (Layout.editor), where each was drawn in the last
// frame (added to the frame geometry, so the editor can show and pick them), and reloading the
// fight screen in the middle of a match.

// The defs whose elements are recorded: fight screen, motif (system.def, editor_motif.go),
// storyboard (editor_storyboard.go)
const (
	editorFileFight int8 = iota
	editorFileMotif
	editorFileStoryboard
)

type editorFightSource struct {
	file    int8   // editorFileFight...
	section string // lowercase, as "simul lifebar"
	prefix  string // lowercase, as "p1.bg0."
	bg      int32  // a BG of a BGDef: 1 + its index among the sections named after section ("titlebg")
}

type editorFightDrawn struct {
	src  int32
	rect [4]float32 // x, y, w, h in the game picture's pixels: all the quads drawn, clipped by their window
	k    [2]float32 // pixels per unit of the def's positions
	n    int        // quads drawn
	// The action of the def it shows (Begin Action) and its element (1-based), -1: none (a sprite, a text)
	action, elem int32
}

type EditorFight struct {
	section string // the section loadFightScreen is reading, "" outside it
	sources []editorFightSource
	index   map[editorFightSource]int32
	cur     int // 1 + the index in drawn of the Layout being drawn, 0 for none
	drawn   []editorFightDrawn
	keep    bool            // reloading after an edit of the text: the files in use are borrowed
	fonts   map[string]*Fnt // the fonts fight screens and motifs loaded, by file name and height
	inFont  bool
}

// editorSourceIndex: 1 + the index of a source in the table, added when new.
func editorSourceIndex(key editorFightSource) int32 {
	e := &edFight
	if i, ok := e.index[key]; ok {
		return i
	}
	if e.index == nil {
		e.index = make(map[editorFightSource]int32)
	}
	e.sources = append(e.sources, key)
	i := int32(len(e.sources))
	e.index[key] = i
	return i
}

// editorDrawBegin starts recording where a source is drawn (k: pixels per unit of its def's
// positions); editorLayoutDrawn ends it.
func editorDrawBegin(src int32, kx, ky float32) {
	e := &edFight
	e.cur = 0
	if src == 0 || edFrame.mem == nil || len(e.drawn) >= 4096 {
		return
	}
	e.drawn = append(e.drawn, editorFightDrawn{src: src, k: [2]float32{kx, ky}, action: -1, elem: -1})
	e.cur = len(e.drawn)
}

var edFight EditorFight

// editorKeptSff and editorKeptSnd run in findActiveSff and findActiveSnd: while the editor
// reloads the fight screen or the motif after an edit of its text, their SFF and SND are not
// read again.
func editorKeptSff(filename string) *Sff {
	if s := sys.fightScreen.sff; edFight.keep && s != nil && s.filename == filename {
		return s
	}
	if s := sys.motif.Sff; edMotif.keep && s != nil && s.filename == filename {
		return s
	}
	if s := sys.storyboard.Sff; edStoryboard.keep && s != nil && s.filename == filename {
		return s
	}
	return nil
}

func editorKeptSnd(filename string) *Snd {
	if s := sys.fightScreen.snd; edFight.keep && s != nil && s.filename == filename {
		return s
	}
	if s := sys.motif.Snd; edMotif.keep && s != nil && s.filename == filename {
		return s
	}
	if s := sys.storyboard.Snd; edStoryboard.keep && s != nil && s.filename == filename {
		return s
	}
	return nil
}

// editorKeptFont runs in loadFnt: the fonts of fight screens and motifs are kept, and not read
// again while the editor reloads one after an edit of its text.
func editorKeptFont(filename string, height int32) *Fnt {
	e := &edFight
	if (e.section == "" && !edMotif.loading && !edStoryboard.loading) || e.inFont {
		return nil
	}
	key := filename + "|" + strconv.Itoa(int(height))
	if f := e.fonts[key]; f != nil && (e.keep || edMotif.keep || edStoryboard.keep) {
		return f
	}
	e.inFont = true
	f, err := loadFnt(filename, height)
	e.inFont = false
	if err != nil {
		return nil
	}
	if e.fonts == nil {
		e.fonts = make(map[string]*Fnt)
	}
	e.fonts[key] = f
	return f
}

// editorFightSection runs in loadFightScreen for each section it reads, and with "" when it ends.
func editorFightSection(name, subname string) {
	if debuggerAttached() {
		edFight.section = strings.ToLower(name + subname)
	}
}

// editorLayoutSource runs in Layout.Read: what Layout.editor keeps, 0 for none.
func editorLayoutSource(pre string) int32 {
	e := &edFight
	if e.section == "" {
		return 0
	}
	return editorSourceIndex(editorFightSource{editorFileFight, e.section, strings.ToLower(pre), 0})
}

// editorLayoutDraw runs before a Layout draws (scl: as the Layout scales the def's positions),
// editorLayoutDrawn after.
func editorLayoutDraw(src int32, scl float32) {
	editorDrawBegin(src, scl*sys.widthScale, scl*sys.heightScale)
}

func editorLayoutDrawn() {
	edFight.cur = 0
}

// editorQuadRect: where initRenderSpriteQuad will put a sprite (x, y, w, h in the game picture's
// pixels), untiled and unrotated.
func editorQuadRect(rp *RenderParams) [4]float32 {
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
	return [4]float32{x, -y, w, h}
}

// editorRenderSprite runs in RenderSprite: the quad is added to the Layout being drawn.
func editorRenderSprite(rp *RenderParams) {
	e := &edFight
	if e.cur == 0 {
		return
	}
	d := &e.drawn[e.cur-1]
	r := editorQuadRect(rp)
	if w := rp.window; w != nil && *w != sys.scrrect {
		x0, y0 := Max(r[0], float32(w[0])), Max(r[1], float32(w[1]))
		x1, y1 := Min(r[0]+r[2], float32(w[0]+w[2])), Min(r[1]+r[3], float32(w[1]+w[3]))
		if x1 <= x0 || y1 <= y0 {
			return
		}
		r = [4]float32{x0, y0, x1 - x0, y1 - y0}
	}
	if d.n == 0 {
		d.rect = r
	} else {
		x0, y0 := Min(d.rect[0], r[0]), Min(d.rect[1], r[1])
		x1, y1 := Max(d.rect[0]+d.rect[2], r[0]+r[2]), Max(d.rect[1]+d.rect[3], r[1]+r[3])
		d.rect = [4]float32{x0, y0, x1 - x0, y1 - y0}
	}
	d.n++
}

// editorScreenGeometry adds the fight screen and the motif to the frame geometry and starts the
// next frame's list:
//
//	"fight": {"def", "items": [[section, prefix, x, y, w, h, kx, ky, bg, action, elem], ...]}: in drawing order,
//	  each Layout drawn (section and prefix of the def's keys it was read from, lowercase), its
//	  rectangle in the game picture's pixels and the pixels per unit of the def's positions, 0 (bg),
//	  the action of the def it shows ([Begin Action]) and its element (1-based), else -1, -1;
//	"motif": {"def", "items": [...]}: the same for the motif's animations and texts, and its BGs:
//	  [bgdef's name, "", x, y, w, h, kx, ky, 1 + the BG's index, action, elem] (kx, ky: per unit of its start);
//	"storyboard": {"def", "items": [...]}: the same for the storyboard playing (editor_storyboard.go).
func editorScreenGeometry(b []byte) []byte {
	editorMotifIndex()
	block := func(name, def string, file int8) {
		if def == "" {
			return
		}
		b = append(b, `,"`...)
		b = append(b, name...)
		b = append(b, `":{"def":`...)
		b = editorJSONString(b, def)
		b = append(b, `,"items":[`...)
		n := 0
		for _, d := range edFight.drawn {
			if d.n == 0 || d.src < 1 || int(d.src) > len(edFight.sources) || edFight.sources[d.src-1].file != file {
				continue
			}
			src := edFight.sources[d.src-1]
			if n > 0 {
				b = append(b, ',')
			}
			n++
			b = append(b, '[')
			b = editorJSONString(b, src.section)
			b = append(b, ',')
			b = editorJSONString(b, src.prefix)
			for _, v := range [...]float32{d.rect[0], d.rect[1], d.rect[2], d.rect[3], d.k[0], d.k[1]} {
				b = append(b, ',')
				b = strconv.AppendFloat(b, float64(v), 'f', 3, 32)
			}
			for _, v := range [...]int32{src.bg, d.action, d.elem} {
				b = append(b, ',')
				b = strconv.AppendInt(b, int64(v), 10)
			}
			b = append(b, ']')
		}
		b = append(b, "]}"...)
	}
	block("fight", sys.fightScreen.def, editorFileFight)
	block("motif", sys.motif.Def, editorFileMotif)
	if sys.storyboard.active {
		block("storyboard", sys.storyboard.Def, editorFileStoryboard)
	}
	edFight.drawn = edFight.drawn[:0]
	edFight.cur = 0
	return b
}
// editorReloadFight loads the fight screen's def again and puts it in place of the one in use;
// the match goes on, the round where it was. textFile, when not empty, is read instead of the
// def, then deleted: unsaved text, written next to it (its files are found from where it is).
// keep: the SFF, SND and fonts in use are kept, not read again.
func editorReloadFight(textFile string, keep bool) (err error) {
	old := &sys.fightScreen
	if old.def == "" {
		return fmt.Errorf("no fight screen")
	}
	defer func() {
		edFight.keep = false
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	from := old.def
	if textFile != "" {
		from = textFile
		defer os.Remove(textFile)
	}
	edFight.keep = keep
	nf, err := loadFightScreen(from)
	if err != nil {
		return err
	}
	nf.def = old.def
	// As FightScreen.reload keeps them
	nf.time.framespercount = old.time.framespercount
	nf.timer.active = old.timer.active
	nf.match.active = old.match.active
	for i := 0; i < 2; i++ {
		nf.scores[i].active = old.scores[i].active
		nf.aiLevels[i].active = old.aiLevels[i].active
		nf.winCounts[i].active = old.winCounts[i].active
		nf.winCounts[i].wins = old.winCounts[i].wins
	}
	nf.active, nf.bars, nf.mode = old.active, old.bars, old.mode
	nf.redlifebar, nf.guardbar, nf.stunbar = old.redlifebar, old.guardbar, old.stunbar
	// The layouts in use and the elements back to their start, then what goes on in the match
	nf.reset()
	for i := range nf.winIcons {
		nf.winIcons[i].wins = append([]WinType{}, old.winIcons[i].wins...)
		nf.winIcons[i].numWins = old.winIcons[i].numWins
	}
	for i := range nf.lifeBars {
		for j := range nf.lifeBars[i] {
			n, o := nf.lifeBars[i][j], old.lifeBars[i][j]
			if n != nil && o != nil {
				n.toplife, n.oldlife, n.midlife, n.midlifeMin, n.mlifetime = o.toplife, o.oldlife, o.midlife, o.midlifeMin, o.mlifetime
			}
		}
	}
	// The portraits come from the characters when the fight screen steps
	for i := range nf.faces {
		for j := range nf.faces[i] {
			n, o := nf.faces[i][j], old.faces[i][j]
			if n != nil && o != nil {
				n.face, n.old_spr, n.old_pal, n.numko = o.face, o.old_spr, o.old_pal, o.numko
				n.teammate_face, n.teammate_scale, n.teammate_face_pfx = o.teammate_face, o.teammate_scale, o.teammate_face_pfx
			}
		}
	}
	nr, or := nf.round, old.round
	nr.fadeIn, nr.fadeOut, nr.shutterTimer, nr.timerActive = or.fadeIn, or.fadeOut, or.shutterTimer, or.timerActive
	nr.roundDisplayTimer, nr.roundDisplayPhase = or.roundDisplayTimer, or.roundDisplayPhase
	nr.fightDisplayTimer, nr.fightDisplayPhase = or.fightDisplayTimer, or.fightDisplayPhase
	nr.koDisplayTimer, nr.koDisplayPhase = or.koDisplayTimer, or.koDisplayPhase
	nr.winDisplayTimer, nr.winDisplayPhase = or.winDisplayTimer, or.winDisplayPhase
	nr.outroStarted, nr.outroFrameAcc = or.outroStarted, or.outroFrameAcc
	// An animation's sprite is set when it steps: a paused game would draw none
	editorEachAnimation(reflect.ValueOf(nf), map[editorSeen]bool{}, (*Animation).UpdateSprite)
	// and go on from where they were
	editorKeepPlayback(reflect.ValueOf(old), reflect.ValueOf(nf), map[editorSeen]bool{})
	sys.fightScreen = *nf
	return nil
}

type editorSeen struct {
	t reflect.Type
	p uintptr
}

var editorNotWalked = map[reflect.Type]bool{
	reflect.TypeOf((*Sff)(nil)): true, reflect.TypeOf((*Snd)(nil)): true,
	reflect.TypeOf((*Fnt)(nil)): true, reflect.TypeOf((*Char)(nil)): true,
	reflect.TypeOf((*Model)(nil)): true,
}

// editorEachAnimation calls f once for each Animation that v leads to, not through files (SFF,
// SND, fonts) or characters.
func editorEachAnimation(v reflect.Value, seen map[editorSeen]bool, f func(*Animation)) {
	switch v.Kind() {
	case reflect.Ptr:
		if v.IsNil() || editorNotWalked[v.Type()] || seen[editorSeen{v.Type(), v.Pointer()}] ||
			(v.Type().Elem().PkgPath() != "" && v.Type().Elem().PkgPath() != editorMotifPkg) {
			return
		}
		seen[editorSeen{v.Type(), v.Pointer()}] = true
		if v.Type() == reflect.TypeOf((*Animation)(nil)) {
			f((*Animation)(unsafe.Pointer(v.Pointer())))
			return
		}
		editorEachAnimation(v.Elem(), seen, f)
	case reflect.Struct:
		if v.Type() == reflect.TypeOf(Animation{}) {
			if v.CanAddr() {
				f((*Animation)(unsafe.Pointer(v.UnsafeAddr())))
			}
			return
		}
		for i := 0; i < v.NumField(); i++ {
			editorEachAnimation(v.Field(i), seen, f)
		}
	case reflect.Array, reflect.Slice:
		for i := 0; i < v.Len(); i++ {
			editorEachAnimation(v.Index(i), seen, f)
		}
	case reflect.Map:
		for it := v.MapRange(); it.Next(); {
			editorEachAnimation(it.Value(), seen, f)
		}
	case reflect.Interface:
		editorEachAnimation(v.Elem(), seen, f)
	}
}

// editorKeepPlayback walks an old and a new copy of the same structure together: each new
// animation goes on from the element and time of the old one (a reload does not restart them).
func editorKeepPlayback(ov, nv reflect.Value, seen map[editorSeen]bool) {
	if !ov.IsValid() || !nv.IsValid() || ov.Type() != nv.Type() {
		return
	}
	switch nv.Kind() {
	case reflect.Ptr:
		if ov.IsNil() || nv.IsNil() || editorNotWalked[nv.Type()] || seen[editorSeen{nv.Type(), nv.Pointer()}] ||
			(nv.Type().Elem().PkgPath() != "" && nv.Type().Elem().PkgPath() != editorMotifPkg) {
			return
		}
		seen[editorSeen{nv.Type(), nv.Pointer()}] = true
		if nv.Type() == reflect.TypeOf((*Animation)(nil)) {
			o, n := (*Animation)(unsafe.Pointer(ov.Pointer())), (*Animation)(unsafe.Pointer(nv.Pointer()))
			if len(o.frames) > 0 && len(n.frames) > 0 {
				n.SetAnimElem(o.curelem+1, o.curelemtime)
			}
			return
		}
		editorKeepPlayback(ov.Elem(), nv.Elem(), seen)
	case reflect.Struct:
		for i := 0; i < nv.NumField(); i++ {
			editorKeepPlayback(ov.Field(i), nv.Field(i), seen)
		}
	case reflect.Array, reflect.Slice:
		for i := 0; i < nv.Len() && i < ov.Len(); i++ {
			editorKeepPlayback(ov.Index(i), nv.Index(i), seen)
		}
	case reflect.Map:
		for it := nv.MapRange(); it.Next(); {
			editorKeepPlayback(ov.MapIndex(it.Key()), it.Value(), seen)
		}
	case reflect.Interface:
		editorKeepPlayback(ov.Elem(), nv.Elem(), seen)
	}
}

func editorFightFunctions(l *lua.LState) {
	luaRegister(l, "debugReloadFight", func(l *lua.LState) int {
		/*Load the fight screen's def file again and use it at once; the match goes on.
		@function debugReloadFight
		@tparam[opt] string textFile Read this file instead, then delete it (unsaved text, in the def's folder).
		@tparam[opt] boolean keep Keep the SFF, SND and fonts in use (after an edit of the text only).
		@treturn boolean ok
		@treturn string error*/
		var err error
		if !debuggerAttached() {
			err = fmt.Errorf("needs -ikdbg")
		} else {
			textFile := ""
			if !nilArg(l, 1) {
				textFile = strArg(l, 1)
			}
			err = editorReloadFight(textFile, !nilArg(l, 2) && boolArg(l, 2))
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
