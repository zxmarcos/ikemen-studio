[CmdletBinding(PositionalBinding = $false)]
param([string]$Msys = "$env:MSYS2_ROOT", [Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets = @())
# Runs build.sh in MSYS2 UCRT64: .\build.ps1 [ikemen] [debugger] [test]
# MSYS2's folder comes from -Msys or the environment variable MSYS2_ROOT.
# (PowerShell 5.1 strips quotes from native arguments, so the work is in a script file.)
if (-not (Test-Path "$Msys\usr\bin\bash.exe")) { "MSYS2 not found at '$Msys': set MSYS2_ROOT or pass -Msys."; exit 1 }
$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
$sh = (Join-Path $PSScriptRoot "build.sh") -replace '\\', '/'
if ($sh -match '^([A-Za-z]):(.*)$') { $sh = "/" + $Matches[1].ToLower() + $Matches[2] }
& "$Msys\usr\bin\bash.exe" -l $sh @Targets
exit $LASTEXITCODE
