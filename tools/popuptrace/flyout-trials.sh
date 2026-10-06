#!/bin/bash
# flyout-trials.sh <out-dir> <trials> <wine-root> [x y]
#
# Clicks (x, y) -- by default the "只读 · 兼容性模式 · 已保存" part of the title bar,
# at 578,28 on a 1440-pixel-wide screen -- in a Word that is already open on
# the test display, <trials> times.  Each time popuptrace follows it for
# 1500 ms, the flyout is judged closed early if it is hidden again within that
# time or the foreground goes to the desktop window (#32769), and Esc closes
# it if it is still open.  Traces go to <out-dir>/trace-N.txt, verdicts to
# <out-dir>/summary.txt.
#
# Only for a test display (PROBE_DISPLAY, default :2; its XAUTHORITY is read
# from the Xwayland command line).  <wine-root> must be the Wine the Word was
# started with, and PROBE_PREFIX (default ~/.wine-c2r-up) its prefix.
set -u
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
[ $# -ge 3 ] || { sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
out=$1; trials=$2; root=$3; x=${4:-578}; y=${5:-28}
display=${PROBE_DISPLAY:-:2}
case "$display" in :0|:0.*) echo "not on the user's display $display" >&2; exit 2;; esac
xauth=${PROBE_XAUTHORITY:-$(ps -eo args | /usr/bin/grep -E "^/usr/bin/Xwayland $display " |
                            sed -n 's/.* -auth \([^ ]*\).*/\1/p' | head -1)}
mkdir -p "$out" || exit 2
E=(env -i HOME="$HOME" USER="${USER:-$(id -un)}" PATH=/usr/bin:/bin LANG="${LANG:-C.UTF-8}"
   XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" DISPLAY="$display" XAUTHORITY="$xauth"
   WAYLAND_DISPLAY=wine-altars-no-wayland WINEPREFIX="${PROBE_PREFIX:-$HOME/.wine-c2r-up}" WINEDEBUG=-all)
: > "$out/summary.txt"
for i in $(seq 1 "$trials"); do
    "${E[@]}" "$root/bin/wine" "$D/popuptrace.exe" "$x" "$y" 1500 > "$out/trace-$i.txt" 2>&1
    python3 -I - "$out/trace-$i.txt" "$i" <<'PY' | tee -a "$out/summary.txt"
import re, sys
show = hide = fg_desktop = flyout = None
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s*([\d.]+) (\w+)\s+hwnd=(\S+)(.*)', l)
    if not m: continue
    t, ev, h, rest = float(m.group(1)), m.group(2), m.group(3), m.group(4)
    r = re.search(r'rect=\((-?\d+),(-?\d+)\)-\((-?\d+),(-?\d+)\)', rest)
    if ev == 'SHOW' and 'class=Net UI Tool Window' in rest and flyout is None and r:
        x1, y1, x2, y2 = map(int, r.groups())
        if x2 - x1 > 200 and y2 - y1 > 150: flyout, show = h, t
    elif ev == 'HIDE' and h == flyout and hide is None: hide = t
    elif ev == 'FG' and 'class=#32769' in rest and fg_desktop is None: fg_desktop = t
early = hide is not None or fg_desktop is not None
print(f"trial={sys.argv[2]} shown_ms={show} hidden_ms={hide} fg_desktop_ms={fg_desktop} "
      f"closed_early={int(early) if show is not None else 'no-flyout'}")
PY
    "${E[@]}" "$root/bin/wine" "$R/tools/sendkeys/sendkeys.exe" -w OpusApp -d 0 esc > /dev/null 2>&1
    sleep 0.8
done
