# Working in this clone

This is the user's clone of Ikemen GO. The local branch `debugger-bridge` adds
an external debugger: the engine side in `src/script_debugger.go` (plus a few
lines in `compiler.go`, `bytecode.go`, `script.go`) and the debugger program in
`tools/debugger`. Read `tools/debugger/NOTES.md` first: state of the work,
build environment and tests.

## Rules

- Chat with the user in Portuguese; commit messages in English, ending with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Never push. Commit on `debugger-bridge` (or a branch off it); `develop`
  tracks upstream.
- At most two background subagents at a time, on disjoint files.
- Videos sent to the user: 30 MB at most.
- Keep engine changes inert without `-ikdbg` and easy to upstream: new code
  goes in `src/script_debugger.go` or new `src/editor_*.go` files, with
  one-line calls elsewhere. The goal is now a live editor (see NOTES.md,
  "Where it is going").

## Environment

- Use PowerShell; the Bash tool fails silently on this machine. PowerShell
  5.1 strips quotes from native arguments, so MSYS2 work goes through script
  files (`tools/debugger/scripts/build.ps1` runs `build.sh`). Commit
  messages too: write them to a file in the scratchpad, `git commit -F file`.
- MSYS2 UCRT64 at `$env:MSYS2_ROOT` (gcc, SDL2, libxmp, cmake, ninja);
  Go at `C:\Program Files\Go`. `C:\msys64` is an old 2016 install: do not use it.
- Release assets (chars, stages, data, font, sound, video) were copied from
  the v1.0.0 Windows release and are ignored by git; `chars/ryu_pots`
  is the large test character (about 6000 controllers).
- The screen is at 125% scale: test scripts must be DPI aware. Never capture
  the screen (SetForegroundWindow + CopyFromScreen once captured the user's
  browser); the debugger saves its own frames (see `scripts/test_ui.ps1`).
