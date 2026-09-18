#!/usr/bin/env bash
# Push freshly built builtins into the prefix, so what runs is what was built.
#
# Since Wine 9 a prefix's C:\windows\system32 holds real copies of the builtin
# PE dlls rather than placeholders, and the loader takes them from there. So
# "make install" updates dist-cx and changes nothing about what Word loads --
# silently, with no error and no missing file, which is the expensive kind of
# wrong: a measurement then describes whichever build the prefix was made from.
# One dll here was four hours and two edits behind its source, and the log it
# produced was read as a finding about the code on disk.
#
# Only files the prefix already has, and only where its copy is itself a Wine
# builtin: import-office.sh deliberately puts Microsoft's own VC runtimes in
# system32 with native overrides, and those must survive.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-cx}"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
SYS="$WINEPREFIX/drive_c/windows/system32"

[ -d "$SYS" ]  || { echo "no prefix at $WINEPREFIX" >&2; exit 1; }
[ -d "$DIST" ] || { echo "no build at $DIST (set DIST=)" >&2; exit 1; }

n=0 kept=0
# winex11.drv is a builtin PE module too, just one of the handful Wine names
# without a .dll suffix -- the plain *.dll glob below silently never saw it,
# so its system32 copy sat two days stale while every renderer patch tested
# against dist-cx's fresher one instead. Named explicitly rather than
# widening the glob to *, which would also try to sync .exe/.sys/.a files
# that do not belong in this mechanism at all.
for built in "$DIST"/lib/wine/x86_64-windows/*.dll "$DIST"/lib/wine/x86_64-windows/winex11.drv; do
    [ -e "$built" ] || continue
    name="$(basename "$built")"
    live="$SYS/$name"
    [ -e "$live" ] || continue
    cmp -s "$built" "$live" && continue
    if ! grep -aqs 'Wine builtin DLL' "$live"; then
        printf '  keeping native  %s\n' "$name"; kept=$((kept+1)); continue
    fi
    cp -f "$built" "$live"
    printf '  updated         %s\n' "$name"; n=$((n+1))
done
printf '%d builtin(s) refreshed, %d native override(s) left alone\n' "$n" "$kept"
