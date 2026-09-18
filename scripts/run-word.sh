#!/usr/bin/env bash
# Start Word in the prefix from a clean slate.
#
# Word decides "the last run failed, offer safe mode" from something that
# survives this cleanup: the per-session %TEMP% directories and the
# Resiliency key are both cleared below and the prompt still appears on a
# noticeable fraction of runs anyway (measured: roughly one run in three,
# matching this project's long-standing "Word sometimes shows no window"
# report almost exactly -- an unattended run that hits this prompt looks
# exactly like a hang, since nothing answers it). What actually flags an
# unclean exit was not found this session: it is not in the Word registry
# tree (`wine reg export` before and after a killed run diffs to nothing but
# AddInLoadTimes noise) and Wine's own RegisterApplicationRestart /
# RegisterApplicationRecoveryCallback are stub no-ops that keep no state, so
# it is neither of the obvious places. `wine taskkill` (a real WM_CLOSE)
# instead of killing the process from the host seemed to help in a couple of
# quick trials but was not isolated cleanly under time pressure -- worth
# a slower, more careful pass before trusting it.
#
# Until the real trigger is found, this script cleans up what it knows about
# and then answers the dialog if it still appears, rather than let an
# automated run sit there indefinitely looking like a hang.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-cx}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
export PATH="$DIST/bin:$PATH"
LOG="${LOG:-/tmp/word.log}"
APP="${APP:-C:\\Program Files\\Microsoft Office\\root\\Office16\\WINWORD.EXE}"

"$DIST/bin/wineserver" -k 2>/dev/null
for _ in 1 2 3; do sleep 1; done

TEMP_DIR="$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp"
n=0
for d in "$TEMP_DIR"/{*-*-*-*-*}/; do
    [ -d "$d" ] || continue
    rm -rf "$d" && n=$((n+1))
done
rm -f "$TEMP_DIR"/*.log
echo "cleared $n stale Office session dirs"

WINEDEBUG=-all "$DIST/bin/wine" reg delete \
    "HKCU\\Software\\Microsoft\\Office\\16.0\\Word\\Resiliency" /f >/dev/null 2>&1
WINEDEBUG=-all "$DIST/bin/wine" reg add "HKCU\\Software\\Wine\\WineDbg" \
    /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1

# Best-effort: if the safe-mode prompt shows up anyway, click "No" (continue
# normally) so a scripted run does not sit blocked behind it looking hung.
# The dialog is a plain #32770 messagebox -- real Win32 child controls, so a
# coordinate click works, unlike the NetUI-drawn dialogs elsewhere in Office
# that need MSAA (tools/uiclick).
#
# It must be told apart from the licensing dialog by *class*, not by size. The
# earlier version of this used the window's reported geometry (250<W<450,
# 60<H<200) on the reasoning that Office's own dialogs are larger. They are
# not: the licensing NUIDialog measures exactly 375x178 and sits squarely
# inside that box, so this loop was firing a blind click into it on every run
# where it appeared -- which is every run. Since pressing that dialog's OK
# makes Word exit cleanly (measured: exit status 0), an automatic click there
# is not a harmless miss; it ends the session and looks exactly like a crash
# from outside. Ask winenum for the Win32 class instead, and act only on
# #32770.
#
# Set WORD_NO_AUTOCLICK=1 to disable this entirely, which is what any
# measurement of what Word does when left alone needs.
if [ "${WORD_NO_AUTOCLICK:-0}" != 1 ] && [ -n "${DISPLAY:-}" ]; then
    (
        UICLICK="$ROOT/tools/uiclick/uiclick.exe"
        WINENUM="$ROOT/tools/winenum/winenum.exe"
        [ -f "$UICLICK" ] && [ -f "$WINENUM" ] || exit 0
        for _ in $(seq 1 40); do
            if WINEDEBUG=-all "$DIST/bin/wine" "$WINENUM" 2>/dev/null \
               | grep -q '#32770 .*vis=1'; then
                # uiclick presses it through MSAA, falling back to a click at
                # the location the object itself reports. The previous version
                # computed +203,+81 from the window origin, which lands at
                # (994,554) while the No button is at (971,570)-(1023,596) --
                # it was missing, which is most of why this prompt looked
                # unanswerable and start-ups looked hung.
                WINEDEBUG=-all "$DIST/bin/wine" "$UICLICK" No '#32770' >/dev/null 2>&1
                exit 0
            fi
            sleep 2
        done
    ) &
    disown
fi

export WINEDEBUG="${WINEDEBUG:--all,+loaddll,err+all}"
echo "log -> $LOG"
exec "$DIST/bin/wine" "$@" "$APP"
