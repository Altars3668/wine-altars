# win-deskrun.ps1 -Exe <program> [-Arguments <text>] [-Wait <seconds>]
#
# Runs a program from the directory this script is in, in the signed-in user's desktop session, and
# prints its output: what scripts/winrun.sh --desktop does for one program, for use inside a
# scripts/winbatch.sh batch, so that desktop programs share the batch's single connection:
#
#   scripts/winbatch.sh probe.exe scripts/win-deskrun.ps1 -- \
#       "powershell -NoProfile -ExecutionPolicy Bypass -File win-deskrun.ps1 -Exe probe.exe"
#
# Over ssh a program runs in the service session, which has no desktop to draw on; a one-shot
# scheduled task with an interactive logon runs it in the user's session instead, hidden.  The
# task is given leave to start on battery power, as the laptop often runs on its battery, and is
# removed afterwards together with its files.  Its exit code becomes this script's; a program that
# has not finished after -Wait seconds (default 400) prints DESKRUN-TIMEOUT and exits 124.
param([Parameter(Mandatory = $true)][string]$Exe, [string]$Arguments = '', [int]$Wait = 400)
$ProgressPreference = 'SilentlyContinue'
$dir = $PSScriptRoot
$tag = 'WineAltarsDesk_' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$out = Join-Path $dir "$tag.out"
$err = Join-Path $dir "$tag.err"
$done = Join-Path $dir "$tag.exit"
$inner = Join-Path $dir "$tag.ps1"
$argumentList = if ($Arguments) { "-ArgumentList '$Arguments' " } else { '' }
Set-Content -Encoding ascii -LiteralPath $inner -Value @(
    "Set-Location -LiteralPath '$dir'",
    "`$p = Start-Process -FilePath '$(Join-Path $dir $Exe)' $argumentList-NoNewWindow -Wait -PassThru -RedirectStandardOutput '$out' -RedirectStandardError '$err'",
    "Set-Content -Encoding ascii -LiteralPath '$done' -Value `$p.ExitCode"
)
# schtasks takes the command as one argument of at most 261 characters; the batch directory has no
# spaces in it, so the path needs no quotes, which Windows PowerShell would pass on mangled
schtasks /create /tn $tag /tr "powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $inner" /sc once /st 23:59 /it /f | Out-Null
if ($LASTEXITCODE) { 'DESKRUN-CREATE-FAILED'; Remove-Item -Force $inner; exit 2 }
$task = Get-ScheduledTask -TaskName $tag
$task.Settings.DisallowStartIfOnBatteries = $false
$task.Settings.StopIfGoingOnBatteries = $false
Set-ScheduledTask -InputObject $task | Out-Null
schtasks /run /tn $tag | Out-Null
$deadline = (Get-Date).AddSeconds($Wait)
while (-not (Test-Path $done) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Milliseconds 300
if (Test-Path $out) { Get-Content -LiteralPath $out }
if (Test-Path $err) { Get-Content -LiteralPath $err }
if (Test-Path $done) { $code = [int](Get-Content -LiteralPath $done | Select-Object -First 1) }
else { 'DESKRUN-TIMEOUT'; $code = 124 }
schtasks /delete /tn $tag /f | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $out, $err, $done, $inner
exit $code
