#!/usr/bin/env bash
# Forget a printer's stored settings so they are worked out again.
#
# wineps.drv used to apply the PPD's default paper size on top of the stored
# devmode every time the printer was opened, so a size chosen once -- by the
# locale or by hand -- was undone on the next print.  That is fixed, but a
# prefix that printed under the old behaviour still has the wrong size sitting
# in the registry, and nothing rewrites it on its own.
#
# Run this once per affected printer, in the locale the applications run in:
# the paper size is picked from it when there is nothing stored.
#
#     scripts/reset-printer-devmode.sh                 # every printer
#     scripts/reset-printer-devmode.sh <printer name>  # just one
set -uo pipefail

PREFIX="${WINEPREFIX:-$HOME/.wine-c2r-up}"
KEY='HKLM\System\CurrentControlSet\Control\Print\Printers'

export WINEPREFIX="$PREFIX"
unset LC_ALL
export LANG="${LANG:-zh_CN.UTF-8}"

say() { printf '==> %s\n' "$*"; }

if pgrep -x WINWORD.EXE >/dev/null || pgrep -x EXCEL.EXE >/dev/null; then
    echo "先关掉 Office，它会在退出时把设置写回去" >&2
    exit 1
fi

if [ $# -ge 1 ]; then
    printers=("$1")
else
    mapfile -t printers < <(wine reg query "$KEY" 2>/dev/null |
        sed -n "s|.*\\\\Printers\\\\\\(.*\\)|\\1|p" | tr -d '\r')
fi

[ ${#printers[@]} -eq 0 ] && { echo "前缀里没有打印机" >&2; exit 1; }

say "语言环境 LANG=$LANG（没有存储值时按它选纸张）"
for p in "${printers[@]}"; do
    [ -z "$p" ] && continue
    wine reg delete "$KEY\\$p" /v "Default DevMode" /f >/dev/null 2>&1
    echo "    $p：已清除"
done

say "下一次打开打印机时重建"
