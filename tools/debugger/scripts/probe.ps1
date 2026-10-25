param([string]$Ik = (Resolve-Path "$PSScriptRoot\..\..\..").Path, [int]$Seconds = 20, [string]$P1 = "kfm", [string]$P2 = "kfm", [string]$Log = "$PSScriptRoot\..\build\probe.log")
# Protocol test without the GUI: starts the patched Ikemen with the mod, sends a few commands
# over stdin and prints the replies, some controller sources and one tick's trace. The whole
# output goes to $Log. Stdin must not start with a UTF-8 BOM (it would stop the game).
Copy-Item "$PSScriptRoot\..\mod\ikdbg.lua" "$Ik\external\mods\ikdbg.lua" -Force
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$Ik\Ikemen_GO.exe"
$psi.WorkingDirectory = $Ik
$psi.Arguments = "-p1 $P1 -p2 $P2 -p1.ai 1 -p2.ai 1 -s stage0 -windowed -nosound -loadmotif -ikdbg 1"
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
$p = [System.Diagnostics.Process]::Start($psi)
$lines = New-Object System.Collections.Concurrent.ConcurrentQueue[string]
$errq = New-Object System.Collections.Concurrent.ConcurrentQueue[string]
$j1 = Register-ObjectEvent -InputObject $p -EventName OutputDataReceived -Action { if ($EventArgs.Data) { $Event.MessageData.Enqueue([string]$EventArgs.Data) } } -MessageData $lines
$j2 = Register-ObjectEvent -InputObject $p -EventName ErrorDataReceived -Action { if ($EventArgs.Data) { $Event.MessageData.Enqueue([string]$EventArgs.Data) } } -MessageData $errq
$p.BeginOutputReadLine(); $p.BeginErrorReadLine()
$in = $p.StandardInput
$script = @(
  @(9, "if ikdbg then ikdbg.exec('player(1); setVar(7, 1234); setFvar(3, 2.5); return var(7), fvar(3)') end"),
  @(9.5, "if ikdbg then ikdbg.exec('player(2); setPos(0, -40); return posX(), posY()') end"),
  @(10, "if ikdbg then ikdbg.exec('player(1); return stateSource(), stateSource(0), stateSource(200)') end")
)
$t0 = Get-Date; $k = 0; $brkSent = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) {
    $el = ((Get-Date) - $t0).TotalSeconds
    if ($k -lt $script.Count -and $el -gt $script[$k][0]) { $cmd = [string]$script[$k][1]; $lines.Enqueue(">>> " + $cmd); $in.WriteLine($cmd); $in.Flush(); $k++ }
    Start-Sleep -Milliseconds 50
}
if (-not $p.HasExited) { $p.Kill() }
Start-Sleep -Milliseconds 300
Unregister-Event -SourceIdentifier $j1.Name; Unregister-Event -SourceIdentifier $j2.Name
$all = $lines.ToArray()
$all | Set-Content $Log -Encoding utf8
"lines: $($all.Count)"
$all | Where-Object { $_ -notmatch '^@(IKS|IKSRC) ' } | ForEach-Object { $_.Substring(0, [Math]::Min(300, $_.Length)) } | Select-Object -First 30
$src = @{}
foreach ($l in $all | Where-Object { $_ -like '@IKSRC *' }) {
  $j = $l.Substring(7) | ConvertFrom-Json
  "IKSRC from $($j.from): $($j.items.Count) items, $([int]($l.Length/1024)) KB"
  for ($i = 0; $i -lt $j.items.Count; $i++) { $src[$j.from + $i] = $j.items[$i] }
}
"--- sample sources"
foreach ($i in 1, 2, 3, 50, 400) { if ($src[$i]) { "$i : $($src[$i].file):$($src[$i].line) $($src[$i].type) state $($src[$i].state)" } }
"--- one tick's trace (P1)"
$snap = $all | Where-Object { $_ -like '@IKS *' } | Select-Object -Last 40 | Select-Object -First 1
$j = $snap.Substring(5) | ConvertFrom-Json
"tick $($j.t), tr entries: $($j.tr.Count / 2), p1 state $($j.ents[0].st) ss $($j.ents[0].ss)"
$p1 = $j.ents[0].id
for ($i = 0; $i -lt $j.tr.Count; $i += 2) { if ($j.tr[$i] -eq $p1) { $s = $src[[int]$j.tr[$i+1]]; "  $($s.state) $($s.type) $($s.file):$($s.line)" } }
"--- stderr (not kfm warnings)"
$errq.ToArray() | Where-Object { $_ -notmatch 'Unknown state controller parameter' } | Select-Object -First 15
Remove-Item "$Ik\external\mods\ikdbg.lua" -Force
