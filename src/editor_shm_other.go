//go:build !windows

package main

// editorShmCreate: frames for the editor are only shared on Windows for now.
func editorShmCreate(size int) []byte { return nil }
