package main

import (
	lua "github.com/yuin/gopher-lua"
)

// A character's explods and projectiles, for an external debugger: all of them in one call
// (explodVar and projVar read one field of one of them).

func editorObjectsFunctions(l *lua.LState) {
	luaRegister(l, "debugObjects", func(l *lua.LState) int {
		/*[redirectable] The character's explods and its projectiles.
		@function debugObjects
		@treturn table explods Array of {id, anim, elem, time, removetime, x, y, bindtime, layer, sprpriority, facing}.
		@treturn table projectiles Array of {id, anim, elem, time, removetime, x, y, vx, vy, hits, misstime, priority}.*/
		c := sys.debugWC
		item := func(kv ...interface{}) *lua.LTable {
			t := l.NewTable()
			for i := 0; i+1 < len(kv); i += 2 {
				var v lua.LNumber
				switch n := kv[i+1].(type) {
				case int:
					v = lua.LNumber(n)
				case int32:
					v = lua.LNumber(n)
				case float32:
					v = lua.LNumber(n)
				}
				t.RawSetString(kv[i].(string), v)
			}
			return t
		}
		explods, projs := l.NewTable(), l.NewTable()
		for _, e := range sys.explods[c.playerNo] {
			if e.id == IErr || e.anim == nil || e.ownerId != c.id {
				continue
			}
			explods.Append(item("id", e.id, "anim", e.animNo, "elem", e.anim.curelem+1, "time", e.time,
				"removetime", e.removetime,
				"x", e.pos[0]+e.offset[0]+e.relativePos[0]+e.interpolate_pos[0],
				"y", e.pos[1]+e.offset[1]+e.relativePos[1]+e.interpolate_pos[1],
				"bindtime", e.bindtime, "layer", e.layerno, "sprpriority", e.sprpriority, "facing", e.facing))
		}
		for _, p := range sys.projs[c.playerNo] {
			if p.ownerId != c.id || !p.isActive() {
				continue
			}
			elem := int32(0)
			if p.anim != nil {
				elem = p.anim.curelem + 1
			}
			projs.Append(item("id", p.id, "anim", p.animNo, "elem", elem, "time", p.time,
				"removetime", p.removetime, "x", p.pos[0], "y", p.pos[1], "vx", p.velocity[0], "vy", p.velocity[1],
				"hits", p.hits, "misstime", p.curmisstime, "priority", p.priority))
		}
		l.Push(explods)
		l.Push(projs)
		return 2
	})
}
