#!/usr/bin/env bash
# A desktop this project can measure on: mutter, hardware GL, and capturable.
#
# The two displays this project used before each answered half the question.
# `:77` is an Xvfb -- readable with XGetImage, but llvmpipe, no compositor and
# no focus management, so anything that depends on a compositor cannot be
# reproduced there at all. The account's own `:0` is the real thing and is not
# readable: under Xwayland the X11 root window holds no other client's pixels,
# so `import -root`/`xwd -root` return nothing usable, and GNOME Shell's
# screenshot D-Bus API refuses callers that are not a known app. That gap is
# why the last session had to stop at "every surface this tree owns is correct
# and what the compositor shows could not be read from this side".
#
# mutter itself closes it. Run headless with a virtual monitor it
#
#   - is the same compositor as the user's desktop (same version, same code),
#   - renders through the real GPU (amdgpu here, not llvmpipe),
#   - starts Xwayland, so Wine runs exactly as it does on the real session,
#   - and exposes org.gnome.Mutter.ScreenCast, so the *composited* result can
#     be read frame by frame -- see scripts/screen-capture.py.
#
# On a bus of its own, which is not optional: see start_bus below.
#
#   eval "$(scripts/measure-desktop.sh start)"   # DISPLAY, XAUTHORITY, and the bus
#   MEASURE_GEOM=2992x1440,1440x2992 scripts/measure-desktop.sh start
#                               # one virtual monitor per size: switch between them with
#                               # scripts/monitor-switch.py, as a remote desktop does
#   scripts/measure-desktop.sh status
#   scripts/measure-desktop.sh stop
#
# It does not replace pick-display.sh: when the user is logged in, their own
# session is still the right place to reproduce something they can see. This
# is for when there is no session to use, or when the composited output has to
# be read rather than looked at.
set -uo pipefail
STATE="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/wine-altars-measure-desktop"
LOG="$STATE/mutter.log"
PIDFILE="$STATE/mutter.pid"
BUS_ADDRFILE="$STATE/bus.address"
BUS_PIDFILE="$STATE/bus.pid"
GEOM="${MEASURE_GEOM:-1920x1080}"
USER_BUS="unix:path=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/bus"

mkdir -p "$STATE"

running() {
    [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

newest_auth() {
    ls -t "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/.mutter-Xwaylandauth.* 2>/dev/null | head -1
}

bus_running() {
    [ -f "$BUS_PIDFILE" ] && kill -0 "$(cat "$BUS_PIDFILE")" 2>/dev/null
}

# A bus of its own, because org.gnome.Mutter.ScreenCast is a well-known name and
# a well-known name belongs to whichever process asks for it first. Started on
# the account's own bus, this mutter takes ScreenCast, RemoteDesktop and
# DisplayConfig away from the session the user is logged into -- and the user's
# gnome-shell, coming up later, silently queues behind it holding only
# org.gnome.Shell.
#
# Nothing reports an error afterwards. gnome-remote-desktop records a virtual
# monitor on *this* desktop and streams it, so the remote client is shown an
# empty screen belonging to a compositor nobody is sitting at while the session
# the user is actually in is never captured at all; xdg-desktop-portal offers
# this desktop to screen sharing; org.gnome.Shell.Screencast records it. It cost
# an evening to find once. Own bus, no name to take.
start_bus() {
    /usr/bin/dbus-daemon --session --fork \
        --print-address=1 --print-pid=3 3>"$BUS_PIDFILE"
}

# Isolation is the whole point, so check it instead of assuming it.
assert_not_hijacked() {
    owner=$(busctl --address="$USER_BUS" --timeout=5 \
        call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus \
        GetConnectionUnixProcessID s org.gnome.Mutter.ScreenCast 2>/dev/null \
        | awk '{print $2}')
    [ -n "$owner" ] || return 0
    [ "$owner" = "$(cat "$PIDFILE" 2>/dev/null)" ] || return 0

    echo "this mutter took org.gnome.Mutter.ScreenCast on the account's bus," >&2
    echo "which breaks remote desktop and screen sharing for the real session." >&2
    echo "Stopping it rather than handing back a desktop that costs that much." >&2
    kill "$(cat "$PIDFILE")" 2>/dev/null
    rm -f "$PIDFILE"
    exit 1
}

case "${1:-status}" in
start)
    if running; then
        echo "already running (pid $(cat "$PIDFILE"))" >&2
    else
        : > "$LOG"
        bus_running || start_bus > "$BUS_ADDRFILE" || {
            echo "could not start a private session bus" >&2; exit 1; }
        setsid env XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
            DBUS_SESSION_BUS_ADDRESS="$(cat "$BUS_ADDRFILE")" \
            mutter --headless $(for g in ${GEOM//,/ }; do printf -- '--virtual-monitor %s ' "$g"; done) \
            >>"$LOG" 2>&1 &
        echo $! > "$PIDFILE"
        for _ in $(seq 1 20); do
            grep -q 'Using public X11 display' "$LOG" && break
            sleep 1
        done
        grep -q 'Using public X11 display' "$LOG" || {
            echo "mutter did not come up; see $LOG" >&2; exit 1; }
        assert_not_hijacked
    fi
    disp=$(sed -n 's/.*Using public X11 display \(:[0-9]*\).*/\1/p' "$LOG" | head -1)
    auth=$(newest_auth)
    printf 'export DISPLAY=%s\n' "$disp"
    printf 'export XAUTHORITY=%s\n' "$auth"
    printf 'export DBUS_SESSION_BUS_ADDRESS=%s\n' "$(cat "$BUS_ADDRFILE")"
    ;;
stop)
    if running; then kill "$(cat "$PIDFILE")" && echo "stopped" >&2
    else echo "not running" >&2; fi
    bus_running && kill "$(cat "$BUS_PIDFILE")" 2>/dev/null
    rm -f "$PIDFILE" "$BUS_PIDFILE" "$BUS_ADDRFILE"
    ;;
status)
    if running; then
        disp=$(sed -n 's/.*Using public X11 display \(:[0-9]*\).*/\1/p' "$LOG" | head -1)
        echo "running: pid $(cat "$PIDFILE") display $disp auth $(newest_auth)" >&2
        echo "  bus: $(cat "$BUS_ADDRFILE" 2>/dev/null || echo '(none -- on the account bus!)')" >&2
        DISPLAY="$disp" XAUTHORITY="$(newest_auth)" glxinfo -B 2>/dev/null \
            | sed -n 's/^ *Device: /  GL: /p'
    else
        echo "not running" >&2
    fi
    ;;
*)
    echo "usage: $0 {start|stop|status}" >&2; exit 2 ;;
esac
