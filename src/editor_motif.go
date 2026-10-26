package main

import (
	"fmt"
	"os"
	"reflect"
	"strings"
	"unsafe"

	lua "github.com/yuin/gopher-lua"
)

// Motif editing for an external editor (with "-ikdbg"): which section and prefix of the motif's
// def (system.def) each of its animations and texts comes from, where they were drawn in the
// last frame (recorded with the fight screen's, editor_fight.go), and reloading the motif in
// place: the objects and tables the scripts hold stay the same, with the new values in them.

type EditorMotif struct {
	index editorDefIndex
	// Each *Anim and *TextSprite as the def made it (the struct's value): what the scripts
	// changed in them since is kept when the motif is reloaded (unless the def changed it too)
	pristine map[interface{}]interface{}
	keep     bool // reloading after an edit of the text: the files in use are borrowed
	loading  bool // in loadMotif, for the editor: its fonts are kept
}

var edMotif = EditorMotif{index: editorDefIndex{file: editorFileMotif}}

var editorMotifPkg = reflect.TypeOf(Motif{}).PkgPath()

// The sources of a def read into a struct by its fields' ini tags (a motif, a storyboard).
type editorDefIndex struct {
	file   int8
	forSff *Sff // the struct whose animations and texts know their source (its SFF)
	dirty  bool
	bgdefs map[*BGDef]string // its BGDefs, by the name of their BGs' sections ("titlebg")
}

// update runs before each frame's geometry is written: when the struct changed, its animations
// and texts are given the source of the def's keys they come from (Anim.editor,
// TextSprite.editor, which their copies keep): the struct fields' ini tags make the path
// ("title_info.menu.item"), as PopulateDataPointers reads them, except that maps keep their
// field's path ("...menu.item.bg.arcade"). pristine: remember them as the def made them.
func (x *editorDefIndex) update(root reflect.Value, sff *Sff, pristine bool) {
	if !x.dirty && x.forSff == sff {
		return
	}
	x.forSff, x.dirty = sff, false
	x.bgdefs = make(map[*BGDef]string)
	var walk func(v reflect.Value, path string)
	walk = func(v reflect.Value, path string) {
		switch v.Kind() {
		case reflect.Ptr:
			if v.IsNil() {
				return
			}
			switch p := v.Interface().(type) {
			case *Anim:
				if p.editor == 0 {
					p.editor = editorDefSource(x.file, path)
				}
				if pristine {
					editorPristine(p, *p)
				}
				return
			case *TextSprite:
				if p.editor == 0 {
					p.editor = editorDefSource(x.file, path)
				}
				if pristine {
					editorPristine(p, *p)
				}
				return
			case *BGDef:
				if _, ok := x.bgdefs[p]; !ok {
					x.bgdefs[p] = strings.TrimSuffix(path[strings.LastIndexByte(path, '.')+1:], "def")
				}
				return
			case *Sff, *Snd, *Fnt, *Model:
				return
			}
			if v.Elem().Kind() == reflect.Struct && v.Type().Elem().PkgPath() == editorMotifPkg {
				walk(v.Elem(), path)
			}
		case reflect.Struct:
			// A storyboard scene's bg: the BGDef named by its name
			if bf, nf := v.FieldByName("BGDef"), v.FieldByName("Name"); bf.IsValid() && nf.IsValid() &&
				nf.Kind() == reflect.String && nf.String() != "" {
				if p, ok := bf.Interface().(*BGDef); ok && p != nil {
					x.bgdefs[p] = strings.ToLower(nf.String())
				}
			}
			t := v.Type()
			for i := 0; i < t.NumField(); i++ {
				f := t.Field(i)
				if f.PkgPath != "" {
					continue
				}
				p := path
				if tag := strings.TrimSpace(f.Tag.Get("ini")); tag != "" && !strings.HasPrefix(strings.ToLower(tag), "map:") {
					p = editorJoinPath(path, tag)
				}
				walk(v.Field(i), p)
			}
		case reflect.Map:
			if v.Type().Key().Kind() != reflect.String {
				return
			}
			for it := v.MapRange(); it.Next(); {
				walk(it.Value(), editorJoinPath(path, it.Key().String()))
			}
		case reflect.Slice, reflect.Array:
			for i := 0; i < v.Len(); i++ {
				walk(v.Index(i), path)
			}
		}
	}
	walk(root, "")
}

// editorMotifIndex runs before each frame's geometry is written (editorDefIndex.update).
func editorMotifIndex() {
	edMotif.index.update(reflect.ValueOf(&sys.motif).Elem(), sys.motif.Sff, true)
	edStoryboard.index.update(reflect.ValueOf(&sys.storyboard).Elem(), sys.storyboard.Sff, false)
}

