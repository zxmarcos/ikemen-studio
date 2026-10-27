package main

import (
	"encoding/hex"
	"fmt"

	lua "github.com/yuin/gopher-lua"
)

// The palette editor of a debugger (with "-ikdbg"): a palette's colours put in
// place while the match goes on, for every player using the same SFF, and the
// palette a player was given.

// editorSetPalette gives palette group,number (PalTable's key: a character's
// palette N is 1,N, from its ACT file or its SFF) the colours rgba (256 RGBA
// colours; colour 0 stays transparent).
func editorSetPalette(pn int, group, number uint16, rgba []byte) error {
	if pn < 0 || pn >= len(sys.cgi) || sys.cgi[pn].sff == nil || sys.cgi[pn].palettedata == nil {
		return fmt.Errorf("no player %v", pn+1)
	}
	if len(rgba) != 1024 {
		return fmt.Errorf("a palette is 256 colours")
	}
	pal := make([]uint32, 256)
	for i := range pal {
		c := rgba[i*4:]
		alpha := uint32(255)
		if i == 0 {
			alpha = 0
		}
		pal[i] = alpha<<24 | uint32(c[2])<<16 | uint32(c[1])<<8 | uint32(c[0])
	}
	found := false
	for i := range sys.cgi {
		gi := &sys.cgi[i]
		if gi.sff != sys.cgi[pn].sff || gi.palettedata == nil {
			continue
		}
		pl := &gi.palettedata.palList
		idx, ok := pl.PalTable[[2]uint16{group, number}]
		if !ok || idx < 0 || idx >= len(pl.palettes) {
			continue
		}
		// In place of the colours, keeping the remaps (the palette the player was given)
		pl.palettes[idx] = pal
		for len(pl.PalTex) <= idx {
			pl.PalTex = append(pl.PalTex, nil)
		}
		pl.PalTex[idx] = NewTextureFromPalette(pal)
		found = true
	}
	if !found {
		return fmt.Errorf("player %v has no palette %v,%v", pn+1, group, number)
	}
	return nil
}

func editorPaletteFunctions(l *lua.LState) {
	luaRegister(l, "debugSetPalette", func(l *lua.LState) int {
		/*Give a palette new colours, for every player using the same SFF.
		@function debugSetPalette
		@tparam int32 pn Player number (1 for P1).
		@tparam int32 group Palette group (1 for the character's palettes).
		@tparam int32 number Palette number.
		@tparam string colours 256 RGBA colours in hexadecimal (2048 digits).
		@treturn boolean ok
		@treturn string error*/
		if !debuggerAttached() {
			l.Push(lua.LFalse)
			l.Push(lua.LString("needs -ikdbg"))
			return 2
		}
		rgba, err := hex.DecodeString(strArg(l, 4))
		if err == nil {
			err = editorSetPalette(int(numArg(l, 1))-1, uint16(numArg(l, 2)), uint16(numArg(l, 3)), rgba)
		}
		if err != nil {
			l.Push(lua.LFalse)
			l.Push(lua.LString(err.Error()))
			return 2
		}
		l.Push(lua.LTrue)
		return 1
	})
	luaRegister(l, "debugPalNo", func(l *lua.LState) int {
		/*The palette a player was given (its number, 1 to the number of palettes).
		@function debugPalNo
		@tparam int32 pn Player number (1 for P1).
		@treturn int32 palNo 0 without a player.*/
		pn := int(numArg(l, 1)) - 1
		if pn < 0 || pn >= len(sys.cgi) {
			l.Push(lua.LNumber(0))
			return 1
		}
		l.Push(lua.LNumber(sys.cgi[pn].palno))
		return 1
	})
}
