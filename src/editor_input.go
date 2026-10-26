package main

import "strconv"

// What a player's input does, for the editor's Commands window: added to the
// player's entry of the frame geometry (editor_frame.go).
//
//	"in": [U, D, B, F, a, b, c, x, y, z, s, d, w, m]  frames held (> 0) or released (< 0)
//	"cm": [[name, n, done, steps, time, maxtime, buffer], ...]  the commands under
//	      way or buffered: the n-th definition of that name (0-based, in the
//	      order of the CMD file), steps done of all, time since the first step,
//	      buffer frames left ("command = name" is true while it is above 0)
func editorInputGeometry(b []byte, c *Char) []byte {
	if c.cmd == nil || c.playerNo < 0 || c.playerNo >= len(c.cmd) {
		return b
	}
	cl := &c.cmd[c.playerNo]
	if ib := cl.Buffer; ib != nil {
		b = append(b, `,"in":[`...)
		for i, v := range [...]int32{ib.Ub, ib.Db, ib.Bb, ib.Fb, ib.ab, ib.bb, ib.cb, ib.xb, ib.yb, ib.zb,
			ib.sb, ib.db, ib.wb, ib.mb} {
			if i > 0 {
				b = append(b, ',')
			}
			b = strconv.AppendInt(b, int64(v), 10)
		}
		b = append(b, ']')
	}
	b = append(b, `,"cm":[`...)
	first := true
	for _, defs := range cl.Commands {
		for n := range defs {
			cm := &defs[n]
			done := 0
			for i := len(cm.completed) - 1; i >= 0; i-- {
				if cm.completed[i] {
					done = i + 1
					break
				}
			}
			if done == 0 && cm.curbuftime <= 0 {
				continue
			}
			if !first {
				b = append(b, ',')
			}
			first = false
			b = append(b, '[')
			b = editorJSONString(b, cm.name)
			for _, v := range [...]int{n, done, len(cm.steps), int(cm.curtime), int(cm.maxtime), int(cm.curbuftime)} {
				b = append(b, ',')
				b = strconv.AppendInt(b, int64(v), 10)
			}
			b = append(b, ']')
		}
	}
	return append(b, ']')
}

// editorJSONString appends s as a JSON string (UTF-8 kept as it is).
func editorJSONString(b []byte, s string) []byte {
	const hex = "0123456789abcdef"
	b = append(b, '"')
	for i := 0; i < len(s); i++ {
		switch ch := s[i]; {
		case ch == '"' || ch == '\\':
			b = append(b, '\\', ch)
		case ch < 0x20:
			b = append(b, '\\', 'u', '0', '0', hex[ch>>4], hex[ch&15])
		default:
			b = append(b, ch)
		}
	}
	return append(b, '"')
}
