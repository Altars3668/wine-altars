#!/usr/bin/env bash
# Start an Office application the way a desktop launcher needs to.
#
# Three things have to be right for Office to behave like a native app, and a
# bare `wine WINWORD.EXE` gets none of them:
#
#   * The UI language.  Wine picks its resources from the process locale, and
#     on a host running en_US that gives you a Chinese Word wrapped in English
#     Wine dialogs -- the print properties sheet being the one people actually
#     hit.  It has to be LANG: LC_MESSAGES alone does not move Wine, which
#     derives the Windows user locale from setlocale(LC_ALL, "") rather than
#     from the message category.  Setting the whole locale is also the more
#     faithful thing to do here, since it is what a Chinese Windows install
#     would report to Office anyway.
#
#   * The file argument.  A desktop file hands over a Unix path; Office needs a
#     Windows one.  winepath does the conversion, and doing it here means the
#     desktop entry stays a plain `%F`.
#
#   * The process.  exec, not a wrapper that lingers: the window manager
#     matches the window to the launcher by WM_CLASS, and an extra shell in
#     between is one more process to leave behind when Office exits.
#
# Usage: office-launch.sh <word|excel|powerpoint|...> [file...]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-wow64}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
WINE="$DIST/bin/wine"
OFFICE='C:\Program Files\Microsoft Office\root\Office16'

# Override with WINE_UI_LANG=C.UTF-8 to see Wine's own English strings.
: "${WINE_UI_LANG:=zh_CN.UTF-8}"
export LANG="$WINE_UI_LANG"
export LANGUAGE="${WINE_UI_LANG%%.*}"
unset LC_ALL          # a host LC_ALL would override LANG and undo the above

case "${1:-}" in
    word)        exe=WINWORD.EXE  ;;
    excel)       exe=EXCEL.EXE    ;;
    powerpoint)  exe=POWERPNT.EXE ;;
    outlook)     exe=OUTLOOK.EXE  ;;
    onenote)     exe=ONENOTE.EXE  ;;
    access)      exe=MSACCESS.EXE ;;
    publisher)   exe=MSPUB.EXE    ;;
    visio)       exe=VISIO.EXE    ;;
    *) echo "用法: $(basename "$0") <word|excel|powerpoint|outlook|onenote|access|publisher|visio> [文件...]" >&2
       exit 2 ;;
esac
shift

args=()
for f in "$@"; do
    # A path that is already a Windows one is passed through untouched; this is
    # what happens when something re-invokes the launcher with its own argv.
    case "$f" in
        [A-Za-z]:\\*) args+=("$f") ;;
        *) w=$("$WINE" winepath -w "$f" 2>/dev/null); args+=("${w:-$f}") ;;
    esac
done

exec "$WINE" "$OFFICE\\$exe" "${args[@]}"
