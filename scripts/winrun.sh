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
# Check that the port answers before anything else, and never retry a failed login: the host
# bans addresses that keep trying.
set -uo pipefail
desktop=0
[ "${1:-}" = --desktop ] && { desktop=1; shift; }
exe=$1; shift
HOST="${WIN_HOST:?set WIN_HOST to the Windows machine}"
USERNAME="${WIN_USER:?set WIN_USER to its user}"
PORT="${WIN_PORT:-22}"
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 -p "$PORT" "$USERNAME@$HOST")
TEMP="C:/Users/$USERNAME/AppData/Local/Temp"
name=$(basename "$exe")

timeout 5 bash -c "</dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "$HOST:$PORT does not answer" >&2; exit 2; }
scp -q -o BatchMode=yes -o ConnectTimeout=10 -P "$PORT" "$exe" "$USERNAME@$HOST:$TEMP/$name" || { echo "scp failed" >&2; exit 2; }

if [ "$desktop" = 0 ]; then
    "${SSH[@]}" "cd /d %TEMP% && $name $*" 2>&1 | tr -d '\r'
    exit 0
fi

ps1=$(mktemp --suffix=.ps1)
printf '%s\r\n' "\$exe = Join-Path \$env:TEMP \"$name\"" \
    "& \$exe $* 2>&1 | Out-File -Encoding ascii (Join-Path \$env:TEMP \"$name.out\")" > "$ps1"
scp -q -o BatchMode=yes -o ConnectTimeout=10 -P "$PORT" "$ps1" "$USERNAME@$HOST:$TEMP/$name.ps1"
rm -f "$ps1"
"${SSH[@]}" "schtasks /create /tn WineAltarsProbe /tr \"powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\\Users\\$USERNAME\\AppData\\Local\\Temp\\$name.ps1\" /sc once /st 23:59 /it /f >nul & schtasks /run /tn WineAltarsProbe >nul" >/dev/null 2>&1
for i in $(seq 1 60); do
    sleep 2
    "${SSH[@]}" "if exist %TEMP%\\$name.out (tasklist /fi \"imagename eq $name\" | find /i \"$name\" >nul || echo done)" 2>/dev/null | tr -d '\r' | grep -q done && break
done
"${SSH[@]}" "type %TEMP%\\$name.out & schtasks /delete /tn WineAltarsProbe /f >nul 2>&1 & del /q %TEMP%\\$name.out %TEMP%\\$name.ps1 2>nul" 2>&1 | tr -d '\r'
