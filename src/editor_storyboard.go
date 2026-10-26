package main

import (
	"fmt"
	"os"
	"reflect"
	"unicode/utf8"

	lua "github.com/yuin/gopher-lua"
)

// Storyboard editing for an external editor (with "-ikdbg"): the storyboard playing (intro,
// ending, logo...) has its layers and BGs recorded where they are drawn, as the motif's
// (editor_motif.go), and is reloaded from its def with the scene going on.

type EditorStoryboard struct {
	index   editorDefIndex
	keep    bool // reloading after an edit of the text: the files in use are borrowed
	loading bool // in loadStoryboard, for the editor: its fonts are kept
}

var edStoryboard = EditorStoryboard{index: editorDefIndex{file: editorFileStoryboard}}

// editorReloadStoryboard loads the playing storyboard's def again and puts it in place, at the
// same scene and time. textFile, when not empty, is read instead of the def, then deleted
// (unsaved text, next to it). keep: the SFF, SND and fonts in use are kept.
func editorReloadStoryboard(textFile string, keep bool) (err error) {
	old := &sys.storyboard
	if old.Def == "" || !old.active {
		return fmt.Errorf("no storyboard playing")
	}
	defer func() {
		edStoryboard.keep, edStoryboard.loading = false, false
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	from := old.Def
	if textFile != "" {
		from = textFile
		defer os.Remove(textFile)
	}
	edStoryboard.keep, edStoryboard.loading = keep, true
	ns, err := loadStoryboard(from)
	edStoryboard.keep, edStoryboard.loading = false, false
	if err != nil {
		return err
	}
	ns.Def = old.Def
	// The layers as a scene starts them (reset would stop the music)
	stop := ns.SceneDef.StopMusic
	ns.SceneDef.StopMusic = false
	ns.reset()
	ns.SceneDef.StopMusic = stop
	// Where the storyboard is
	ns.active, ns.initialized, ns.counter, ns.endTimer = old.active, old.initialized, old.counter, old.endTimer
	ns.currentSceneIndex, ns.canceled, ns.musicPlaying, ns.fadePolicy = old.currentSceneIndex, old.canceled, old.musicPlaying, old.fadePolicy
	ns.netReady, ns.loadEnding, ns.inMatch = old.netReady, old.loadEnding, old.inMatch
	ns.id, ns.ownerID, ns.playerNo, ns.placement = old.id, old.ownerID, old.playerNo, old.placement
	ns.fadeIn, ns.fadeOut = old.fadeIn, old.fadeOut
	for key, scene := range ns.Scene {
		oscene := old.Scene[key]
		if oscene == nil {
			continue
		}
		if scene.Bg.BGDef != nil && oscene.Bg.BGDef != nil {
			scene.Bg.BGDef.time = oscene.Bg.BGDef.time
		}
		// Text already typed stays typed
		for lk, lp := range scene.Layer {
			if ol := oscene.Layer[lk]; ol != nil && lp.TextSpriteData != nil && ol.typedLen > 0 {
				lp.typedLen, lp.charDelayCounter, lp.lineFullyRendered = ol.typedLen, ol.charDelayCounter, ol.lineFullyRendered
				if n := utf8.RuneCountInString(lp.Text); lp.typedLen > n {
					lp.typedLen = n
				}
				lp.TextSpriteData.wrapText(lp.Text, lp.typedLen)
			}
		}
	}
	if i := ns.currentSceneIndex; i >= 0 && i < len(ns.sceneKeys) {
		ns.buildDialogueQueue(ns.Scene[ns.sceneKeys[i]])
		ns.syncDialoguePosToTime()
	}
	// Animations get their sprite when they step: a paused storyboard would draw none
	editorEachAnimation(reflect.ValueOf(ns), map[editorSeen]bool{}, (*Animation).UpdateSprite)
	editorKeepPlayback(reflect.ValueOf(old), reflect.ValueOf(ns), map[editorSeen]bool{})
	*old = *ns
	edStoryboard.index.dirty = true
	return nil
}

func editorStoryboardFunctions(l *lua.LState) {
	luaRegister(l, "debugReloadStoryboard", func(l *lua.LState) int {
		/*Load the playing storyboard's def file again and use it at once, at the same scene and time.
		@function debugReloadStoryboard
		@tparam[opt] string textFile Read this file instead, then delete it (unsaved text, in the def's folder).
		@tparam[opt] boolean keep Keep the SFF, SND and fonts in use (after an edit of the text only).
		@tparam[opt] boolean sprites Read the SFF again all the same (after an edit of the SFF).
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
			edFight.newSff = !nilArg(l, 3) && boolArg(l, 3)
			err = editorReloadStoryboard(textFile, !nilArg(l, 2) && boolArg(l, 2))
			edFight.newSff = false
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