func editorJoinPath(base, elem string) string {
	elem = strings.ToLower(strings.ReplaceAll(strings.TrimSpace(elem), " ", "_"))
	if elem == "" {
		return base
	}
	if base == "" {
		return elem
	}
	return base + "." + elem
}

// editorDefSource: the source of a path, its first part the section ("title_info" is
// [Title Info]), the rest the prefix of the keys.
func editorDefSource(file int8, path string) int32 {
	if path == "" {
		return 0
	}
	section, prefix := path, ""
	if dot := strings.IndexByte(path, '.'); dot >= 0 {
		section, prefix = path[:dot], path[dot+1:]+"."
	}
	return editorSourceIndex(editorFightSource{file, strings.ReplaceAll(section, "_", " "), prefix, 0})
}
// editorPristine remembers an object as the def made it, the first time it is seen.
func editorPristine(p, value interface{}) {
	m := &edMotif
	if m.pristine == nil {
		m.pristine = make(map[interface{}]interface{})
	}
	if _, ok := m.pristine[p]; !ok {
		m.pristine[p] = value
	}
}

// editorField: a struct's field, settable even when unexported (v addressable).
func editorField(v reflect.Value, i int) reflect.Value {
	f := v.Field(i)
	return reflect.NewAt(f.Type(), unsafe.Pointer(f.UnsafeAddr())).Elem()
}

// editorMergeObject puts nv's values into ov (both addressable structs), but for the fields the
// scripts changed since the def made ov (it differs from pristine) that the new def leaves as
// they were (nv's is pristine's): those keep ov's, as a clipping window set once by a menu.
func editorMergeObject(ov, pristine, nv reflect.Value) {
	pv := reflect.New(nv.Type()).Elem()
	pv.Set(pristine)
	for i := 0; i < nv.NumField(); i++ {
		o, p, n := editorField(ov, i), editorField(pv, i), editorField(nv, i)
		if reflect.DeepEqual(o.Interface(), p.Interface()) || !reflect.DeepEqual(n.Interface(), p.Interface()) {
			o.Set(n)
		}
	}
}

// editorBgDefDraw runs before BGDef.draw draws a BG, editorLayoutDrawn after: a BG of the
// motif's is recorded with its elements, as the n-th section named after its BGDef.
func editorBgDefDraw(s *BGDef, b *backGround) {
	edFight.cur = 0
	file := editorFileMotif
	name, ok := edMotif.index.bgdefs[s]
	if !ok {
		file = editorFileStoryboard
		name, ok = edStoryboard.index.bgdefs[s]
	}
	if !ok || edFrame.mem == nil {
		return
	}
	for i, bg := range s.bg {
		if bg == b {
			src := editorSourceIndex(editorFightSource{file, name, "", int32(i + 1)})
			editorDrawBegin(src, s.scale[0]*s.localscl*sys.widthScale, s.scale[1]*s.localscl*sys.heightScale)
			return
		}
	}
}

// The objects the scripts hold (userdata in the motif table): reloading copies the new values
// into them.
var editorMotifKept = map[reflect.Type]bool{
	reflect.TypeOf((*Anim)(nil)): true, reflect.TypeOf((*TextSprite)(nil)): true,
	reflect.TypeOf((*Rect)(nil)): true, reflect.TypeOf((*PalFX)(nil)): true,
	reflect.TypeOf((*Fade)(nil)): true, reflect.TypeOf((*BGDef)(nil)): true,
}

// editorMotifKeep walks the old and the new motif together: each kept object of the new one
// is copied into the old one's, which takes its place. True when nv should be set to ov
// (the caller sets it: a field, or a map's value).
func editorMotifKeep(ov, nv reflect.Value) bool {
	if ov.Type() != nv.Type() {
		return false
	}
	switch nv.Kind() {
	case reflect.Ptr:
		if ov.IsNil() || nv.IsNil() || ov.Pointer() == nv.Pointer() {
			return false
		}
		if editorMotifKept[nv.Type()] {
			key := ov.Interface()
			if pr, ok := edMotif.pristine[key]; ok && reflect.TypeOf(pr) == nv.Type().Elem() {
				edMotif.pristine[key] = nv.Elem().Interface()
				editorMergeObject(ov.Elem(), reflect.ValueOf(pr), nv.Elem())
			} else {
				ov.Elem().Set(nv.Elem())
			}
			return true
		}
		if nv.Elem().Kind() == reflect.Struct && nv.Type().Elem().PkgPath() == editorMotifPkg &&
			nv.Type() != reflect.TypeOf((*Sff)(nil)) && nv.Type() != reflect.TypeOf((*Snd)(nil)) {
			editorMotifKeep(ov.Elem(), nv.Elem())
		}
	case reflect.Struct:
		t := nv.Type()
		for i := 0; i < t.NumField(); i++ {
			if t.Field(i).PkgPath != "" {
				continue
			}
			if f := nv.Field(i); editorMotifKeep(ov.Field(i), f) && f.CanSet() {
				f.Set(ov.Field(i))
			}
		}
	case reflect.Map:
		if ov.IsNil() || nv.IsNil() {
			return false
		}
		for it := nv.MapRange(); it.Next(); {
			o := ov.MapIndex(it.Key())
			if o.IsValid() && editorMotifKeep(o, it.Value()) {
				nv.SetMapIndex(it.Key(), o)
			}
		}
	case reflect.Array, reflect.Slice:
		for i := 0; i < nv.Len() && i < ov.Len(); i++ {
			if f := nv.Index(i); editorMotifKeep(ov.Index(i), f) && f.CanSet() {
				f.Set(ov.Index(i))
			}
		}
	}
	return false
}

