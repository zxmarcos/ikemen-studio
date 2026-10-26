package main

import (
	"encoding/json"
	"fmt"

	"github.com/qmuntal/gltf"
	lua "github.com/yuin/gopher-lua"
)

// The stage's 3D model for an external debugger: its animations, nodes and lights, read, turned
// on and off, changed while the match goes on. The engine does not keep the glTF names:
// loadglTFModel hands its document to editorModelLoaded, which keeps them by file.

type editorModelNames struct {
	nodes, meshes, lights []string
}

var edModelNames = map[string]*editorModelNames{}

func editorModelLoaded(filename string, doc *gltf.Document) {
	if !debuggerAttached() {
		return
	}
	n := &editorModelNames{}
	for _, node := range doc.Nodes {
		n.nodes = append(n.nodes, node.Name)
	}
	for _, m := range doc.Meshes {
		n.meshes = append(n.meshes, m.Name)
	}
	if ext, ok := doc.Extensions["KHR_lights_punctual"]; ok {
		var v struct {
			Lights []struct {
				Name string `json:"name"`
			} `json:"lights"`
		}
		if raw, ok := ext.(json.RawMessage); ok && json.Unmarshal(raw, &v) == nil {
			for _, l := range v.Lights {
				n.lights = append(n.lights, l.Name)
			}
		}
	}
	edModelNames[filename] = n
}

// The stage's model, and the names of the file it came from (empty ones when not known)
func editorStageModelNow() (*Model, *editorModelNames) {
	s := sys.stage
	if s == nil || s.model == nil {
		return nil, nil
	}
	names := edModelNames[edStage.modelFile]
	if names == nil {
		names = &editorModelNames{}
	}
	return s.model, names
}

func editorModelFunctions(l *lua.LState) {
	name := func(names []string, i int) lua.LString {
		if i < len(names) {
			return lua.LString(names[i])
		}
		return ""
	}
	array := func() *lua.LTable {
		t := l.NewTable()
		t.RawSetString("_array", lua.LTrue)
		return t
	}
	luaRegister(l, "debugModel", func(l *lua.LState) int {
		/*The stage's 3D model; nil without one.
		@function debugModel
		@treturn table {file, anims = array of {name, id, enabled, time, duration, loop, loopCount},
		nodes = array of {name, visible, mesh (its name, "" without one), light (index, -1), parent (-1: a root), layer},
		lights = array of {name, type (0 directional, 1 point, 2 spot), r, g, b, intensity, range, animated}};
		indices are 0-based.*/
		m, names := editorStageModelNow()
		if m == nil || !debuggerAttached() {
			l.Push(lua.LNil)
			return 1
		}
		t := l.NewTable()
		t.RawSetString("file", lua.LString(edStage.modelFile))
		anims := array()
		for _, a := range m.animations {
			at := l.NewTable()
			at.RawSetString("name", lua.LString(a.name))
			at.RawSetString("id", lua.LNumber(a.id))
			at.RawSetString("enabled", lua.LBool(a.enabled))
			at.RawSetString("time", lua.LNumber(a.time))
			at.RawSetString("duration", lua.LNumber(a.duration))
			at.RawSetString("loop", lua.LNumber(a.loop))
			at.RawSetString("loopCount", lua.LNumber(a.loopCount))
			anims.Append(at)
		}
		t.RawSetString("anims", anims)
		parent := make([]int, len(m.nodes))
		for i := range parent {
			parent[i] = -1
		}
		for i, n := range m.nodes {
			for _, c := range n.childrenIndex {
				if int(c) < len(parent) {
					parent[c] = i
				}
			}
		}
		nodes := array()
		for i, n := range m.nodes {
			nt := l.NewTable()
			nt.RawSetString("name", name(names.nodes, i))
			nt.RawSetString("visible", lua.LBool(n.visible))
			mesh, light, layer := lua.LString(""), -1, 0
			if n.meshIndex != nil {
				mesh = name(names.meshes, int(*n.meshIndex))
				if mesh == "" {
					mesh = lua.LString(fmt.Sprintf("mesh %d", *n.meshIndex))
				}
			}
			if n.lightIndex != nil {
				light = int(*n.lightIndex)
			}
			if n.layerNumber != nil {
				layer = *n.layerNumber
			}
			nt.RawSetString("mesh", mesh)
			nt.RawSetString("light", lua.LNumber(light))
			nt.RawSetString("parent", lua.LNumber(parent[i]))
			nt.RawSetString("layer", lua.LNumber(layer))
			nodes.Append(nt)
		}
		t.RawSetString("nodes", nodes)
		lights := array()
		for i := range m.lights {
			lg := &m.lights[i]
			lt := l.NewTable()
			lt.RawSetString("name", name(names.lights, i))
			lt.RawSetString("type", lua.LNumber(lg.lightType))
			c, _ := lg.color.getValue().([3]float32)
			lt.RawSetString("r", lua.LNumber(c[0]))
			lt.RawSetString("g", lua.LNumber(c[1]))
			lt.RawSetString("b", lua.LNumber(c[2]))
			in, _ := lg.intensity.getValue().(float32)
			rg, _ := lg.lightRange.getValue().(float32)
			lt.RawSetString("intensity", lua.LNumber(in))
			lt.RawSetString("range", lua.LNumber(rg))
			lt.RawSetString("animated", lua.LBool(lg.color.isAnimated || lg.intensity.isAnimated || lg.lightRange.isAnimated))
			lights.Append(lt)
		}
		t.RawSetString("lights", lights)
		l.Push(t)
		return 1
	})
	luaRegister(l, "debugModelAnim", func(l *lua.LState) int {
		/*Turn an animation of the stage's model on (from its start) or off (the nodes back at rest).
		@function debugModelAnim
		@tparam int32 index 0-based.
		@tparam boolean enabled*/
		m, _ := editorStageModelNow()
		if i := int(numArg(l, 1)); m != nil && debuggerAttached() && i >= 0 && i < len(m.animations) {
			m.animations[i].toggle(boolArg(l, 2))
		}
		return 0
	})
	luaRegister(l, "debugModelNode", func(l *lua.LState) int {
		/*Show or hide a node of the stage's model (its mesh), until the round starts again.
		@function debugModelNode
		@tparam int32 index 0-based.
		@tparam boolean visible
		@tparam[opt] boolean children Its children too, and theirs.*/
		m, _ := editorStageModelNow()
		if m == nil || !debuggerAttached() {
			return 0
		}
		visible, children := boolArg(l, 2), !nilArg(l, 3) && boolArg(l, 3)
		var set func(i int)
		set = func(i int) {
			if i < 0 || i >= len(m.nodes) {
				return
			}
			m.nodes[i].visible = visible
			if children {
				for _, c := range m.nodes[i].childrenIndex {
					set(int(c))
				}
			}
		}
		set(int(numArg(l, 1)))
		return 0
	})
	luaRegister(l, "debugModelLight", func(l *lua.LState) int {
		/*Change a light of the stage's model (an animation of it still wins).
		@function debugModelLight
		@tparam int32 index 0-based.
		@tparam float32 r
		@tparam float32 g
		@tparam float32 b
		@tparam float32 intensity
		@tparam[opt] float32 range*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.lights) {
			return 0
		}
		lg := &m.lights[i]
		lg.color.restAt([3]float32{float32(numArg(l, 2)), float32(numArg(l, 3)), float32(numArg(l, 4))})
		lg.intensity.restAt(float32(numArg(l, 5)))
		if !nilArg(l, 6) {
			lg.lightRange.restAt(float32(numArg(l, 6)))
		}
		return 0
	})
}
