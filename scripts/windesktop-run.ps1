# windesktop-run.ps1: the task windesktop-launch.ps1 registers runs this in the signed-in user's
# desktop session; it runs the program and leaves its output and exit code where the launcher waits.
param([string]$Exe, [string]$Arguments = '', [string]$Out, [string]$ExitFile)
$program = Join-Path $PSScriptRoot $Exe
if ($Arguments) {
    & $program ($Arguments -split ' ') 2>&1 | Out-File -Encoding ascii $Out
} else {
    & $program 2>&1 | Out-File -Encoding ascii $Out
}
Set-Content -Encoding ascii -Path $ExitFile -Value $LASTEXITCODE
