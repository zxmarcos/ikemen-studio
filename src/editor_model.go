package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"strconv"
	"time"

	mgl "github.com/go-gl/mathgl/mgl32"
	"github.com/qmuntal/gltf"
	"github.com/qmuntal/gltf/modeler"
	lua "github.com/yuin/gopher-lua"
)

// The stage's 3D model for an external debugger: its scenes, nodes, meshes, materials,
// animations and lights, read and changed while the match goes on, written back to its glTF
// file; the node selected and the one under the mouse outlined on the game (the frame
// geometry's "mdl"), a node picked on the game by its triangles. The engine does not keep the
// glTF names: loadglTFModel hands its document to editorModelLoaded, which keeps them by file.

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

// What the editor knows of the model: the matrices it was last drawn with, the nodes selected
// and under the mouse, and what was changed since its file was read (what Save writes).
type editorModelState struct {
	viewProj  mgl.Mat4
	drawn     bool
	sel       int
	hover     int
	picked    int // bumped when a node is picked on the game
	hit       *mgl.Vec3 // where the last pick met it, in the model's space
	trs       map[int]bool
	props     map[int]bool
	materials map[int]bool
	lights    map[int]bool
	samplers  map[[2]int]bool // animation, sampler: its keys
}

var edModel = editorModelState{sel: -1, hover: -1}

// editorModelRead runs when a model file is read (not when a reload keeps it): nothing changed.
func editorModelRead() {
	e := &edModel
	e.trs, e.props, e.materials, e.lights = map[int]bool{}, map[int]bool{}, map[int]bool{}, map[int]bool{}
	e.samplers = map[[2]int]bool{}
}

func (e *editorModelState) dirty() bool {
	return len(e.trs)+len(e.props)+len(e.materials)+len(e.lights)+len(e.samplers) > 0
}

// An animation put at a time (turning it on): Model.step moves every animation on by a tick, so
// only this one, from a tick before; the others keep where they are.
func editorModelAnimAt(m *Model, i int, t float32) {
	a := m.animations[i]
	t = Clamp(t, 0, Max(0, a.duration-1e-4))
	enabled := make([]bool, len(m.animations))
	for k, o := range m.animations {
		enabled[k] = o.enabled
		o.enabled = k == i
	}
	loop := a.loop
	a.time = Max(0, t-1.0/60)
	m.step()
	a.time, a.loop = t, loop
	for k, o := range m.animations {
		o.enabled = enabled[k] || k == i
	}
}

// A channel moving a node: 0 translation, 1 rotation, 2 scale; -1 another kind
func editorChannelPath(c *GLTFAnimationChannel) int {
	if c.nodeIndex == nil {
		return -1
	}
	switch c.targetType {
	case TRSTranslation:
		return 0
	case TRSRotation:
		return 1
	case TRSScale:
		return 2
	}
	return -1
}

// The values an accessor holds, flat
func editorAccessorFloats(doc *gltf.Document, i uint32) ([]float32, bool) {
	if int(i) >= len(doc.Accessors) {
		return nil, false
	}
	v, err := modeler.ReadAccessor(doc, doc.Accessors[i], nil)
	if err != nil {
		return nil, false
	}
	switch d := v.(type) {
	case []float32:
		return d, true
	case [][3]float32:
		out := make([]float32, 0, len(d)*3)
		for _, e := range d {
			out = append(out, e[:]...)
		}
		return out, true
	case [][4]float32:
		out := make([]float32, 0, len(d)*4)
		for _, e := range d {
			out = append(out, e[:]...)
		}
		return out, true
	}
	return nil, false
}

// An animation sampler's times or values written into the document: the accessor kept when it
// holds them already, written over when only this sampler uses it and its size is the same,
// else a new one at the end of the binary buffer. n: numbers per element (1, 3 or 4).
func editorWriteKeys(doc *gltf.Document, acc uint32, data []float32, n int, shared bool) (uint32, error) {
	if cur, ok := editorAccessorFloats(doc, acc); ok && len(cur) == len(data) {
		same := true
		for k := range cur {
			same = same && cur[k] == data[k]
		}
		if same {
			return acc, nil
		}
	}
	minMax := func(a *gltf.Accessor) {
		if n == 1 && len(data) > 0 {
			a.Min, a.Max = []float32{data[0]}, []float32{data[len(data)-1]}
		} else if a.Min != nil || a.Max != nil {
			a.Min, a.Max = nil, nil
		}
	}
	if int(acc) < len(doc.Accessors) && !shared {
		a := doc.Accessors[acc]
		if a.BufferView != nil && a.Sparse == nil && a.ComponentType == gltf.ComponentFloat &&
			int(a.Count)*n == len(data) && int(*a.BufferView) < len(doc.BufferViews) {
			bv := doc.BufferViews[*a.BufferView]
			stride := int(bv.ByteStride)
			if stride == 0 {
				stride = 4 * n
			}
			if int(bv.Buffer) < len(doc.Buffers) {
				buf := doc.Buffers[bv.Buffer].Data
				at := int(bv.ByteOffset + a.ByteOffset)
				if len(data) == 0 || at+(len(data)/n-1)*stride+4*n <= len(buf) {
					for e := 0; e < len(data)/n; e++ {
						for c := 0; c < n; c++ {
							binary.LittleEndian.PutUint32(buf[at+e*stride+4*c:], math.Float32bits(data[e*n+c]))
						}
					}
					minMax(a)
					return acc, nil
				}
			}
		}
	}
	if len(doc.Buffers) == 0 || doc.Buffers[len(doc.Buffers)-1].URI != "" && !doc.Buffers[len(doc.Buffers)-1].IsEmbeddedResource() {
		return acc, fmt.Errorf("its buffers are files of their own: animation keys are written into a .glb only")
	}
	var typed interface{}
	switch n {
	case 3:
		v := make([][3]float32, len(data)/3)
		for e := range v {
			copy(v[e][:], data[e*3:])
		}
		typed = v
	case 4:
		v := make([][4]float32, len(data)/4)
		for e := range v {
			copy(v[e][:], data[e*4:])
		}
		typed = v
	default:
		typed = append([]float32(nil), data...)
	}
	i := modeler.WriteAccessor(doc, gltf.TargetNone, typed)
	minMax(doc.Accessors[i])
	return i, nil
}

