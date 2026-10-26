param([string]$Actions = "", [int]$Wait = 14, [switch]$Fresh, [switch]$Keep, [string]$Exe = (Resolve-Path "$PSScriptRoot\..\..\..\Ikemen_GO.exe").Path, [string]$P1 = "kfm", [string]$P2 = "kfm", [string]$IniExtra = "", [string]$Shots = "$PSScriptRoot\..\build\shots")
# UI test: starts build\ikemen-debugger.exe --launch, lays out both windows, runs actions and saves
# the debugger's frames as PNGs in $Shots. -Fresh writes a new ini (AI on both sides).
# Clicks are WM_APP+0x4D47 messages (wParam 0 move, 1/2 left down/up; lParam x|y<<16) and frames
# are saved by the app itself on WM_APP+0x4D48, so the real mouse and screen are never used.
# Coordinates are client pixels of the debugger window (sized to 1600x1000).
# Actions, '|'-separated: key:F5 (key:F10*3 repeats) | wait:2 | click:x,y | dclick:x,y | type:text | shot:name
#   | game:hide, game:show, game:min, game:off (the game window: hidden, shown, minimized, moved off screen)
#   | down:RIGHT, up:RIGHT (hold and release a key; letters, digits, arrows, ALT, SHIFT) | visible (prints whether the game window is shown)
#   | drag:x1,y1,x2,y2 (left button held from one point to the other) | ps:code (PowerShell, e.g. to edit a file)
#   | lua:code (run in the game as if typed in the Console; the result goes to the Console)
#   | ctrl:Z (a key with Ctrl held) | wheel:x,y,n (n notches, up when positive)
#   | keys:+DOWN,+RIGHT,-DOWN,+A,-A,-RIGHT (pressed +, released -, 50 ms apart: a motion for the game)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class W {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct COPYDATA { public IntPtr dwData; public int cbData; public IntPtr lpData; }
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref COPYDATA l);
  public static void Lua(IntPtr h, string s) { byte[] b = Encoding.UTF8.GetBytes(s); IntPtr p = Marshal.AllocHGlobal(b.Length); Marshal.Copy(b, 0, p, b.Length); COPYDATA d = new COPYDATA(); d.dwData = (IntPtr)0x4D47; d.cbData = b.Length; d.lpData = p; SendMessage(h, 0x004A, IntPtr.Zero, ref d); Marshal.FreeHGlobal(p); }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
$dir = (Resolve-Path "$PSScriptRoot\..\build").Path
$shots = $Shots
New-Item -ItemType Directory -Force $shots | Out-Null
if ($Fresh) { "[IkemenDebugger][Settings]`nExe=$Exe`nP1=$P1`nP2=$P2`nStage=stage0`nExtra=-windowed -nosound`nAI1=1`nAI2=1`n$IniExtra" | Set-Content "$dir\ikemen-debugger.ini" -Encoding ascii }
Get-Process Ikemen_GO, ikemen-debugger -ErrorAction SilentlyContinue | Stop-Process -Force
$dbg = Start-Process "$dir\ikemen-debugger.exe" -ArgumentList "--launch" -PassThru
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 20) { $dbg.Refresh(); if ($dbg.MainWindowHandle -ne 0) { break }; Start-Sleep -Milliseconds 200 }
$hd = $dbg.MainWindowHandle
[W]::SetWindowPos($hd, [IntPtr]::Zero, 0, 0, 1600, 1000, 0x0040) | Out-Null
$game = $null
while (((Get-Date) - $t0).TotalSeconds -lt 30) { $game = Get-Process Ikemen_GO -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1; if ($game) { break }; Start-Sleep -Milliseconds 300 }
if ($game) { $sw = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea; [W]::SetWindowPos($game.MainWindowHandle, [IntPtr]::Zero, 1920 - 640, 1040 - 480, 640, 480, 0x0040) | Out-Null } else { "game window not found" }
Start-Sleep -Seconds $Wait

