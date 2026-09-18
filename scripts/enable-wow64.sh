#!/usr/bin/env bash
# Let an existing prefix run 32-bit programs, using a Wine built with WoW64.
#
# A prefix copied from a Windows install carries Windows' own syswow64 -- real
# Microsoft binaries, hundreds of them.  Wine cannot load those: they are not
# its own runtime, and the loader fails at the first one it needs:
#
#     wine: failed to load L"\\??\\C:\\windows\\syswow64\\ntdll.dll" error c0000135
#
# So the directory has to step aside and let wineboot lay down Wine's 32-bit
# runtime in its place.  Nothing of the user's lives there -- it is the system
# directory for 32-bit code, and on a 64-bit Office prefix nothing had been able
# to use it anyway.  It is moved, not deleted.
#
# This does not itself build anything: point WINE at a Wine configured with
# ARCHS=i386,x86_64 (see scripts/build-wine.sh), or the script stops and says so.
set -euo pipefail

WINE="${WINE:?set WINE to the wow64-capable wine}"
: "${WINEPREFIX:?set WINEPREFIX}"
export WINEPREFIX

C="$WINEPREFIX/drive_c"
SYSWOW="$C/windows/syswow64"
STASH="$WINEPREFIX/syswow64-from-windows"

libdir="$(cd "$(dirname "$WINE")/../lib/wine" 2>/dev/null && pwd || true)"
if [ -z "$libdir" ] || [ ! -d "$libdir/i386-windows" ]; then
    echo "这个 Wine 没有 32 位支持（缺 lib/wine/i386-windows）" >&2
    echo "请用 ARCHS=i386,x86_64 重新构建，见 scripts/build-wine.sh" >&2
    exit 1
fi
echo "==> Wine: $("$WINE" --version) （带 WoW64）"

# Windows' own binaries are recognisable by what Wine's are not: a Wine build
# of ntdll.dll names itself in the file.  Only move the directory aside when it
# is Windows', so re-running after the switch is a no-op.
if [ -f "$SYSWOW/ntdll.dll" ] && ! grep -qa "Wine" "$SYSWOW/ntdll.dll" 2>/dev/null; then
    [ -e "$STASH" ] && { echo "已有 $STASH，先处理它再重试" >&2; exit 1; }
    n=$(ls "$SYSWOW" | wc -l)
    mv "$SYSWOW" "$STASH"
    echo "==> 移出 Windows 自带的 syswow64（$n 个文件）-> $STASH"
else
    echo "==> syswow64 已是 Wine 的，或不存在"
fi

echo "==> wineboot -u（重建 32 位运行时）"
WINEDEBUG=-all "$WINE" wineboot -u >/dev/null 2>&1 || true

if [ -f "$SYSWOW/ntdll.dll" ]; then
    echo "==> syswow64 已由 Wine 重建（$(ls "$SYSWOW" | wc -l) 个文件）"
else
    echo "!! wineboot 没有建出 syswow64" >&2
    exit 1
fi

# Prove it, rather than assume: run a 32-bit program Wine itself shipped.
if [ -f "$SYSWOW/winver.exe" ]; then
    if WINEDEBUG=-all timeout 60 "$WINE" "$SYSWOW/winver.exe" >/dev/null 2>&1; then
        echo "==> 32 位程序可以运行"
    else
        echo "==> 32 位程序启动了但很快退出（无显示时正常）"
    fi
fi
echo "==> done"