// editorModelDrawn runs in Stage.drawModel with the matrices the model is drawn with.
func editorModelDrawn(proj, view mgl.Mat4) {
	if !debuggerAttached() {
		return
	}
	edModel.viewProj = proj.Mul4(view)
	edModel.drawn = true
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
	if edModel.trs == nil {
		editorModelRead()
	}
	return s.model, names
}

// A clip space point to the game picture's pixels
func editorClipToPicture(c mgl.Vec4) [2]float32 {
	return [2]float32{(c[0]/c[3] + 1) / 2 * float32(sys.scrrect[2]), (1 - c[1]/c[3]) / 2 * float32(sys.scrrect[3])}
}

// A node's box (its mesh's, else its children's, in the world) as line segments in the game
// picture's pixels (x0, y0, x1, y1, ...), cut where they go behind the camera; a node with
// nothing under it gives where it is (point) instead.
func editorNodeBox(m *Model, i int) (lines []float32, point []float32) {
	if i < 0 || i >= len(m.nodes) || !edModel.drawn {
		return nil, nil
	}
	n := m.nodes[i]
	var corners [8]mgl.Vec4
	box := func(lo, hi [3]float32, mat mgl.Mat4) {
		for k := 0; k < 8; k++ {
			p := mgl.Vec4{lo[0], lo[1], lo[2], 1}
			if k&1 != 0 {
				p[0] = hi[0]
			}
			if k&2 != 0 {
				p[1] = hi[1]
			}
			if k&4 != 0 {
				p[2] = hi[2]
			}
			corners[k] = mat.Mul4x1(p)
		}
	}
	if lo, hi, ok := editorMeshBox(m, n); ok {
		box(lo, hi, edModel.viewProj.Mul4(n.worldTransform))
	} else {
		// The world box of the meshes under it
		lo := [3]float32{math.MaxFloat32, math.MaxFloat32, math.MaxFloat32}
		hi := [3]float32{-math.MaxFloat32, -math.MaxFloat32, -math.MaxFloat32}
		found := false
		var walk func(c *Node)
		walk = func(c *Node) {
			if l, h, ok := editorMeshBox(m, c); ok {
				box(l, h, c.worldTransform)
				for _, p := range corners {
					for a := 0; a < 3; a++ {
						lo[a], hi[a] = Min(lo[a], p[a]), Max(hi[a], p[a])
					}
				}
				found = true
			}
			for _, k := range c.childrenIndex {
				if int(k) < len(m.nodes) {
					walk(m.nodes[k])
				}
			}
		}
		walk(n)
		if !found {
			c := edModel.viewProj.Mul4x1(mgl.Vec4{n.worldTransform[12], n.worldTransform[13], n.worldTransform[14], 1})
			if c[3] <= 1e-4 {
				return nil, nil
			}
			p := editorClipToPicture(c)
			return nil, p[:]
		}
		box(lo, hi, edModel.viewProj)
	}
	const near = 1e-3
	for _, e := range [12][2]int{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}} {
		a, b := corners[e[0]], corners[e[1]]
		if a[3] < near && b[3] < near {
			continue
		}
		// The part in front of the camera
		if a[3] < near {
			a = a.Add(b.Sub(a).Mul((near - a[3]) / (b[3] - a[3])))
		} else if b[3] < near {
			b = b.Add(a.Sub(b).Mul((near - b[3]) / (a[3] - b[3])))
		}
		pa, pb := editorClipToPicture(a), editorClipToPicture(b)
		lines = append(lines, pa[0], pa[1], pb[0], pb[1])
	}
	return lines, nil
}

// The box of a node's own mesh, in its space
func editorMeshBox(m *Model, n *Node) (lo, hi [3]float32, ok bool) {
	if n.meshIndex == nil || int(*n.meshIndex) >= len(m.meshes) {
		return
	}
	lo = [3]float32{math.MaxFloat32, math.MaxFloat32, math.MaxFloat32}
	hi = [3]float32{-math.MaxFloat32, -math.MaxFloat32, -math.MaxFloat32}
	for _, p := range m.meshes[*n.meshIndex].primitives {
		for a := 0; a < 3; a++ {
			lo[a], hi[a] = Min(lo[a], p.boundingBox.min[a]), Max(hi[a], p.boundingBox.max[a])
		}
		ok = true
	}
	return
}

