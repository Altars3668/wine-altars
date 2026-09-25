#!/bin/bash
# word-iter.sh <log> [WINEDEBUG] [dll...]
#
# One turn of the loop the composition work ran on: deploy the named DLLs from
# the build tree into the installed Wine, start Word on the measuring desktop
# (:2, scripts/measure-desktop.sh), get it past the safe-mode and recovery
# questions, wait for its window, and summarize what dcomp and seh said.
#
#   scripts/word-iter.sh /tmp/word.log                          # dcomp, default channels
#   scripts/word-iter.sh /tmp/word.log +dcomp,err+all dcomp d3d11
#
# Kills whatever runs in the Office prefix first.  Never pipe its output into
# something that exits early: the wineserver the first step starts inherits the
# pipe, and the command then waits for it.
#
# The DLLs are replaced by a copy and a rename, not written over: writing over a
# DLL that a running Office process has mapped changes its pages under it and
# brings it down, while a rename leaves it the old file.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
log=$1; shift
debug=warn+dcomp,fixme+dcomp,warn+seh,err+all
if [ $# -gt 0 ]; then debug=$1; shift; fi
dlls=("$@"); [ ${#dlls[@]} -eq 0 ] && dlls=(dcomp)
build=$here/wine-src/build-wow64
prefix=$HOME/.wine-c2r-test
wine=/opt/wine-altars

WINEPREFIX=$prefix $wine/bin/wineserver -k 2>/dev/null
for dll in "${dlls[@]}"; do
    for arch in x86_64 i386; do
        src=$build/dlls/$dll/$arch-windows/$dll.dll; dst=$wine/lib/wine/$arch-windows/$dll.dll
        cp "$src" "$dst.new" && mv -f "$dst.new" "$dst"
    done
done
# The wineserver, and the session's explorer with it, take their display from
# whoever starts them: start them on :2 too, or the desktop, the clipboard and
# the screen size Wine believes in all come from some other display.
xauth=$(pgrep -a Xwayland | awk '$3 == ":2" { for (i = 1; i <= NF; i++) if ($i == "-auth") print $(i + 1) }' | head -1)
DISPLAY=:2 XAUTHORITY=$xauth WINEPREFIX=$prefix WINEDEBUG=-all timeout 90 $wine/bin/wine regsvr32 /s dcomp.dll
cd "$here"
OFFICE_DEBUG_DISPLAY=2 scripts/office-debug.sh start "$debug" "$log" >/dev/null
for i in $(seq 1 60); do
    w=$(OFFICE_DEBUG_DISPLAY=2 scripts/office-debug.sh windows 2>/dev/null)
    if echo "$w" | /usr/bin/grep -q ' #32770 .*"Microsoft Word"'; then
        OFFICE_DEBUG_DISPLAY=2 scripts/office-debug.sh click '否(N)' '#32770' >/dev/null 2>&1
        echo "declined safe mode"
    fi
    if echo "$w" | /usr/bin/grep -q ' NUIDialog .*"Microsoft Word"'; then
        OFFICE_DEBUG_DISPLAY=2 scripts/office-debug.sh click '否' NetUIHWND >/dev/null 2>&1
        echo "declined a question"
    fi
    echo "$w" | /usr/bin/grep -q ' OpusApp ' && break
    sleep 2
done
sleep 20
OFFICE_DEBUG_DISPLAY=2 scripts/office-debug.sh windows 2>&1 | head -4
echo "--- dcomp and exceptions"
/usr/bin/grep -a ':dcomp:\|EXCEPTION_ACCESS_VIOLATION\|Failed to find library' "$log" | cut -c1-240 | sort | uniq -c | sort -rn | head -40
