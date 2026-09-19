#!/usr/bin/env bash
# Make the prefix's printers behave the way they would on Windows.
#
# Wine bridges CUPS queues into the prefix by itself and drives them through
# its PostScript driver, so printing works out of the box.  Two things it does
# not get right on a prefix that has been used already:
#
#   * The cached DevMode.  Wine writes one into the printer's registry key the
#     first time it enumerates the queue, and from then on that cached copy is
#     what applications get.  A prefix first enumerated under an English locale
#     therefore keeps Letter as its paper size forever, even after the locale
#     is right -- the code that consults LOCALE_IPAPERSIZE only runs when there
#     is no cached DevMode to use.  Clearing it makes Wine build a fresh one.
#
#   * The UI language.  Wine takes it from the prefix
#     (HKCU\Software\Wine\LC_MESSAGES), set by
#     scripts/install-start-menu.sh, so it no longer depends on who starts
#     Office.  This script reads the same value rather than inventing one.
#
# Those two interact, and getting it wrong is easy: the rebuilt DevMode takes
# its paper size from whatever locale the rebuild happens under.  Clearing the
# cache from an English shell simply writes Letter back.  So the regeneration
# runs under the prefix's own language, not the caller's.
#
# Nothing is invented: the DevMode is rebuilt from the queue's own PPD, and the
# key is exported first so the old one can be put back.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-wow64}"
WINE="$DIST/bin/wine"
: "${WINEPREFIX:?set WINEPREFIX}"
export WINEPREFIX WINEDEBUG=-all

# The prefix's own language, for the reason in the header; fall back to the
# same default install-start-menu.sh uses if it has not been set yet.
: "${WINE_UI_LANG:=$("$WINE" reg query 'HKCU\Software\Wine' /v LC_MESSAGES 2>/dev/null |
      grep -oP '(?<=LC_MESSAGES)\s+REG_SZ\s+\K.*' | tr -d '\r')}"
: "${WINE_UI_LANG:=zh_CN.UTF-8}"
export LANG="$WINE_UI_LANG"
export LANGUAGE="${WINE_UI_LANG%%.*}"
unset LC_ALL
BACKUP="$WINEPREFIX/printer-devmode-backup"
PRINTERS='HKLM\System\CurrentControlSet\Control\Print\Printers'

say() { printf '==> %s\n' "$*"; }

say "prefix 里的打印机"
mapfile -t names < <("$WINE" reg query "$PRINTERS" 2>/dev/null |
                     grep -oP '(?<=Printers\\).*' | tr -d '\r')
if [ "${#names[@]}" = 0 ]; then
    echo "    一台都没有。Wine 是从 CUPS 拿的，先确认 lpstat -p 有输出。"
    exit 0
fi
printf '    %s\n' "${names[@]}"

say "清除缓存的 DevMode（先备份）"
mkdir -p "$BACKUP"
for p in "${names[@]}"; do
    f="$BACKUP/$p.reg"
    "$WINE" reg export "$PRINTERS\\$p" "$(echo "$f" | sed 's|^/|Z:\\|; s|/|\\|g')" /y >/dev/null 2>&1
    if "$WINE" reg delete "$PRINTERS\\$p" /v 'Default DevMode' /f >/dev/null 2>&1; then
        printf '    %-36s 已清除（备份 %s 字节）\n' "$p" "$([ -s "$f" ] && stat -c%s "$f" || echo 0)"
    else
        printf '    %-36s 没有缓存，跳过\n' "$p"
    fi
done
"$DIST/bin/wineserver" -k >/dev/null 2>&1 || true
sleep 2

say "重新生成后的默认设置"
# The probe reports each queue's DevMode, which is what an application sees.
if [ -x "$ROOT/tools/printprobe/printprobe.exe" ]; then
    "$WINE" "$ROOT/tools/printprobe/printprobe.exe" 2>/dev/null |
        grep -E '^\s+(纸张|===)' | sed 's/^/    /'
else
    echo "    （tools/printprobe 没编译，跳过验证）"
fi

say "界面语言（取自 prefix：$WINE_UI_LANG）"
msg=$("$WINE" reg query 'HKCU\NoSuchKeyHere' 2>&1 | head -1)
case "$msg" in
    *Unable*) echo "    !! 仍是英文资源——检查 HKCU\Software\Wine\LC_MESSAGES 与 locale -a" ;;
    *)        echo "    Wine 已使用本地化资源" ;;
esac
say "done"
