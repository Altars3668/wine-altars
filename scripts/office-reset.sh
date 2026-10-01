#!/usr/bin/env bash
# Clear a stuck Office in this prefix, for when clicking the icon does nothing.
#
# Office is single-instance: a second launch hands the request to the instance
# already running and exits. If that instance is on a display you are not
# looking at -- left behind by a crash, a remote session that went away, or a
# test run on a spare display -- every later click is silently swallowed, and
# Word offers safe mode on the next try because it sees launches that produced
# no window. Nothing on screen says any of this.
#
# So: kill everything this prefix is running, and clear the resiliency record
# that the failed launches left behind.
#
# pkill is used by process name (-x) and never by command line (-f), because -f
# matches the caller's own arguments -- this script's name contains every
# pattern it searches for, and an -f would kill the shell running it.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-/opt/wine-altars}"
[ -x "$DIST/bin/wine" ] || DIST="$ROOT/dist-up"
WINE="${WINE:-$DIST/bin/wine}"
: "${WINEPREFIX:=$HOME/.wine-c2r-up}"
export WINEPREFIX WINEDEBUG=-all

say() { printf '==> %s\n' "$*"; }

say "停掉这个 prefix 里的所有进程"
timeout 60 "$DIST/bin/wineserver" -k >/dev/null 2>&1 || true
sleep 2
for name in WINWORD.EXE EXCEL.EXE POWERPNT.EXE OUTLOOK.EXE ONENOTE.EXE \
            MSACCESS.EXE MSPUB.EXE VISIO.EXE msedgewebview2.exe \
            mathtypelib.exe MathType.exe AxMath.exe \
            wineserver wineboot.exe winedevice.exe services.exe plugplay.exe \
            rpcss.exe explorer.exe winemenubuilder.exe; do
    pkill -9 -x "$name" 2>/dev/null
done
sleep 2
left=$(ps -eo comm 2>/dev/null | grep -cE '^(wine|WINWORD|EXCEL|POWERPNT)' || true)
echo "    残留 ${left:-0} 个"

say "清掉失败启动留下的痕迹"
# Word decides to offer safe mode from this key; a run that never painted a
# window counts as a failure, so a swallowed click leaves it behind.
for app in Word Excel PowerPoint Outlook; do
    "$WINE" reg delete "HKCU\\Software\\Microsoft\\Office\\16.0\\$app\\Resiliency" /f >/dev/null 2>&1 \
        && echo "    $app Resiliency"
done
find "$WINEPREFIX/drive_c/users" -name '~$*.dot?' -delete 2>/dev/null || true
echo "    文档锁文件"

timeout 60 "$DIST/bin/wineserver" -k >/dev/null 2>&1 || true
say "done —— 现在从菜单点击即可正常启动"
