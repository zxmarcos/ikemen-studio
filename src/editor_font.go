package main

import (
	"fmt"
	"os"
	"strconv"

	lua "github.com/yuin/gopher-lua"
)

// Font editing for an external editor (with "-ikdbg"): every font loaded is recorded with its
// file, the texts drawn say their font in the frame geometry, and a font's file is read again
// into the fonts in use (the texts show it at once).

type editorFontLoad struct {
	file   string
	height int32
}

type EditorFont struct {
	cache  map[string]*Fnt // the fonts fight screens, motifs and storyboards loaded, by file and height
	inFont bool
	of     map[*Fnt]editorFontLoad // every font loaded, and from what
	files  []string                // the files of those, each once, in the order first loaded
	index  map[string]int32        // 1 + a file's index in files
}

var edFont EditorFont

// editorKeptFont runs in loadFnt: every font loaded is recorded; the fonts of fight screens,
// motifs and storyboards are kept, and not read again while the editor reloads one after an
// edit of its text.
func editorKeptFont(filename string, height int32) *Fnt {
	e := &edFont
	if e.inFont || !debuggerAttached() {
		return nil
	}
	key := filename + "|" + strconv.Itoa(int(height))
	screen := edFight.section != "" || edMotif.loading || edStoryboard.loading
	if f := e.cache[key]; f != nil && screen && (edFight.keep || edMotif.keep || edStoryboard.keep) {
		return f
	}
	f, err := editorLoadFont(filename, height)
	if err != nil {
		return nil // loadFnt reads it and fails the same way
	}
	if screen {
		if e.cache == nil {
			e.cache = make(map[string]*Fnt)
		}
		e.cache[key] = f
	}
	if e.of == nil {
		e.of, e.index = make(map[*Fnt]editorFontLoad), make(map[string]int32)
	}
	e.of[f] = editorFontLoad{filename, height}
	if e.index[filename] == 0 {
		e.files = append(e.files, filename)
		e.index[filename] = int32(len(e.files))
	}
	return f
}

// editorLoadFont: loadFnt itself, not through editorKeptFont.
func editorLoadFont(filename string, height int32) (*Fnt, error) {
	edFont.inFont = true
	defer func() { edFont.inFont = false }()
	return loadFnt(filename, height)
}

// editorFontDrawn runs in Fnt.DrawText: the text being recorded (editorDrawBegin) says its font.
func editorFontDrawn(f *Fnt) {
	if e := &edFight; e.cur > 0 {
		e.drawn[e.cur-1].font = edFont.index[edFont.of[f].file]
	}
}

// editorReloadFont reads a font's file again into every font loaded from it. textFile, when
// not empty, is read instead, then deleted (unsaved text, next to the file).
func editorReloadFont(filename, textFile string) (err error) {
	if HasExtension(filename, ".fnt") {
		return fmt.Errorf("only .def fonts are reloaded")
	}
	from := filename
	if textFile != "" {
		from = textFile
		defer os.Remove(textFile)
	}
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	n := 0
	for f, l := range edFont.of {
		if l.file != filename {
			continue
		}
		nf, err := editorLoadFont(from, l.height)
		if err != nil {
			return err
		}
		*f = *nf
		n++
	}
	if n == 0 {
		return fmt.Errorf("no font loaded from %s", filename)
	}
	return nil
}

func editorFontFunctions(l *lua.LState) {
	luaRegister(l, "debugReloadFont", func(l *lua.LState) int {
		/*Read a font's def file again into every font loaded from it; texts show it at once.
		@function debugReloadFont
		@tparam string file The font's file, as it was loaded.
		@tparam[opt] string textFile Read this file instead, then delete it (unsaved text, in the def's folder).
		@treturn boolean ok
		@treturn string error*/
		var err error
		if !debuggerAttached() {
			err = fmt.Errorf("needs -ikdbg")
		} else {
			textFile := ""
			if !nilArg(l, 2) {
				textFile = strArg(l, 2)
			}
			err = editorReloadFont(strArg(l, 1), textFile)
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
