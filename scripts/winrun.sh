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
    exit "${PIPESTATUS[0]}"
fi

tag="WineAltarsProbe_$(date +%s)_$RANDOM"
ps1=$(mktemp --suffix=.ps1)
printf '%s\r\n' "\$exe = Join-Path \$env:TEMP \"$name\"" \
    "& \$exe $* 2>&1 | Out-File -Encoding ascii (Join-Path \$env:TEMP \"$tag.out\")" \
    "Set-Content -Encoding ascii -Path (Join-Path \$env:TEMP \"$tag.exit\") -Value \$LASTEXITCODE" > "$ps1"
scp -q -o BatchMode=yes -o ConnectTimeout=10 -P "$PORT" "$ps1" "$USERNAME@$HOST:$TEMP/$tag.ps1" || { rm -f "$ps1"; echo "desktop task upload failed" >&2; exit 2; }
rm -f "$ps1"
"${SSH[@]}" "schtasks /create /tn $tag /tr \"powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\\Users\\$USERNAME\\AppData\\Local\\Temp\\$tag.ps1\" /sc once /st 23:59 /it >nul && schtasks /run /tn $tag >nul" >/dev/null 2>&1 || { echo "desktop task start failed" >&2; exit 2; }
completed=0
for i in $(seq 1 180); do
    result=$("${SSH[@]}" "if exist %TEMP%\\$tag.exit (echo done) else (echo waiting)" 2>/dev/null | tr -d '\r') || { echo "desktop task polling failed" >&2; exit 2; }
    if [ "$result" = done ]; then completed=1; break; fi
    sleep 2
done
[ "$completed" = 1 ] || { echo "desktop task timed out; leaving its files for inspection" >&2; exit 124; }
"${SSH[@]}" "type %TEMP%\\$tag.out" 2>&1 | tr -d '\r'
output_status=${PIPESTATUS[0]}
result=$("${SSH[@]}" "type %TEMP%\\$tag.exit" 2>/dev/null | tr -d '\r\n') || { echo "desktop task exit status unavailable" >&2; exit 2; }
[ "$output_status" = 0 ] || { echo "desktop task output unavailable" >&2; exit 2; }
case "$result" in ''|*[!0-9]*) echo "invalid desktop task exit status" >&2; exit 2 ;; esac
"${SSH[@]}" "schtasks /delete /tn $tag /f >nul 2>&1 & del /q %TEMP%\\$tag.out %TEMP%\\$tag.exit %TEMP%\\$tag.ps1 2>nul" >/dev/null 2>&1 || echo "desktop task cleanup failed" >&2
printf 'remote_exit_code=%s\n' "$result" >&2
[ "$result" = 0 ]
