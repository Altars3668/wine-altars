#!/usr/bin/env bash
# Create the Office prefix with wine-altars and put it in Windows 11 mode.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
export PATH="$ROOT/dist/bin:$PATH"
export WINEARCH=win64
# Mono and Gecko dialogs have nothing to do with Office and block a script.
export WINEDLLOVERRIDES="mscoree=d;mshtml=d"

command -v wine >/dev/null || { echo "build first: scripts/build-wine.sh" >&2; exit 1; }
[ "$(wine --version)" ] && echo "using $(command -v wine) — $(wine --version)"

if [ -e "$WINEPREFIX/system.reg" ] && [ "${FORCE:-0}" != 1 ]; then
    echo "prefix already exists at $WINEPREFIX (FORCE=1 to recreate)"
else
    rm -rf "$WINEPREFIX"
    wineboot -u
    wineserver -w
fi

wine winecfg -v win11
wineserver -w

# Office ships Microsoft's own VC runtimes in root/vfs/System; they are more
# complete than Wine's and import-office.sh puts them in system32. Tell Wine to
# prefer them. This is what winetricks' vcrun2015+ does, minus the download.
for d in msvcp140 msvcp140_1 msvcp140_2 msvcp140_atomic_wait msvcp140_codecvt_ids \
         vcruntime140 vcruntime140_1 vcruntime140_threads concrt140 vccorlib140 \
         mfc140 mfc140u mfcm140 mfcm140u atl100 atl110 msvcp100 msvcp110 msvcr100 msvcr110; do
    wine reg add 'HKCU\Software\Wine\DllOverrides' /v "$d" /t REG_SZ /d "native,builtin" /f >/dev/null 2>&1
done
wineserver -w
echo "prefix ready: $WINEPREFIX"
