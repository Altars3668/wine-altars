#!/usr/bin/env bash
# Start Word N times and report, for each start, what actually reached the
# screen.
#
# The rendering faults this project chases are not deterministic: Word's title
# bar and Backstage nav rail came up solid black on roughly one start in three,
# and a single good run proves nothing about a change. So this runs the same
# start repeatedly and prints one line each -- distinct colours, black pixels,
# and any row/column band that is at least 90% pure black, which is the shape
# those two faults take.
#
# It also reports the two regions this project has caught failing on their own
# -- Word's title bar (rows 0..46) and its Backstage navigation rail (columns
# 0..64) -- as distinct-colour counts, because "blank" and "black" are
# different failures with the same look in a screenshot and only the colour
# count tells them apart quickly.  A painted rail is dozens of colours; 2 is
# background plus its separator line.
#
#   eval "$(scripts/measure-desktop.sh start)"
#   scripts/render-census.sh 6
#
# Needs a display Word can render on; scripts/measure-desktop.sh makes one when
# there is no session to use.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-cx}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
N="${1:-5}"
SETTLE="${SETTLE:-12}"

[ -n "${DISPLAY:-}" ] || { echo "no DISPLAY" >&2; exit 1; }

for i in $(seq 1 "$N"); do
    pkill -x WINWORD.EXE 2>/dev/null
    sleep 3
    WINEDEBUG=-all "$ROOT/scripts/run-word.sh" >/dev/null 2>&1 &
    for _ in $(seq 1 60); do
        WINEDEBUG=-all "$DIST/bin/wine" "$ROOT/tools/winenum/winenum.exe" 2>/dev/null \
            | grep -q OpusApp && break
        sleep 4
    done
    sleep "$SETTLE"
    # The offscreen client window Wine XComposite-redirects Office's swapchain
    # into: the one place the app's own pixels can be read without the
    # compositor in the way.
    win=$(xwininfo -root -tree 2>/dev/null | awk '/1438x808/ {print $1; exit}')
    if [ -z "$win" ]; then
        printf 'run %d: no window\n' "$i"
        continue
    fi
    # Two regions this project has caught failing on their own: Word's title
    # bar (rows 0..46) and its Backstage navigation rail (columns 0..64).
    # "Blank" and "black" look the same in a screenshot and the colour count
    # separates them at a glance -- a painted rail is dozens of colours, 2 is
    # the background plus its separator line.
    rail=$(python3 "$ROOT/scripts/xwin-pixels.py" "$win" --region 0,48,66,760 \
           | sed -n 's/.*rgb: \([0-9]*\) distinct.*/\1/p')
    title=$(python3 "$ROOT/scripts/xwin-pixels.py" "$win" --region 0,0,1438,47 \
            | sed -n 's/.*rgb: \([0-9]*\) distinct.*/\1/p')
    printf 'run %d: rail=%-5s title=%-5s ' "$i" "$rail" "$title"
    python3 "$ROOT/scripts/xwin-pixels.py" "$win" | tail -n +2 | grep -v alpha: \
        | tr '\n' ' ' | sed 's/  */ /g'
    printf '\n'
done
pkill -x WINWORD.EXE 2>/dev/null
