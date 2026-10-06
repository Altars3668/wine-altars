#!/bin/bash
# dropdowns.sh <out-dir> <wine-root>
#
# Opens the print page (Ctrl+P) of a Word that is already open on the test
# display and clicks each of its five dropdowns three times -- printer, pages,
# sides, collation, pages per sheet, at the positions they have on a
# 1440-pixel-wide screen -- following each click for 3000 ms with popuptrace.
# A dropdown that came up is closed with Esc, which goes to it because it is
# in the foreground; when none came up nothing is pressed, so the print page
# stays.  Never clicks "打印" and never sends Enter.  Verdicts go to
# <out-dir>/summary.txt: how many ms from the click until the list showed.
#
# Same environment as flyout-trials.sh (PROBE_DISPLAY, PROBE_PREFIX).
set -u
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
[ $# -ge 2 ] || { sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
out=$1; root=$2
display=${PROBE_DISPLAY:-:2}
case "$display" in :0|:0.*) echo "not on the user's display $display" >&2; exit 2;; esac
xauth=${PROBE_XAUTHORITY:-$(ps -eo args | /usr/bin/grep -E "^/usr/bin/Xwayland $display " |
                            sed -n 's/.* -auth \([^ ]*\).*/\1/p' | head -1)}
mkdir -p "$out" || exit 2
E=(env -i HOME="$HOME" USER="${USER:-$(id -un)}" PATH=/usr/bin:/bin LANG="${LANG:-C.UTF-8}"
   XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" DISPLAY="$display" XAUTHORITY="$xauth"
   WAYLAND_DISPLAY=wine-altars-no-wayland WINEPREFIX="${PROBE_PREFIX:-$HOME/.wine-c2r-up}" WINEDEBUG=-all)
W="$root/bin/wine"

"${E[@]}" "$W" "$R/tools/sendkeys/sendkeys.exe" -w OpusApp -d 0 ctrl+p > /dev/null 2>&1
sleep 6
for target in printer:328:252 pages:328:350 sides:328:425 collate:328:472 perpage:328:519; do
    name=${target%%:*}; xy=${target#*:}; x=${xy%%:*}; y=${xy#*:}
    for i in 1 2 3; do
        "${E[@]}" "$W" "$D/popuptrace.exe" "$x" "$y" 3000 > "$out/dd-$name-$i.txt" 2>&1
        python3 -I - "$out/dd-$name-$i.txt" "$name" "$i" <<'PY'
import re, sys
show = None
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s*([\d.]+) (\w+)\s+hwnd=(\S+)(.*)', l)
    if m and m.group(2) == 'SHOW' and 'Net UI Tool Window' in m.group(4):
        show = float(m.group(1)); break
print(f'{sys.argv[2]:8s} trial {sys.argv[3]}: shown_ms={show}')
PY
        if tail -n +2 "$out/dd-$name-$i.txt" | /usr/bin/grep ' FG ' | tail -1 | /usr/bin/grep -q 'Net UI Tool Window'; then
            "${E[@]}" "$W" "$R/tools/sendkeys/sendkeys.exe" -d 0 esc > /dev/null 2>&1
        fi
        sleep 1
    done
done | tee "$out/summary.txt"
