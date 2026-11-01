param([string]$Msys = "$env:MSYS2_ROOT")
# One-time setup of a fresh MSYS2 (https://www.msys2.org, installed at $Msys):
# full update, then the packages Ikemen GO's BUILDING.md lists (minus Go: the
# system Go in C:\Program Files\Go is used) and cmake/ninja for the debugger.
# MSYS2's folder comes from -Msys or the environment variable MSYS2_ROOT.
if (-not (Test-Path "$Msys\usr\bin\bash.exe")) { "MSYS2 not found at '$Msys': set MSYS2_ROOT or pass -Msys."; exit 1 }
$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
function Run($cmd) {
    "### $cmd"
    & "$Msys\usr\bin\bash.exe" -lc $cmd
}
Run "true"
Run "pacman --noconfirm -Syuu"
Run "pacman --noconfirm -Syuu"
Run "pacman -S --noconfirm --needed git make diffutils mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-tools mingw-w64-ucrt-x86_64-libxmp mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja"
Run "gcc --version | head -1; pkg-config --modversion sdl2 libxmp"
