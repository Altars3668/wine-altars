# windesktop-launch.ps1 -Exe <program> [-Arguments "<args>"] [-Wait <seconds>]
#
# For scripts/winbatch.sh: runs a program of the batch's directory in the signed-in user's desktop
# session instead of the ssh service session, from inside the batch's one ssh connection.  It
# registers a one-shot task (interactive logon, allowed on battery power: the laptop often runs on
# its battery, and a task that may not start there only waits), starts it, waits for its exit file,
# prints what the program printed and its exit code, and removes the task.  The task runs
# windesktop-run.ps1, which must be in the batch too.
#
#   winbatch.sh probe.exe scripts/windesktop-launch.ps1 scripts/windesktop-run.ps1 -- \
#       "powershell -NoProfile -ExecutionPolicy Bypass -File windesktop-launch.ps1 -Exe probe.exe"
param([Parameter(Mandatory = $true)][string]$Exe, [string]$Arguments = '', [int]$Wait = 300)
$ProgressPreference = 'SilentlyContinue'
$dir = $PSScriptRoot
$tag = 'WineAltarsDesk_' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$runner = Join-Path $dir 'windesktop-run.ps1'
$out = Join-Path $dir "$tag.out"
$exit = Join-Path $dir "$tag.exit"
$argument = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$runner`" -Exe `"$Exe`" -Out `"$out`" -ExitFile `"$exit`""
if ($Arguments) { $argument += " -Arguments `"$Arguments`"" }
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $argument -WorkingDirectory $dir
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $tag -Action $action -Principal $principal -Settings $settings | Out-Null
Start-ScheduledTask -TaskName $tag
$deadline = (Get-Date).AddSeconds($Wait)
while (-not (Test-Path $exit) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (Test-Path $exit) {
    Get-Content $out
    'DESKTOP-EXIT ' + (Get-Content $exit | Select-Object -First 1)
} else {
    'DESKTOP-TIMEOUT after ' + $Wait + ' s'
}
Unregister-ScheduledTask -TaskName $tag -Confirm:$false
