#!/usr/bin/env bash
# Print the display to actually run Office on, and why.
#
# This project spent a long session measuring on :77 without noticing it is
#
#     /usr/bin/Xvfb :77 -screen 0 1920x1080x24 -nolisten tcp
#
# a headless framebuffer nobody can see. That is the right target for reading
# pixels back with XGetImage and the wrong one for anything a person has to
# look at or type into. The real session was reachable the whole time; the
# probe that said otherwise was just missing XAUTHORITY.
#
#   eval "$(scripts/pick-display.sh --export)"    # sets DISPLAY and XAUTHORITY
#   scripts/pick-display.sh                       # just report
set -uo pipefail
EXPORT=0; [ "${1:-}" = "--export" ] && EXPORT=1

best_disp=""; best_auth=""; best_score=-1
for sock in /tmp/.X11-unix/X*; do
    n=${sock##*/X}
    d=":$n"
    # try each auth file that might belong to this user's session
    for auth in "" $HOME/.Xauthority /run/user/$(id -u)/.mutter-Xwaylandauth.*; do
        [ -n "$auth" ] && [ ! -e "$auth" ] && continue
        dims=$(XAUTHORITY="${auth:-$HOME/.Xauthority}" DISPLAY="$d" timeout 5 xdpyinfo 2>/dev/null \
               | awk '/^  dimensions/ {print $2; exit}')
        [ -n "$dims" ] || continue
        # score: a real session beats a virtual framebuffer
        score=0
        server=$(ps -eo args 2>/dev/null | grep -E "X(org|vfb|wayland)[^ ]* +$d( |$)" | grep -v grep | head -1)
        case "$server" in
            *Xvfb*)      kind="Xvfb (headless — nobody can see this)" ;;
            *Xwayland*)  kind="Xwayland (a real session)"; score=$((score+10)) ;;
            *Xorg*)      kind="Xorg (a real session)";     score=$((score+10)) ;;
            *)           kind="unknown" ;;
        esac
        # a compositor and real focus management are what make it a desktop
        comp=$(XAUTHORITY="${auth:-}" DISPLAY="$d" python3 -c "
import ctypes,ctypes.util,sys
x=ctypes.CDLL(ctypes.util.find_library('X11'))
d=x.XOpenDisplay(b'$d')
if not d: sys.exit()
a=x.XInternAtom(d,b'_NET_WM_CM_S0',0); x.XGetSelectionOwner.restype=ctypes.c_ulong
print('yes' if x.XGetSelectionOwner(d,a) else 'no')" 2>/dev/null)
        [ "$comp" = yes ] && score=$((score+5))
        printf '%-7s %-11s %-38s compositor=%-4s auth=%s\n' \
               "$d" "$dims" "$kind" "${comp:-?}" "${auth:-<none>}" >&2
        if [ "$score" -gt "$best_score" ]; then
            best_score=$score; best_disp="$d"; best_auth="$auth"
        fi
        break
    done
done

if [ -z "$best_disp" ]; then echo "no usable display found" >&2; exit 1; fi
if [ "$EXPORT" = 1 ]; then
    printf 'export DISPLAY=%s\n' "$best_disp"
    [ -n "$best_auth" ] && printf 'export XAUTHORITY=%s\n' "$best_auth"
else
    printf '\n-> use %s%s\n' "$best_disp" \
           "$([ -n "$best_auth" ] && printf ' with XAUTHORITY=%s' "$best_auth")" >&2
fi