// The node whose triangles are the first under a point of the game picture (pixels), -1 none:
// visible nodes with a mesh, at rest (skins and morph targets are not followed).
func editorModelPick(m *Model, x, y float32) int {
	if !edModel.drawn || sys.scrrect[2] <= 0 || sys.scrrect[3] <= 0 {
		return -1
	}
	inv := edModel.viewProj.Inv()
	nx, ny := x/float32(sys.scrrect[2])*2-1, 1-y/float32(sys.scrrect[3])*2
	unproject := func(z float32) mgl.Vec4 {
		p := inv.Mul4x1(mgl.Vec4{nx, ny, z, 1})
		return p.Mul(1 / p[3])
	}
	// The segment from the near plane to the far one: its parameter is the same in every node's space
	p0, p1 := unproject(-1), unproject(1)
	inScene := map[int]bool{}
	var mark func(i uint32)
	mark = func(i uint32) {
		if int(i) < len(m.nodes) && !inScene[int(i)] {
			inScene[int(i)] = true
			for _, c := range m.nodes[i].childrenIndex {
				mark(c)
			}
		}
	}
	for _, s := range m.scenes {
		for _, i := range s.nodes {
			mark(i)
		}
	}
	best, found := float32(math.MaxFloat32), -1
	vertex := func(p *Primitive, k uint32) mgl.Vec3 {
		o := int(p.vertexBufferOffset) + 4*int(p.numVertices) + 12*int(k)
		if o+12 > len(m.vertexBuffer) {
			return mgl.Vec3{}
		}
		b := m.vertexBuffer[o:]
		return mgl.Vec3{math.Float32frombits(binary.LittleEndian.Uint32(b)),
			math.Float32frombits(binary.LittleEndian.Uint32(b[4:])), math.Float32frombits(binary.LittleEndian.Uint32(b[8:]))}
	}
	for i, n := range m.nodes {
		lo, hi, ok := editorMeshBox(m, n)
		if !ok || !n.visible || !inScene[i] {
			continue
		}
		wi := n.worldTransform.Inv()
		o4, e4 := wi.Mul4x1(p0), wi.Mul4x1(p1)
		o, d := o4.Vec3(), e4.Vec3().Sub(o4.Vec3())
		// The box first
		tmin, tmax := float32(0), best
		for a := 0; a < 3 && tmin <= tmax; a++ {
			if Abs(d[a]) < 1e-12 {
				if o[a] < lo[a] || o[a] > hi[a] {
					tmin = tmax + 1
				}
				continue
			}
			t0, t1 := (lo[a]-o[a])/d[a], (hi[a]-o[a])/d[a]
			if t0 > t1 {
				t0, t1 = t1, t0
			}
			tmin, tmax = Max(tmin, t0), Min(tmax, t1)
		}
		if tmin > tmax {
			continue
		}
		for _, p := range m.meshes[*n.meshIndex].primitives {
			if p.mode != TRIANGLES && p.mode != TRIANGLE_STRIP && p.mode != TRIANGLE_FAN {
				continue
			}
			first := int(p.elementBufferOffset / 4)
			if first+int(p.numIndices) > len(m.elementBuffer) {
				continue
			}
			idx := m.elementBuffer[first : first+int(p.numIndices)]
			count := len(idx) / 3
			if p.mode != TRIANGLES {
				count = len(idx) - 2
			}
			for t := 0; t < count; t++ {
				var a, b, c uint32
				switch p.mode {
				case TRIANGLES:
					a, b, c = idx[3*t], idx[3*t+1], idx[3*t+2]
				case TRIANGLE_STRIP:
					a, b, c = idx[t], idx[t+1], idx[t+2]
				default:
					a, b, c = idx[0], idx[t+1], idx[t+2]
				}
				// Möller-Trumbore, both faces
				va, vb, vc := vertex(p, a), vertex(p, b), vertex(p, c)
				e1, e2 := vb.Sub(va), vc.Sub(va)
				h := d.Cross(e2)
				det := e1.Dot(h)
				if Abs(det) < 1e-12 {
					continue
				}
				f := 1 / det
				s := o.Sub(va)
				u := f * s.Dot(h)
				if u < 0 || u > 1 {
					continue
				}
				q := s.Cross(e1)
				v := f * d.Dot(q)
				if v < 0 || u+v > 1 {
					continue
				}
				if hit := f * e2.Dot(q); hit >= 0 && hit < best {
					best, found = hit, i
				}
			}
		}
	}
	edModel.hit = nil
	if found >= 0 {
		hit := p0.Vec3().Add(p1.Vec3().Sub(p0.Vec3()).Mul(best))
		edModel.hit = &hit
	}
	return found
}

