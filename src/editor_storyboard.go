package main

import (
	"fmt"
	"os"
	"reflect"
	"strconv"
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
	// A scene deleted under it: the last one, not the storyboard's end
	if ns.currentSceneIndex >= len(ns.sceneKeys) && len(ns.sceneKeys) > 0 {
		ns.currentSceneIndex = len(ns.sceneKeys) - 1
	}
	current := ""
	if i := ns.currentSceneIndex; i >= 0 && i < len(ns.sceneKeys) {
		current = ns.sceneKeys[i]
	}
	for key, scene := range ns.Scene {
		oscene := old.Scene[key]
		if oscene == nil {
			continue
		}
		for lk, lp := range scene.Layer {
			if lp.TextSpriteData == nil || lp.Text == "" {
				continue
			}
			// Text already typed stays typed
			if ol := oscene.Layer[lk]; ol != nil && ol.typedLen > 0 {
				lp.typedLen, lp.charDelayCounter, lp.lineFullyRendered = ol.typedLen, ol.charDelayCounter, ol.lineFullyRendered
				if n := utf8.RuneCountInString(lp.Text); lp.typedLen > n {
					lp.typedLen = n
				}
				lp.TextSpriteData.wrapText(lp.Text, lp.typedLen)
			} else if key == current {
				// A new one is typed up to now: a paused storyboard would show none of it
				for t := Max(lp.StartTime-1, 0); t < ns.counter; t++ {
					editorTypeLayer(lp)
				}
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

// editorTypeLayer: a text layer's typewriter one tick on, as Storyboard.step does it.
func editorTypeLayer(lp *LayerProperties) {
	if lp.lineFullyRendered {
		return
	}
	StepTypewriter(lp.Text, &lp.typedLen, &lp.charDelayCounter, &lp.lineFullyRendered, lp.TextDelay)
	if n := utf8.RuneCountInString(lp.Text); lp.typedLen >= n {
		lp.typedLen, lp.lineFullyRendered = n, true
	}
	lp.TextSpriteData.wrapText(lp.Text, lp.typedLen)
}

// editorStoryboardSeek puts the storyboard playing at a time of a scene: its layers and BG stepped
// there, without its sounds, music or fades, which belong to playing it.
func editorStoryboardSeek(index int, tick int32) error {
	s := &sys.storyboard
	if !s.active {
		return fmt.Errorf("no storyboard playing")
	}
	if index < 0 || index >= len(s.sceneKeys) {
		return fmt.Errorf("no scene %d", index)
	}
	// The layers as a scene starts them (reset would stop the music)
	stop := s.SceneDef.StopMusic
	s.SceneDef.StopMusic = false
	s.reset()
	s.SceneDef.StopMusic = stop
	s.active, s.initialized = true, true
	s.currentSceneIndex, s.counter, s.endTimer = index, 0, -1
	scene := s.Scene[s.sceneKeys[index]]
	s.buildDialogueQueue(scene)
	fadeIn, fadeOut := s.fades()
	fadeIn.reset()
	fadeOut.reset()
	for t := int32(0); t < tick; t++ {
		if scene.Bg.Name != "" {
			scene.Bg.BGDef.step()
		}
		for _, lp := range scene.Layer {
			if t >= lp.StartTime && (t < lp.EndTime || lp.EndTime <= 0) && lp.AnimData != nil {
				lp.AnimData.Update(true)
			}
			if lp.TextSpriteData == nil || lp.Text == "" {
				continue
			}
			if next := t + 1; next >= lp.StartTime && (next < lp.EndTime || lp.EndTime <= 0) {
				editorTypeLayer(lp)
			}
		}
		s.counter++
	}
	s.syncDialoguePosToTime()
	// Animations get their sprite when they step: a paused storyboard would draw none
	editorEachAnimation(reflect.ValueOf(s), map[editorSeen]bool{}, (*Animation).UpdateSprite)
	return nil
}

// editorStoryboardState: the frame geometry's "storyState": the scene, the time in it, paused.
func editorStoryboardState(b []byte) []byte {
	s := &sys.storyboard
	if !s.active {
		return b
	}
	b = append(b, `,"storyState":[`...)
	b = strconv.AppendInt(b, int64(s.currentSceneIndex), 10)
	b = append(b, ',')
	b = strconv.AppendInt(b, int64(s.counter), 10)
	b = append(b, ',')
	b = strconv.AppendBool(b, sys.paused)
	return append(b, ']')
}

func editorStoryboardFunctions(l *lua.LState) {
	luaRegister(l, "debugStoryboardSeek", func(l *lua.LState) int {
		/*Put the storyboard playing at a time of one of its scenes.
		@function debugStoryboardSeek
		@tparam int scene The scene's index, from 0.
		@tparam int tick The time in that scene.
		@treturn boolean ok
		@treturn string error*/
		err := fmt.Errorf("needs -ikdbg")
		if debuggerAttached() {
			err = editorStoryboardSeek(int(numArg(l, 1)), int32(numArg(l, 2)))
		}
		if err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
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