$shotN = 0; $vk = @{ END = 0x23; BACK = 0x08; HOME = 0x24; F5 = 0x74; F10 = 0x79; F11 = 0x7A; RETURN = 0x0D; LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28; ALT = 0x12; SHIFT = 0x10 }
foreach ($c in [char[]]"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") { $vk["$c"] = [int]$c }
foreach ($a in ($Actions -split '\|' | Where-Object { $_ })) {
  $kind, $arg = $a -split ':', 2
  switch ($kind) {
    'key' { $name, $times = $arg -split '\*'; if (-not $times) { $times = 1 }; for ($r = 0; $r -lt [int]$times; $r++) { $code = $vk[$name]; [W]::PostMessage($hd, 0x100, [IntPtr]$code, [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 60; [W]::PostMessage($hd, 0x101, [IntPtr]$code, [IntPtr]0xC0000001) | Out-Null; Start-Sleep -Milliseconds 30 } }
    'dclick' { $x, $y = $arg -split ','; $l = [IntPtr](([int]$y -shl 16) -bor [int]$x); $m = 0x8000 + 0x4D47; [W]::PostMessage($hd, $m, [IntPtr]0, $l) | Out-Null; Start-Sleep -Milliseconds 40; foreach ($k in 1..2) { [W]::PostMessage($hd, $m, [IntPtr]1, $l) | Out-Null; Start-Sleep -Milliseconds 40; [W]::PostMessage($hd, $m, [IntPtr]2, $l) | Out-Null; Start-Sleep -Milliseconds 40 } }
    'drag' { $x1, $y1, $x2, $y2 = $arg -split ',' | ForEach-Object { [int]$_ }; $m = 0x8000 + 0x4D47; [W]::PostMessage($hd, $m, [IntPtr]0, [IntPtr](($y1 -shl 16) -bor $x1)) | Out-Null; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]1, [IntPtr](($y1 -shl 16) -bor $x1)) | Out-Null; Start-Sleep -Milliseconds 80; foreach ($s in 1..10) { $x = $x1 + ($x2 - $x1) * $s / 10; $y = $y1 + ($y2 - $y1) * $s / 10; [W]::PostMessage($hd, $m, [IntPtr]0, [IntPtr](([int]$y -shl 16) -bor [int]$x)) | Out-Null; Start-Sleep -Milliseconds 40 }; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]2, [IntPtr](($y2 -shl 16) -bor $x2)) | Out-Null }
    'ps' { Invoke-Expression $arg }
    'wheel' { $x, $y, $n = $arg -split ',' | ForEach-Object { [int]$_ }; $m = 0x8000 + 0x4D47; $l = [IntPtr](($y -shl 16) -bor $x); [W]::PostMessage($hd, $m, [IntPtr]0, $l) | Out-Null; Start-Sleep -Milliseconds 40; foreach ($k in 1..[Math]::Abs($n)) { [W]::PostMessage($hd, $m, [IntPtr]($(if ($n -gt 0) { 5 } else { 6 })), $l) | Out-Null; Start-Sleep -Milliseconds 40 } }
    'lua' { [W]::Lua($hd, $arg) }
    'ctrl' { [W]::PostMessage($hd, 0x100, [IntPtr]0x11, [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 40; [W]::PostMessage($hd, 0x100, [IntPtr]$vk[$arg], [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 60; [W]::PostMessage($hd, 0x101, [IntPtr]$vk[$arg], [IntPtr]0xC0000001) | Out-Null; Start-Sleep -Milliseconds 40; [W]::PostMessage($hd, 0x101, [IntPtr]0x11, [IntPtr]0xC0000001) | Out-Null }
    'down' { [W]::PostMessage($hd, 0x100, [IntPtr]$vk[$arg], [IntPtr]1) | Out-Null }
    'up' { [W]::PostMessage($hd, 0x101, [IntPtr]$vk[$arg], [IntPtr]0xC0000001) | Out-Null }
    'keys' { foreach ($k in $arg -split ',') { $code = $vk[$k.Substring(1)]; if ($k[0] -eq '+') { [W]::PostMessage($hd, 0x100, [IntPtr]$code, [IntPtr]1) | Out-Null } else { [W]::PostMessage($hd, 0x101, [IntPtr]$code, [IntPtr]0xC0000001) | Out-Null }; Start-Sleep -Milliseconds 50 } }
    'visible' { "game window visible: " + [W]::IsWindowVisible($game.MainWindowHandle) }
    'game' { $g = $game.MainWindowHandle; switch ($arg) { 'hide' { [W]::ShowWindow($g, 0) | Out-Null } 'show' { [W]::ShowWindow($g, 5) | Out-Null } 'min' { [W]::ShowWindow($g, 6) | Out-Null } 'off' { [W]::SetWindowPos($g, [IntPtr]::Zero, -5000, -5000, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null } } }
    'rclick' { $x, $y = $arg -split ','; $l = [IntPtr](([int]$y -shl 16) -bor [int]$x); $m = 0x8000 + 0x4D47; [W]::PostMessage($hd, $m, [IntPtr]0, $l) | Out-Null; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]3, $l) | Out-Null; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]4, $l) | Out-Null }
    'wait' { Start-Sleep -Milliseconds ([int]([double]$arg * 1000)) }
    'click' { $x, $y = $arg -split ','; $l = [IntPtr](([int]$y -shl 16) -bor [int]$x); $m = 0x8000 + 0x4D47; [W]::PostMessage($hd, $m, [IntPtr]0, $l) | Out-Null; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]1, $l) | Out-Null; Start-Sleep -Milliseconds 80; [W]::PostMessage($hd, $m, [IntPtr]2, $l) | Out-Null }
    'type' { foreach ($ch in $arg.ToCharArray()) { [W]::PostMessage($hd, 0x102, [IntPtr][int]$ch, [IntPtr]1) | Out-Null; Start-Sleep -Milliseconds 15 } }
    'shot' { $script:shotN++; Remove-Item "$dir\shot_$shotN.bmp" -ErrorAction SilentlyContinue; [W]::PostMessage($hd, 0x8000 + 0x4D48, [IntPtr]$shotN, [IntPtr]0) | Out-Null; $tw = Get-Date; while (-not (Test-Path "$dir\shot_$shotN.bmp") -and ((Get-Date) - $tw).TotalSeconds -lt 3) { Start-Sleep -Milliseconds 50 }; Start-Sleep -Milliseconds 100; $img = [System.Drawing.Image]::FromFile("$dir\shot_$shotN.bmp"); $out = Join-Path $shots "$arg.png"; $img.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $img.Dispose(); Remove-Item "$dir\shot_$shotN.bmp"; $out }
    
  }
  Start-Sleep -Milliseconds 150
}
if (-not $Keep) {
  $dbg.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2
  Get-Process Ikemen_GO, ikemen-debugger -ErrorAction SilentlyContinue | Stop-Process -Force
}
"done"
