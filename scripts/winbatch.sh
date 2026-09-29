#!/bin/bash
# winbatch.sh <file>... -- <command>...
#
# Runs several probes or tests on the reference Windows machine in two connections: one upload of all
# the files, one session that runs every command in turn in their directory and then removes it.
# Each command's output follows a line "===== <command>" and ends with "WINRUN-EXIT <code>".  It runs
# in the ssh service session; programs that need the signed-in user's desktop go through
# scripts/winrun.sh --desktop instead.
#
#   WIN_HOST=host WIN_USER=user WIN_PORT=22 scripts/winbatch.sh a.exe b_test.exe -- "a.exe" "b_test.exe domdoc"
#
# Check that the port answers before anything else, and never retry a failed login: the host bans
# addresses that keep trying, and a burst of short connections as well.
set -uo pipefail
files=()
while [ $# -gt 0 ] && [ "$1" != -- ]; do files+=("$1"); shift; done
[ "${1:-}" = -- ] && shift
[ ${#files[@]} -gt 0 ] && [ $# -gt 0 ] || { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
HOST="${WIN_HOST:?set WIN_HOST to the Windows machine}"
USERNAME="${WIN_USER:?set WIN_USER to its user}"
PORT="${WIN_PORT:-22}"
TEMP="C:/Users/$USERNAME/AppData/Local/Temp"
tag="WineAltarsBatch_$(date +%s)_$RANDOM"

timeout 5 bash -c "</dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "$HOST:$PORT does not answer" >&2; exit 2; }
stage=$(mktemp -d)
mkdir "$stage/$tag"
cp -- "${files[@]}" "$stage/$tag/" || { rm -rf -- "${stage:?}"; exit 2; }
scp -q -r -o BatchMode=yes -o ConnectTimeout=10 -P "$PORT" "$stage/$tag" "$USERNAME@$HOST:$TEMP/" ||
    { rm -rf -- "${stage:?}"; echo "scp failed" >&2; exit 2; }
rm -rf -- "${stage:?}"

line="cd /d %TEMP%\\$tag"
for cmd in "$@"; do
    line+=" & echo ===== $cmd & $cmd & call echo WINRUN-EXIT %^errorlevel%"
done
line+=" & cd /d %TEMP% & rmdir /s /q $tag"
ssh -o BatchMode=yes -o ConnectTimeout=10 -p "$PORT" "$USERNAME@$HOST" "$line" 2>&1 | tr -d '\r'
