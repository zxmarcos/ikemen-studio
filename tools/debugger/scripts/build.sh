#!/bin/bash
# Builds Ikemen GO (build/build.sh Win64) and/or the debugger in MSYS2 UCRT64.
# Usage: build.sh [ikemen] [debugger] [test]   (ikemen and debugger when no argument is given)
# Run from PowerShell through build.ps1, which sets up the MSYS2 environment.
root="$(cd "$(dirname "$0")/../../.." && pwd)"
targets="${*:-ikemen debugger}"
export PATH="/c/Program Files/Go/bin:$PATH"
cd "$root" || exit 1
for t in $targets; do
	case $t in
	ikemen)
		go version || exit 1
		./build/build.sh Win64 || exit 1
		;;
	debugger)
		cmake -S tools/debugger -B tools/debugger/build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit 1
		cmake --build tools/debugger/build --target ikemen-debugger || exit 1
		;;
	test)
		cmake -S tools/debugger -B tools/debugger/build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit 1
		cmake --build tools/debugger/build --target air-test sff-test snd-test cmd-test ini-test || exit 1
		tools/debugger/build/air-test.exe || exit 1
		tools/debugger/build/sff-test.exe || exit 1
		tools/debugger/build/snd-test.exe || exit 1
		tools/debugger/build/cmd-test.exe || exit 1
		tools/debugger/build/ini-test.exe || exit 1
		;;
	*)
		echo "unknown target: $t"; exit 1
		;;
	esac
done
