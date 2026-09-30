#!/bin/bash
# winrun.sh [--desktop] <exe> [args...]
#
# Copies a probe or test to the reference Windows machine, runs it there and prints what it
# printed.  Over ssh it runs in the service session, where some classes refuse to exist at all
# (AppVisibility answers 0xc0000022); --desktop runs it instead in the signed-in user's desktop
# session through a one-shot scheduled task, hidden, and removes the task and its files after.
#
#   WIN_HOST=host WIN_USER=user WIN_PORT=22 scripts/winrun.sh probe.exe
#
# A desktop task is waited for about six minutes; WIN_WAIT sets the number of seconds for a longer
# one, such as a whole conformance test.
#
# Check that the port answers before anything else, and never retry a failed login: the host
# bans addresses that keep trying.  Every step goes over one connection -- the first opens it and the
# others share its login (ControlMaster), and the waiting for a desktop task happens on the Windows
# side -- because a burst of short ssh connections gets the address shut out as well.
set -uo pipefail
desktop=0
[ "${1:-}" = --desktop ] && { desktop=1; shift; }
exe=$1; shift
HOST="${WIN_HOST:?set WIN_HOST to the Windows machine}"
USERNAME="${WIN_USER:?set WIN_USER to its user}"
PORT="${WIN_PORT:-22}"
TEMP="C:/Users/$USERNAME/AppData/Local/Temp"
name=$(basename "$exe")

timeout 5 bash -c "</dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "$HOST:$PORT does not answer" >&2; exit 2; }
# the control socket wants a short path
mux=$(mktemp -d)
MUX=(-o ControlMaster=auto -o "ControlPath=$mux/%C" -o ControlPersist=60 -o ServerAliveInterval=15 -o ServerAliveCountMax=8)
trap 'ssh "${MUX[@]}" -p "$PORT" -O exit "$USERNAME@$HOST" >/dev/null 2>&1; rm -rf -- "${mux:?}"' EXIT
# scp never opens a shared connection itself (it passes ControlMaster=no), so one is opened first.
# ConnectTimeout does not cover the key exchange, which has been seen to stall for good: the login
# gets a minute.  WIN_SSH_LOG=<file> keeps a verbose log of it, to see where a failed one stopped.
timeout 60 ssh -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -o ControlMaster=yes \
        ${WIN_SSH_LOG:+-v -E "$WIN_SSH_LOG"} -p "$PORT" -fN "$USERNAME@$HOST" ||
    { echo "ssh login failed" >&2; exit 2; }
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -p "$PORT" "$USERNAME@$HOST")
scp -q -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -P "$PORT" "$exe" "$USERNAME@$HOST:$TEMP/$name" || { echo "scp failed" >&2; exit 2; }

if [ "$desktop" = 0 ]; then
    # run it and take it away again in the same connection; cmd passes the program's bytes through as
    # they are, and "call echo %^errorlevel%" reads the exit code after the program has run
    out=$("${SSH[@]}" "cd /d %TEMP% && $name $* & call echo WINRUN-EXIT %^errorlevel% & del /q $name" 2>&1 | tr -d '\r')
    status=$(printf '%s\n' "$out" | sed -n 's/^WINRUN-EXIT //p' | tail -1)
    printf '%s\n' "$out" | sed '/^WINRUN-EXIT /d'
    case "$status" in ''|*[!0-9-]*) echo "no exit status from $name" >&2; exit 2 ;; esac
    exit "$status"
fi

tag="WineAltarsProbe_$(date +%s)_$RANDOM"
ps1=$(mktemp --suffix=.ps1)
printf '%s\r\n' "\$exe = Join-Path \$env:TEMP \"$name\"" \
    "& \$exe $* 2>&1 | Out-File -Encoding ascii (Join-Path \$env:TEMP \"$tag.out\")" \
    "Set-Content -Encoding ascii -Path (Join-Path \$env:TEMP \"$tag.exit\") -Value \$LASTEXITCODE" > "$ps1"
scp -q -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -P "$PORT" "$ps1" "$USERNAME@$HOST:$TEMP/$tag.ps1" || { rm -f "$ps1"; echo "desktop task upload failed" >&2; exit 2; }
rm -f "$ps1"
# schtasks gives a task no start on battery power, and the laptop often runs on its battery: such a
# task only sits queued until the wait runs out.  PowerShell takes the settings off it before it runs.
settings=$(printf '%s' "\$ProgressPreference = 'SilentlyContinue'; \$t = Get-ScheduledTask -TaskName $tag;" \
    " \$t.Settings.DisallowStartIfOnBatteries = \$false; \$t.Settings.StopIfGoingOnBatteries = \$false;" \
    " Set-ScheduledTask -InputObject \$t | Out-Null" | iconv -f UTF-8 -t UTF-16LE | base64 -w0)
"${SSH[@]}" "schtasks /create /tn $tag /tr \"powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\\Users\\$USERNAME\\AppData\\Local\\Temp\\$tag.ps1\" /sc once /st 23:59 /it >nul && powershell -NoProfile -EncodedCommand $settings >nul 2>&1 && schtasks /run /tn $tag >nul" >/dev/null 2>&1 || { echo "desktop task start failed" >&2; exit 2; }
# one connection waits for the task, prints its output and exit code, and cleans up after it
wait=$(printf '%s' "\$ProgressPreference = 'SilentlyContinue'; \$t = \$env:TEMP; \$exit = Join-Path \$t '$tag.exit';" \
    " \$deadline = (Get-Date).AddSeconds(${WIN_WAIT:-360});" \
    " while (-not (Test-Path \$exit) -and (Get-Date) -lt \$deadline) { Start-Sleep -Milliseconds 500 };" \
    " if (-not (Test-Path \$exit)) { 'WINRUN-TIMEOUT'; exit 124 };" \
    " Get-Content (Join-Path \$t '$tag.out'); 'WINRUN-EXIT ' + (Get-Content \$exit | Select-Object -First 1);" \
    " schtasks /delete /tn $tag /f | Out-Null;" \
    " Remove-Item -Force (Join-Path \$t '$tag.out'), \$exit, (Join-Path \$t '$tag.ps1'), (Join-Path \$t '$name')" \
    | iconv -f UTF-8 -t UTF-16LE | base64 -w0)
out=$("${SSH[@]}" "powershell -NoProfile -EncodedCommand $wait" 2>&1 | tr -d '\r')
case "$out" in
*WINRUN-TIMEOUT*) echo "desktop task timed out; leaving its files for inspection" >&2; exit 124 ;;
esac
result=$(printf '%s\n' "$out" | sed -n 's/^WINRUN-EXIT //p' | tail -1)
printf '%s\n' "$out" | sed '/^WINRUN-EXIT /d'
case "$result" in ''|*[!0-9-]*) echo "invalid desktop task exit status" >&2; exit 2 ;; esac
printf 'remote_exit_code=%s\n' "$result" >&2
[ "$result" = 0 ]
