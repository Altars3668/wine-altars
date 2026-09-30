#!/bin/bash
# winbatch.sh <file>... -- <command>...
#
# Runs several probes or tests on the reference Windows machine in one connection: one upload of all
# the files and of a batch file holding the commands, then one session that runs the batch file in their
# directory and then removes it, the session sharing the upload's login (ControlMaster).  The commands
# go through a file because cmd.exe refuses a command line over 8191 characters ("命令行太长"); in it a
# "%" stands for itself, as it did on the command line.
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
# the batch file: each command after its "=====" line, then its exit code; the echo line has cmd's
# special characters escaped outside quotes, both lines have "%" doubled
python3 - "$stage/$tag/winbatch-run.cmd" "$@" <<'PY' || { rm -rf -- "${stage:?}"; exit 2; }
import sys
def literal(text):
    return text.replace('%', '%%')
def echoable(text):
    out, quoted = [], False
    for c in literal(text):
        if c == '"':
            quoted = not quoted
        elif not quoted and c in '^&|<>()':
            out.append('^')
        out.append(c)
    return ''.join(out)
lines = ['@echo off', 'cd /d "%~dp0"']
for command in sys.argv[2:]:
    # "(call )" clears the error level, which cmd's own commands such as echo leave as it was
    lines += ['echo ===== ' + echoable(command), '(call )', literal(command), 'echo WINRUN-EXIT %errorlevel%']
with open(sys.argv[1], 'w', newline='') as f:
    f.write('\r\n'.join(lines) + '\r\n')
PY
scp -q -r -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -P "$PORT" "$stage/$tag" "$USERNAME@$HOST:$TEMP/" ||
    { rm -rf -- "${stage:?}"; echo "scp failed" >&2; exit 2; }
rm -rf -- "${stage:?}"

# afterwards also remove what an earlier run that stopped half-way left behind
line="cd /d %TEMP%\\$tag & cmd /d /c winbatch-run.cmd & cd /d %TEMP% & rmdir /s /q $tag"
line+=' & for /d %d in ("%TEMP%\WineAltarsBatch_*") do @rmdir /s /q "%d"'
ssh -o BatchMode=yes -o ConnectTimeout=10 "${MUX[@]}" -p "$PORT" "$USERNAME@$HOST" "$line" 2>&1 | tr -d '\r'