// The frame geometry's model: "mdl": {"sel", "hover": nodes (-1 none), "picked": bumped at each
// pick, "dirty": changed since its file was read, "sb", "hb": the selected and hovered nodes'
// boxes as segments [x0, y0, x1, y1, ...], or "sp", "hp": [x, y] where they are, in the game
// picture's pixels; "vp": the view-projection matrix, "w", "h": the picture's size; with a node
// selected, "pw": the world matrix of its parent, "nw": its own (column-major); "hit": where the
// last pick met the model, in its space}
func editorModelGeometry(b []byte) []byte {
	m, _ := editorStageModelNow()
	if m == nil {
		edModel.drawn = false
		return b
	}
	e := &edModel
	nums := func(key string, v []float32) {
		if v == nil {
			return
		}
		b = append(b, `,"`+key+`":[`...)
		for i, f := range v {
			if i > 0 {
				b = append(b, ',')
			}
			b = strconv.AppendFloat(b, float64(f), 'f', 1, 32)
		}
		b = append(b, ']')
	}
	b = append(b, `,"mdl":{"sel":`...)
	b = strconv.AppendInt(b, int64(e.sel), 10)
	b = append(b, `,"hover":`...)
	b = strconv.AppendInt(b, int64(e.hover), 10)
	b = append(b, `,"picked":`...)
	b = strconv.AppendInt(b, int64(e.picked), 10)
	b = append(b, `,"dirty":`...)
	b = strconv.AppendBool(b, e.dirty())
	l, p := editorNodeBox(m, e.sel)
	nums("sb", l)
	nums("sp", p)
	if e.hover != e.sel {
		l, p = editorNodeBox(m, e.hover)
		nums("hb", l)
		nums("hp", p)
	}
	exact := func(key string, v []float32) {
		b = append(b, `,"`+key+`":[`...)
		for i, f := range v {
			if i > 0 {
				b = append(b, ',')
			}
			b = strconv.AppendFloat(b, float64(f), 'g', -1, 32)
		}
		b = append(b, ']')
	}
	if e.hit != nil {
		exact("hit", e.hit[:])
	}
	if e.drawn {
		exact("vp", e.viewProj[:])
		b = append(b, `,"w":`...)
		b = strconv.AppendInt(b, int64(sys.scrrect[2]), 10)
		b = append(b, `,"h":`...)
		b = strconv.AppendInt(b, int64(sys.scrrect[3]), 10)
	}
	// What dragging the selected node needs: the matrices it is seen through
	if e.drawn && e.sel >= 0 && e.sel < len(m.nodes) {
		parent := mgl.Ident4()
		for _, n := range m.nodes {
			for _, c := range n.childrenIndex {
				if int(c) == e.sel {
					parent = n.worldTransform
				}
			}
		}
		exact("pw", parent[:])
		exact("nw", m.nodes[e.sel].worldTransform[:])
	}
	e.drawn = false
	return append(b, '}')
}

