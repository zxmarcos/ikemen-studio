//go:build windows

package main

import (
	"os"
	"strconv"
	"unsafe"

	"golang.org/x/sys/windows"
)

// editorShmCreate maps "Local\IkemenFrame<pid>" for the editor (see editor_frame.go).
func editorShmCreate(size int) []byte {
	name, err := windows.UTF16PtrFromString(`Local\IkemenFrame` + strconv.Itoa(os.Getpid()))
	if err != nil {
		return nil
	}
	h, err := windows.CreateFileMapping(windows.InvalidHandle, nil, windows.PAGE_READWRITE,
		uint32(uint64(size)>>32), uint32(size), name)
	if err != nil {
		LogMessage("Editor frames: " + err.Error())
		return nil
	}
	addr, err := windows.MapViewOfFile(h, windows.FILE_MAP_WRITE, 0, 0, uintptr(size))
	if err != nil {
		LogMessage("Editor frames: " + err.Error())
		windows.CloseHandle(h)
		return nil
	}
	// The mapping lives as long as the process
	return unsafe.Slice((*byte)(unsafe.Pointer(addr)), size)
}
