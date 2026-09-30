#!/bin/bash
# winbatch.sh <file>... -- <command>...
#
# Runs several probes or tests on the reference Windows machine in one connection: one upload of all
# the files, then one session that runs every command in turn in their directory and then removes it,
# the session sharing the upload's login (ControlMaster).
# Each command's output follows a line "===== <command>" and ends with "WINRUN-EXIT <code>".  It runs
# in the ssh service session; programs that need the signed-in user's desktop go through
# scripts/winrun.sh --desktop instead.
#
#   WIN_HOST=host WIN_USER=user WIN_PORT=22 scripts/winbatch.sh a.exe b_test.exe -- "a.exe" "b_test.exe domdoc"
#
# Check that the port answers before anything else, and never retry a failed login: the host bans
# addresses that keep trying, and a burst of short connections as well; that is why both steps go over
# the one connection.
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
# the upload and the session share one connection; the control socket wants a short path
mux=$(mktemp -d)
MUX=(-o ControlMaster=auto -o "ControlPath=$mux/%C" -o ControlPersist=60 -o ServerAliveInterval=15 -o ServerAliveCountMax=8)
trap 'ssh "${MUX[@]}" -p "$PORT" -O exit "$USERNAME@$HOST" >/dev/null 2>&1; rm -rf -- "${mux:?}"' EXIT
# scp never opens a shared connection itself (it passes ControlMaster=no), so one is opened first.
# ConnectTimeout does not cover the key exchange, which has been seen to stall for good: the login
# gets a minute.  WIN_SSH_LOG=<file> keeps a verbose log of it, to see where a failed one stopped.
timeout 60 ssh -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -o ControlMaster=yes \
        ${WIN_SSH_LOG:+-v -E "$WIN_SSH_LOG"} -p "$PORT" -fN "$USERNAME@$HOST" ||
    { echo "ssh login failed" >&2; exit 2; }
stage=$(mktemp -d)
mkdir "$stage/$tag"
cp -- "${files[@]}" "$stage/$tag/" || { rm -rf -- "${stage:?}"; exit 2; }
scp -q -r -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -P "$PORT" "$stage/$tag" "$USERNAME@$HOST:$TEMP/" ||
    { rm -rf -- "${stage:?}"; echo "scp failed" >&2; exit 2; }
rm -rf -- "${stage:?}"

line="cd /d %TEMP%\\$tag"
for cmd in "$@"; do
    line+=" & echo ===== $cmd & $cmd & call echo WINRUN-EXIT %^errorlevel%"
done
line+=" & cd /d %TEMP% & rmdir /s /q $tag"
ssh -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -p "$PORT" "$USERNAME@$HOST" "$line" 2>&1 | tr -d '\r'