// editorModelSave writes what was changed (nodes' transforms and Ikemen extras, materials,
// lights) into the model's glTF file (glb or gltf; its buffers and images as they are), keeping
// the file as it was in a .bak the first time.
func editorModelSave() error {
	m, _ := editorStageModelNow()
	file := edStage.modelFile
	if m == nil || file == "" {
		return fmt.Errorf("the stage has no model")
	}
	data, err := os.ReadFile(file)
	if err != nil {
		return err
	}
	doc := new(gltf.Document)
	if err := gltf.NewDecoder(bytes.NewReader(data)).Decode(doc); err != nil {
		return fmt.Errorf("%v: %v", file, err)
	}
	if len(doc.Nodes) != len(m.nodes) || len(doc.Materials) != len(m.materials) {
		return fmt.Errorf("%v changed since it was read", file)
	}
	e := &edModel
	for i := range e.trs {
		n, mn := doc.Nodes[i], m.nodes[i]
		n.Translation, _ = mn.transition.restValue.([3]float32)
		n.Rotation, _ = mn.rotation.restValue.([4]float32)
		n.Scale, _ = mn.scale.restValue.([3]float32)
		n.Matrix = gltf.DefaultMatrix
	}
	for i := range e.props {
		n, mn := doc.Nodes[i], m.nodes[i]
		x, _ := n.Extras.(map[string]interface{})
		if x == nil {
			x = map[string]interface{}{}
		}
		set := func(key string, keep bool, v interface{}) {
			if keep {
				x[key] = v
			} else {
				delete(x, key)
			}
		}
		set("trans", mn.trans != TransNone, [...]string{"NONE", "ADD", "SUB", "MUL"}[mn.trans])
		set("layerNumber", mn.layerNumber != nil, func() interface{} {
			if mn.layerNumber != nil {
				return *mn.layerNumber
			}
			return nil
		}())
		set("castShadow", !mn.castShadow, "0")
		set("disableZTest", !mn.zTest, "1")
		set("disableZWrite", !mn.zWrite, "1")
		outline, _ := mn.meshOutline.restValue.(float32)
		set("meshOutline", outline != 0, outline)
		if len(x) > 0 {
			n.Extras = x
		} else {
			n.Extras = nil
		}
	}
	unlitUsed := false
	for i := range e.materials {
		d, mm := doc.Materials[i], m.materials[i]
		if d.PBRMetallicRoughness == nil {
			d.PBRMetallicRoughness = &gltf.PBRMetallicRoughness{}
		}
		pbr := d.PBRMetallicRoughness
		c, _ := mm.baseColorFactor.restValue.([4]float32)
		pbr.BaseColorFactor = &c
		metallic, _ := mm.metallic.restValue.(float32)
		roughness, _ := mm.roughness.restValue.(float32)
		pbr.MetallicFactor, pbr.RoughnessFactor = gltf.Float(metallic), gltf.Float(roughness)
		d.EmissiveFactor, _ = mm.emission.restValue.([3]float32)
		d.AlphaMode = [...]gltf.AlphaMode{gltf.AlphaOpaque, gltf.AlphaMask, gltf.AlphaBlend}[mm.alphaMode]
		d.AlphaCutoff = nil
		if mm.alphaMode == AlphaModeMask {
			cut, _ := mm.alphaCutoff.restValue.(float32)
			d.AlphaCutoff = gltf.Float(cut)
		}
		d.DoubleSided = mm.doubleSided
		if d.OcclusionTexture != nil {
			ao, _ := mm.ambientOcclusion.restValue.(float32)
			d.OcclusionTexture.Strength = gltf.Float(ao)
		}
		if mm.unlit {
			if d.Extensions == nil {
				d.Extensions = gltf.Extensions{}
			}
			d.Extensions["KHR_materials_unlit"] = json.RawMessage("{}")
			unlitUsed = true
		} else if d.Extensions != nil {
			delete(d.Extensions, "KHR_materials_unlit")
		}
	}
	if unlitUsed {
		used := false
		for _, u := range doc.ExtensionsUsed {
			used = used || u == "KHR_materials_unlit"
		}
		if !used {
			doc.ExtensionsUsed = append(doc.ExtensionsUsed, "KHR_materials_unlit")
		}
	}
	if len(e.lights) > 0 {
		raw, _ := doc.Extensions["KHR_lights_punctual"].(json.RawMessage)
		var ext map[string]interface{}
		if json.Unmarshal(raw, &ext) != nil {
			return fmt.Errorf("%v: its lights could not be read", file)
		}
		lights, _ := ext["lights"].([]interface{})
		for i := range e.lights {
			if i >= len(lights) || i >= len(m.lights) {
				continue
			}
			l, _ := lights[i].(map[string]interface{})
			if l == nil {
				continue
			}
			ml := &m.lights[i]
			c, _ := ml.color.restValue.([3]float32)
			l["color"] = []float32{c[0], c[1], c[2]}
			l["intensity"], _ = ml.intensity.restValue.(float32)
			if r, _ := ml.lightRange.restValue.(float32); r > 0 {
				l["range"] = r
			} else {
				delete(l, "range")
			}
			if ml.lightType == SpotLight {
				inner, _ := ml.innerConeAngle.restValue.(float32)
				outer, _ := ml.outerConeAngle.restValue.(float32)
				l["spot"] = map[string]interface{}{"innerConeAngle": inner, "outerConeAngle": outer}
			}
		}
		out, err := json.Marshal(ext)
		if err != nil {
			return err
		}
		doc.Extensions["KHR_lights_punctual"] = json.RawMessage(out)
	}
	// Animations' keys: each sampler changed, its times and values (and how it interpolates)
	uses := map[uint32]int{}
	for _, a := range doc.Animations {
		for _, s := range a.Samplers {
			uses[s.Input]++
			uses[s.Output]++
		}
	}
	for as := range e.samplers {
		a, s := as[0], as[1]
		if a >= len(doc.Animations) || a >= len(m.animations) || s >= len(doc.Animations[a].Samplers) ||
			s >= len(m.animations[a].samplers) {
			return fmt.Errorf("%v: its animations changed since it was read", file)
		}
		ds, ms := doc.Animations[a].Samplers[s], m.animations[a].samplers[s]
		n := 3
		if int(ds.Output) < len(doc.Accessors) {
			n = int(doc.Accessors[ds.Output].Type.Components())
		}
		in, err := editorWriteKeys(doc, ds.Input, m.animationTimeStamps[ms.inputIndex], 1, uses[ds.Input] > 1)
		if err != nil {
			return fmt.Errorf("%v: %v", file, err)
		}
		out, err := editorWriteKeys(doc, ds.Output, ms.output, n, uses[ds.Output] > 1)
		if err != nil {
			return fmt.Errorf("%v: %v", file, err)
		}
		ds.Input, ds.Output = in, out
		ds.Interpolation = gltf.Interpolation(ms.interpolation)
	}

	var w bytes.Buffer
	enc := gltf.NewEncoder(&w) // without a file system: external buffers are not written again
	enc.AsBinary = len(data) >= 4 && string(data[:4]) == "glTF"
	if !enc.AsBinary {
		enc.SetJSONIndent("", "  ")
	}
	if err := enc.Encode(doc); err != nil {
		return err
	}
	if _, err := os.Stat(file + ".bak"); os.IsNotExist(err) {
		if err := os.WriteFile(file+".bak", data, 0644); err != nil {
			return err
		}
	}
	tmp := file + ".ikdbg"
	if err := os.WriteFile(tmp, w.Bytes(), 0644); err != nil {
		return err
	}
	if err := os.Rename(tmp, file); err != nil {
		os.Remove(tmp)
		return err
	}
	// The model in use is the file now: a reload keeps it
	if st, err := os.Stat(file); err == nil && edStage.model != nil {
		edStage.modelTime = st.ModTime()
	}
	editorModelRead()
	return nil
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
	vec := func(v ...float32) *lua.LTable {
		t := array()
		for _, f := range v {
			t.Append(lua.LNumber(f))
		}
		return t
	}
	f32 := func(p GLTFAnimatableProperty) float32 {
		v, _ := p.getValue().(float32)
		return v
	}
	result := func(l *lua.LState, err error) int {
		if err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	}
	luaRegister(l, "debugModel", func(l *lua.LState) int {
		/*The stage's 3D model; nil without one.
		@function debugModel
		@treturn table {file, dirty (changed since its file was read), sel (the node outlined, -1),
		scenes = array of {name, nodes (its roots)},
		anims = array of {name, id, enabled, time, duration, loop, loopCount, channels},
		nodes = array of {name, visible, mesh (index, -1), light (index, -1), parent (-1: a root),
		children, t, r, s (at rest: translation, rotation quaternion x y z w, scale), animated (an
		animation moves it), trans (0 none, 1 add, 2 sub, 3 mul), layer (nil: its scene's), shadow,
		ztest, zwrite, outline},
		meshes = array of {name, prims = array of {material (-1), vertices, indices, mode}},
		materials = array of {name, color (r g b a), emission (r g b), metallic, roughness, ao,
		alpha (0 opaque, 1 mask, 2 blend), cutoff, doubleSided, unlit, tex (1 base colour, 2 normal,
		4 metallic-roughness, 8 occlusion, 16 emission), animated},
		lights = array of {name, type (0 directional, 1 point, 2 spot), r, g, b, intensity, range,
		inner, outer, animated}}; indices are 0-based.*/
		m, names := editorStageModelNow()
		if m == nil || !debuggerAttached() {
			l.Push(lua.LNil)
			return 1
		}
		t := l.NewTable()
		t.RawSetString("file", lua.LString(edStage.modelFile))
		t.RawSetString("dirty", lua.LBool(edModel.dirty()))
		t.RawSetString("sel", lua.LNumber(edModel.sel))
		scenes := array()
		for _, s := range m.scenes {
			st := l.NewTable()
			st.RawSetString("name", lua.LString(s.name))
			roots := array()
			for _, n := range s.nodes {
				roots.Append(lua.LNumber(n))
			}
			st.RawSetString("nodes", roots)
			scenes.Append(st)
		}
		t.RawSetString("scenes", scenes)
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
			at.RawSetString("channels", lua.LNumber(len(a.channels)))
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
			mesh, light := -1, -1
			if n.meshIndex != nil {
				mesh = int(*n.meshIndex)
			}
			if n.lightIndex != nil {
				light = int(*n.lightIndex)
			}
			nt.RawSetString("mesh", lua.LNumber(mesh))
			nt.RawSetString("light", lua.LNumber(light))
			nt.RawSetString("parent", lua.LNumber(parent[i]))
			children := array()
			for _, c := range n.childrenIndex {
				children.Append(lua.LNumber(c))
			}
			nt.RawSetString("children", children)
			tr, _ := n.transition.restValue.([3]float32)
			rot, _ := n.rotation.restValue.([4]float32)
			sc, _ := n.scale.restValue.([3]float32)
			nt.RawSetString("t", vec(tr[:]...))
			nt.RawSetString("r", vec(rot[:]...))
			nt.RawSetString("s", vec(sc[:]...))
			nt.RawSetString("animated", lua.LBool(n.transition.isAnimated || n.rotation.isAnimated || n.scale.isAnimated))
			nt.RawSetString("trans", lua.LNumber(n.trans))
			if n.layerNumber != nil {
				nt.RawSetString("layer", lua.LNumber(*n.layerNumber))
			}
			nt.RawSetString("shadow", lua.LBool(n.castShadow))
			nt.RawSetString("ztest", lua.LBool(n.zTest))
			nt.RawSetString("zwrite", lua.LBool(n.zWrite))
			nt.RawSetString("outline", lua.LNumber(f32(n.meshOutline)))
			nodes.Append(nt)
		}
		t.RawSetString("nodes", nodes)
		meshes := array()
		for i, me := range m.meshes {
			mt := l.NewTable()
			mname := name(names.meshes, i)
			if mname == "" {
				mname = lua.LString(me.name)
			}
			mt.RawSetString("name", mname)
			prims := array()
			for _, p := range me.primitives {
				pt := l.NewTable()
				mat := -1
				if p.materialIndex != nil {
					mat = int(*p.materialIndex)
				}
				pt.RawSetString("material", lua.LNumber(mat))
				pt.RawSetString("vertices", lua.LNumber(p.numVertices))
				pt.RawSetString("indices", lua.LNumber(p.numIndices))
				pt.RawSetString("mode", lua.LNumber(p.mode))
				prims.Append(pt)
			}
			mt.RawSetString("prims", prims)
			meshes.Append(mt)
		}
		t.RawSetString("meshes", meshes)
		materials := array()
		for _, mm := range m.materials {
			mt := l.NewTable()
			mt.RawSetString("name", lua.LString(mm.name))
			c, _ := mm.baseColorFactor.getValue().([4]float32)
			em, _ := mm.emission.getValue().([3]float32)
			mt.RawSetString("color", vec(c[:]...))
			mt.RawSetString("emission", vec(em[:]...))
			mt.RawSetString("metallic", lua.LNumber(f32(mm.metallic)))
			mt.RawSetString("roughness", lua.LNumber(f32(mm.roughness)))
			mt.RawSetString("ao", lua.LNumber(f32(mm.ambientOcclusion)))
			mt.RawSetString("alpha", lua.LNumber(mm.alphaMode))
			mt.RawSetString("cutoff", lua.LNumber(f32(mm.alphaCutoff)))
			mt.RawSetString("doubleSided", lua.LBool(mm.doubleSided))
			mt.RawSetString("unlit", lua.LBool(mm.unlit))
			tex := 0
			for k, p := range []*uint32{mm.textureIndex, mm.normalMapIndex, mm.metallicRoughnessMapIndex,
				mm.ambientOcclusionMapIndex, mm.emissionMapIndex} {
				if p != nil {
					tex |= 1 << k
				}
			}
			mt.RawSetString("tex", lua.LNumber(tex))
			mt.RawSetString("animated", lua.LBool(mm.baseColorFactor.isAnimated || mm.emission.isAnimated ||
				mm.metallic.isAnimated || mm.roughness.isAnimated))
			materials.Append(mt)
		}
		t.RawSetString("materials", materials)
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
			lt.RawSetString("intensity", lua.LNumber(f32(lg.intensity)))
			lt.RawSetString("range", lua.LNumber(f32(lg.lightRange)))
			lt.RawSetString("inner", lua.LNumber(f32(lg.innerConeAngle)))
			lt.RawSetString("outer", lua.LNumber(f32(lg.outerConeAngle)))
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
	luaRegister(l, "debugModelAnimTime", func(l *lua.LState) int {
		/*Put an animation of the stage's model at a time (turning it on); it goes on from there.
		@function debugModelAnimTime
		@tparam int32 index 0-based.
		@tparam float32 time In seconds.*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.animations) {
			return 0
		}
		editorModelAnimAt(m, i, float32(numArg(l, 2)))
		return 0
	})
	luaRegister(l, "debugModelAnimKeys", func(l *lua.LState) int {
		/*The keys of an animation of the stage's model, for its channels moving nodes.
		@function debugModelAnimKeys
		@tparam int32 index 0-based.
		@treturn table {anim, duration, channels = array of {node, path (0 translation, 1 rotation,
		2 scale), sampler (0-based), interp (0 linear, 1 step, 2 cubic spline), times, values (3 or 4
		numbers a key; a cubic spline's in-tangent, value, out-tangent)}}; nil without it. Times and
		values are strings of numbers apart by spaces, each the shortest that reads back the same
		float32 (JSON's 6 digits would round them).*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.animations) {
			l.Push(lua.LNil)
			return 1
		}
		nums := func(v []float32) lua.LString {
			b := make([]byte, 0, len(v)*10)
			for k, f := range v {
				if k > 0 {
					b = append(b, ' ')
				}
				b = strconv.AppendFloat(b, float64(f), 'g', -1, 32)
			}
			return lua.LString(b)
		}
		a := m.animations[i]
		t := l.NewTable()
		t.RawSetString("anim", lua.LNumber(i))
		t.RawSetString("duration", lua.LNumber(a.duration))
		channels := array()
		for _, c := range a.channels {
			path := editorChannelPath(c)
			if path < 0 || int(c.samplerIndex) >= len(a.samplers) {
				continue
			}
			s := a.samplers[c.samplerIndex]
			ct := l.NewTable()
			ct.RawSetString("node", lua.LNumber(*c.nodeIndex))
			ct.RawSetString("path", lua.LNumber(path))
			ct.RawSetString("sampler", lua.LNumber(c.samplerIndex))
			ct.RawSetString("interp", lua.LNumber(s.interpolation))
			ct.RawSetString("times", nums(m.animationTimeStamps[s.inputIndex]))
			ct.RawSetString("values", nums(s.output))
			channels.Append(ct)
		}
		t.RawSetString("channels", channels)
		l.Push(t)
		return 1
	})
	luaRegister(l, "debugModelAnimSampler", func(l *lua.LState) int {
		/*Give a sampler of an animation of the stage's model other keys (a sampler moving nodes).
		@function debugModelAnimSampler
		@tparam int32 anim 0-based.
		@tparam int32 sampler 0-based.
		@tparam int32 interp 0 linear, 1 step, 2 cubic spline.
		@tparam table times In seconds, rising.
		@tparam table values 3 or 4 numbers a key (a cubic spline: in-tangent, value, out-tangent).
		@treturn boolean ok
		@treturn string error*/
		m, _ := editorStageModelNow()
		ai, si := int(numArg(l, 1)), int(numArg(l, 2))
		if m == nil || !debuggerAttached() || ai < 0 || ai >= len(m.animations) || si < 0 || si >= len(m.animations[ai].samplers) {
			return result(l, fmt.Errorf("no such sampler"))
		}
		a := m.animations[ai]
		n := 0
		for _, c := range a.channels {
			if int(c.samplerIndex) == si {
				switch editorChannelPath(c) {
				case 0, 2:
					n = 3
				case 1:
					n = 4
				}
			}
		}
		interp := int(numArg(l, 3))
		floats := func(t *lua.LTable) []float32 {
			var out []float32
			for k := 1; k <= t.Len(); k++ {
				out = append(out, float32(lua.LVAsNumber(t.RawGetInt(k))))
			}
			return out
		}
		times, values := floats(l.CheckTable(4)), floats(l.CheckTable(5))
		per := n
		if interp == InterpolationCubicSpline {
			per *= 3
		}
		if n == 0 || interp < 0 || interp > 2 || len(times) == 0 || len(values) != len(times)*per {
			return result(l, fmt.Errorf("keys that do not fit the sampler"))
		}
		for k := 1; k < len(times); k++ {
			if times[k] <= times[k-1] {
				return result(l, fmt.Errorf("times that do not rise"))
			}
		}
		// Times of its own: others may share the ones it had
		s := a.samplers[si]
		key := uint32(0x80000000) | uint32(ai)<<16 | uint32(si)
		m.animationTimeStamps[key] = times
		s.inputIndex, s.output, s.interpolation = key, values, GLTFAnimationInterpolation(interp)
		a.duration = 0
		for _, o := range a.samplers {
			if ts := m.animationTimeStamps[o.inputIndex]; len(ts) > 0 && ts[len(ts)-1] > a.duration {
				a.duration = ts[len(ts)-1]
			}
		}
		if a.enabled {
			editorModelAnimAt(m, ai, a.time)
		}
		edModel.samplers[[2]int{ai, si}] = true
		return result(l, nil)
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
	luaRegister(l, "debugModelNodeTRS", func(l *lua.LState) int {
		/*Place a node of the stage's model (at rest: an animation moving it still wins).
		@function debugModelNodeTRS
		@tparam int32 index 0-based.
		@tparam float32 tx Translation, then the rotation quaternion (x, y, z, w), then the scale:
		ten numbers in all.*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.nodes) {
			return 0
		}
		var v [10]float32
		for k := range v {
			v[k] = float32(numArg(l, 2+k))
		}
		n := m.nodes[i]
		n.transition.restAt([3]float32{v[0], v[1], v[2]})
		n.rotation.restAt([4]float32{v[3], v[4], v[5], v[6]})
		n.scale.restAt([3]float32{v[7], v[8], v[9]})
		n.transformChanged = true
		edModel.trs[i] = true
		return 0
	})
	luaRegister(l, "debugModelNodeProps", func(l *lua.LState) int {
		/*Change how a node of the stage's model is drawn (the Ikemen extras of its glTF node).
		@function debugModelNodeProps
		@tparam int32 index 0-based.
		@tparam int32 trans 0 none, 1 add, 2 sub, 3 mul.
		@tparam int32 layer nil: its scene's; -1, 0 behind the players, 1 in front.
		@tparam boolean castShadow
		@tparam boolean zTest
		@tparam boolean zWrite
		@tparam float32 outline*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.nodes) {
			return 0
		}
		n := m.nodes[i]
		n.trans = Trans(Clamp(int32(numArg(l, 2)), 0, 3))
		n.layerNumber = nil
		if !nilArg(l, 3) {
			n.layerNumber = new(int)
			*n.layerNumber = int(numArg(l, 3))
		}
		n.castShadow, n.zTest, n.zWrite = boolArg(l, 4), boolArg(l, 5), boolArg(l, 6)
		n.meshOutline.restAt(float32(numArg(l, 7)))
		edModel.props[i] = true
		return 0
	})
	luaRegister(l, "debugModelMaterial", func(l *lua.LState) int {
		/*Change a material of the stage's model (an animation of it still wins).
		@function debugModelMaterial
		@tparam int32 index 0-based.
		@tparam float32 r Base colour r, g, b, a; emission r, g, b; metallic; roughness; occlusion
		strength: ten numbers.
		@tparam int32 alpha 0 opaque, 1 mask, 2 blend (the 12th argument).
		@tparam float32 cutoff
		@tparam boolean doubleSided
		@tparam boolean unlit*/
		m, _ := editorStageModelNow()
		i := int(numArg(l, 1))
		if m == nil || !debuggerAttached() || i < 0 || i >= len(m.materials) {
			return 0
		}
		var v [10]float32
		for k := range v {
			v[k] = float32(numArg(l, 2+k))
		}
		mm := m.materials[i]
		mm.baseColorFactor.restAt([4]float32{v[0], v[1], v[2], v[3]})
		mm.emission.restAt([3]float32{v[4], v[5], v[6]})
		mm.metallic.restAt(v[7])
		mm.roughness.restAt(v[8])
		if mm.ambientOcclusionMapIndex != nil {
			mm.ambientOcclusion.restAt(v[9])
		}
		mm.alphaMode = AlphaMode(Clamp(int32(numArg(l, 12)), 0, 2))
		// The shader cuts with it whatever the mode: the engine reads it for mask only
		mm.alphaCutoff.restAt(float32(0))
		if mm.alphaMode == AlphaModeMask {
			mm.alphaCutoff.restAt(float32(numArg(l, 13)))
		}
		mm.doubleSided, mm.unlit = boolArg(l, 14), boolArg(l, 15)
		edModel.materials[i] = true
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
		@tparam[opt] float32 range
		@tparam[opt] float32 inner A spot light's cone angles, in radians.
		@tparam[opt] float32 outer*/
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
		if !nilArg(l, 7) && !nilArg(l, 8) {
			lg.innerConeAngle.restAt(float32(numArg(l, 7)))
			lg.outerConeAngle.restAt(float32(numArg(l, 8)))
		}
		edModel.lights[i] = true
		return 0
	})
	luaRegister(l, "debugModelSelect", func(l *lua.LState) int {
		/*The node of the stage's model outlined on the frames (editorModelGeometry).
		@function debugModelSelect
		@tparam int32 index 0-based; -1 none.*/
		if debuggerAttached() {
			edModel.sel = int(numArg(l, 1))
		}
		return 0
	})
	luaRegister(l, "debugModelPick", func(l *lua.LState) int {
		/*The node of the stage's model under a point of the game picture, outlined as hovered
		(or as selected).
		@function debugModelPick
		@tparam float32 x In the game picture's pixels; nil: no point (nothing hovered).
		@tparam float32 y
		@tparam[opt] boolean select It becomes the node selected.
		@treturn int32 node 0-based; -1 none.*/
		m, _ := editorStageModelNow()
		node := -1
		if m != nil && debuggerAttached() && !nilArg(l, 1) {
			node = editorModelPick(m, float32(numArg(l, 1)), float32(numArg(l, 2)))
		}
		edModel.hover = node
		if !nilArg(l, 3) && boolArg(l, 3) {
			edModel.sel = node
			edModel.picked++
		}
		l.Push(lua.LNumber(node))
		return 1
	})
	luaRegister(l, "debugModelSave", func(l *lua.LState) int {
		/*Write what was changed in the stage's model into its glTF file (a .bak the first time).
		@function debugModelSave
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			return result(l, fmt.Errorf("needs -ikdbg"))
		}
		return result(l, editorModelSave())
	})
	luaRegister(l, "debugModelRevert", func(l *lua.LState) int {
		/*Forget the changes of the stage's model: the next reload of the stage reads its file again.
		@function debugModelRevert*/
		if debuggerAttached() {
			edStage.modelTime = time.Time{}
		}
		return 0
	})
}
