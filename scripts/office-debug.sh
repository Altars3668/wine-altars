#!/bin/bash
# office-debug.sh - drive and inspect an Office app on the live desktop (:0).
#
# Synthetic input from outside cannot reach Wine on this machine's GNOME
# Wayland session: Xwayland runs with -enable-ei-portal, so XTEST events go
# through the remote-desktop portal and wait for someone at the desk to allow
# them.  Everything here is done from inside the Wine session instead --
# SendInput is queued by the wineserver, never by the X server, and the UI is
# read through MSAA rather than from pixels -- so it needs nobody's consent and
# works the same on :0 as on a bare Xvfb.
#
#   office-debug.sh start [WINEDEBUG] [log]   start Word as the desktop would
#   office-debug.sh wait [class] [seconds]    wait for a top-level window
#   office-debug.sh keys key...               send keys to Word (ctrl+p, esc, down...)
#   office-debug.sh click name [class]        press an MSAA object by name
#   office-debug.sh dump [class]              print the MSAA tree
#   office-debug.sh windows                   list the session's top-level windows
#   office-debug.sh state                     MSO's view of the printer's Print Schema
#   office-debug.sh close [seconds]           close Word the way its close button does
#   office-debug.sh run tool.exe args...      run any Windows helper in the session
#
# WINEPREFIX defaults to ~/.wine-c2r-test and the Wine to /opt/wine-altars.

set -u
here=$(cd "$(dirname "$0")/.." && pwd)
prefix=${WINEPREFIX:-$HOME/.wine-c2r-test}
wine=${WINE:-/opt/wine-altars/bin/wine}
word_exe='C:\Program Files\Microsoft Office\root\Office16\WINWORD.EXE'
word_class=OpusApp

die() { echo "office-debug: $*" >&2; exit 1; }

# The desktop's own session: whatever gnome-shell runs with, which is what an
# application started from the dock or a desktop icon inherits.
session_env() {
    local shell_pid
    shell_pid=$(pgrep -u "$(id -u)" -x gnome-shell | head -1)
    [ -n "$shell_pid" ] || die "no gnome-shell session for $(id -un)"
    tr '\0' '\n' < "/proc/$shell_pid/environ" |
        grep -vE '^(WINE[A-Z_0-9]*|_)=' |
        grep -vE '^(DISPLAY|XAUTHORITY)='
}

# The Xwayland serving :0 names its own authority file; a nested or headless
# compositor started for testing has another, and must not be picked up.
xauth_for() {
    pgrep -a Xwayland | awk -v d=":$1" '$3 == d { for (i = 1; i <= NF; i++) if ($i == "-auth") print $(i + 1) }' | head -1
}

display=${OFFICE_DEBUG_DISPLAY:-0}
xauth=$(xauth_for "$display")
[ -n "$xauth" ] || die "no Xwayland for :$display"

helper_env=(DISPLAY=":$display" XAUTHORITY="$xauth" WINEPREFIX="$prefix" WINEDEBUG=-all
            LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 HOME="$HOME" USER="$USER" PATH="$PATH"
            XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}")

tool() {
    local name=$1
    local exe="$here/tools/${name%.exe}/${name%.exe}.exe"
    [ -f "$exe" ] || exe=$name
    echo "$exe"
}

run() {
    local exe
    exe=$(tool "$1"); shift
    env -i "${helper_env[@]}" "$wine" "$exe" "$@" 2>/dev/null | tr -d '\r'
}

word_pid() { pgrep -f 'Office16\\WINWORD.EXE' | head -1; }

cmd=${1:-}; shift || true
case "$cmd" in
start)
    [ -z "$(word_pid)" ] || die "Word is already running (pid $(word_pid))"
    debug=${1:-}
    log=${2:-/tmp/office-debug-word.log}
    mapfile -t envv < <(session_env)
    env -i "${envv[@]}" DISPLAY=":$display" XAUTHORITY="$xauth" WINEPREFIX="$prefix" \
        ${debug:+WINEDEBUG="$debug"} "$wine" "$word_exe" > "$log" 2>&1 &
    echo "started Word, output in $log"
    ;;
wait)
    class=${1:-$word_class}; secs=${2:-90}
    for ((i = 0; i < secs; i += 2)); do
        if run toplevels | grep -q " vis=1 $class"; then echo "$class is up after ${i}s"; exit 0; fi
        sleep 2
    done
    die "no $class after ${secs}s"
    ;;
keys)
    run sendkeys -w "$word_class" -d 400 "$@"
    ;;
click)
    name=$1; class=${2:-}
    run uiclick "$name" ${class:+"$class"}
    ;;
dump)
    run uidump ${1:+"$1"} | iconv -f UTF-8 -t UTF-8 -c
    ;;
windows)
    run toplevels "$@"
    ;;
state)
    pid=$(word_pid); [ -n "$pid" ] || die "Word is not running"
    base=$(grep -i 'OFFICE16/MSO.DLL' "/proc/$pid/maps" | head -1 | cut -d- -f1)
    [ -n "$base" ] || die "MSO.DLL is not loaded in Word"
    run msoprintstate "$base"
    ;;
close)
    secs=${1:-60}
    [ -n "$(word_pid)" ] || { echo "Word is not running"; exit 0; }
    run sendkeys -w "$word_class" -close
    for ((i = 0; i < secs; i++)); do
        [ -z "$(word_pid)" ] && { echo "Word closed after ${i}s"; exit 0; }
        sleep 1
    done
    die "Word still running after ${secs}s -- a dialog may be asking something (try: $0 dump)"
    ;;
run)
    run "$@"
    ;;
*)
    sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