// editorMergeLua puts the values of a new motif table into the one the scripts hold: tables are
// merged, the userdata kept (the same objects, with the new values copied in), keys the scripts
// added stay.
func editorMergeLua(old, nt *lua.LTable) {
	nt.ForEach(func(k, v lua.LValue) {
		ov := old.RawGet(k)
		switch nv := v.(type) {
		case *lua.LTable:
			if ot, ok := ov.(*lua.LTable); ok {
				editorMergeLua(ot, nv)
				return
			}
		case *lua.LUserData:
			if _, ok := ov.(*lua.LUserData); ok {
				return
			}
		}
		old.RawSet(k, v)
	})
}

// editorReloadMotif loads the motif's def again through the scripts' loadMotif, then puts the new
// values in what the scripts and the engine already hold. textFile, when not empty, is read
// instead of the def, then deleted (unsaved text, next to it). keep: the SFF, SND and fonts in
// use are kept.
func editorReloadMotif(l *lua.LState, textFile string, keep bool) (err error) {
	old := sys.motif
	if old.Def == "" {
		return fmt.Errorf("no motif")
	}
	table, ok := l.GetGlobal("motif").(*lua.LTable)
	if !ok {
		return fmt.Errorf("the scripts have no motif table")
	}
	defer func() {
		edMotif.keep, edMotif.loading = false, false
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	from := old.Def
	if textFile != "" {
		from = textFile
		defer os.Remove(textFile)
	}
	edMotif.keep, edMotif.loading = keep, true
	if err := l.CallByParam(lua.P{Fn: l.GetGlobal("loadMotif"), NRet: 1, Protect: true}, lua.LString(from)); err != nil {
		return err
	}
	nt, _ := l.Get(-1).(*lua.LTable)
	l.Pop(1)
	edMotif.keep, edMotif.loading = false, false
	nm := &sys.motif
	nm.Def = old.Def
	// What goes on (screens, menus, fades) stays as it was
	nm.ch, nm.co, nm.de, nm.di, nm.vi, nm.wi, nm.hi, nm.me = old.ch, old.co, old.de, old.di, old.vi, old.wi, old.hi, old.me
	nm.fadeIn, nm.fadeOut, nm.fadePolicy = old.fadeIn, old.fadeOut, old.fadePolicy
	editorMotifKeep(reflect.ValueOf(&old).Elem(), reflect.ValueOf(nm).Elem())
	if nt != nil {
		nt.RawSetString("def", lua.LString(old.Def))
		editorMergeLua(table, nt)
	}
	// Animations get their sprite when they step: a paused screen would draw none
	editorEachAnimation(reflect.ValueOf(nm), map[editorSeen]bool{}, (*Animation).UpdateSprite)
	edMotif.index.dirty = true
	return nil
}

func editorMotifFunctions(l *lua.LState) {
	luaRegister(l, "debugAnimSource", func(l *lua.LState) int {
		/*Give an animation the source (the def's keys) of another: a portrait made from a
		character's sprites by the scripts, with the motif property's AnimData.
		@function debugAnimSource
		@tparam Anim anim
		@tparam Anim from*/
		a, ok1 := toUserData(l, 1).(*Anim)
		from, ok2 := toUserData(l, 2).(*Anim)
		if ok1 && ok2 && a != nil && from != nil && debuggerAttached() {
			a.editor = from.editor
		}
		return 0
	})
	luaRegister(l, "debugReloadMotif", func(l *lua.LState) int {
		/*Load the motif's def file again (through loadMotif) and use it at once, in place.
		@function debugReloadMotif
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
			err = editorReloadMotif(l, textFile, !nilArg(l, 2) && boolArg(l, 2))
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
