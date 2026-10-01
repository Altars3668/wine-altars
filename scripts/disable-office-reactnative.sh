#!/usr/bin/env bash
# Keep Office's React Native host out of the prefix until Wine can host it.
#
# About a minute and a half into every Word session, Office loads
# react-native-win32.dll and that host asks the runtime for
# Windows.Web.Http.Filters.HttpBaseProtocolFilter and
# Windows.Networking.Sockets.MessageWebSocket.  Wine has neither, so
# RoGetActivationFactory returns REGDB_E_CLASSNOTREG, C++/WinRT throws
# hresult_class_not_registered, and nothing in React Native catches it:
# terminate, abort, and then Office's own crash handler writing to NULL on
# purpose to force a dump.  Under Wine that access violation neither kills the
# process nor resolves -- it is re-dispatched forever, about a hundred thousand
# times a second, burning a whole core for as long as the app stays up.  Every
# window in that process then feels frozen; opening the font dropdown is just
# the first thing most people try.
#
# Until those two runtime classes exist, the cheapest correct answer is to not
# load the host at all.  Wine's DllOverrides with an empty value makes
# LoadLibrary fail for that one DLL, and Office treats it as an unavailable
# optional component: measured over four minutes, CPU stays at 0-1%, the font
# dropdown opens and closes normally, and the app stays responsive.
#
# What it costs: the Office surfaces built on React Native do not appear.  They
# do not work today either -- they take the whole application down with them.
#
# 2026-09-21: that last paragraph no longer holds on every build.  Measured on
# ~/.wine-altars-office with Office 16.0.20208, disabling the host does not
# leave Word with fewer features -- it leaves Word with no interface at all:
# MSAA reports a single object and the screen keeps an empty frame with a
# shadowed transparent edge.  React Native is load-bearing for the window
# itself in that build, not an optional extra, so this is not a workaround
# there and the override was undone again.
#
# What it is still useful for is telling the two apart: if disabling the host
# changes nothing, React Native was not what was failing.
#
#     scripts/disable-office-reactnative.sh            # apply
#     scripts/disable-office-reactnative.sh --undo     # put it back
#
# Undoing it restores the 100% CPU behaviour, so undo once Wine grows those
# classes (dlls/coremessaging already gained Windows.System.DispatcherQueue,
# which was the first of the three this host asks for).
set -uo pipefail

PREFIX="${WINEPREFIX:-$HOME/.wine-c2r-up}"
DLL="react-native-win32"
KEY='HKCU\Software\Wine\DllOverrides'

export WINEPREFIX="$PREFIX"
say() { printf '==> %s\n' "$*"; }

if [ "${1:-}" = --undo ]; then
    say "恢复 $DLL 的加载（$PREFIX）"
    wine reg delete "$KEY" /v "$DLL" /f >/dev/null 2>&1
    wine reg query "$KEY" 2>/dev/null | grep -qi "$DLL" \
        && { echo "    没删掉" >&2; exit 1; }
    echo "    已恢复；Office 会重新加载 React Native 宿主，那个满载循环也会回来"
    exit 0
fi

say "在 $PREFIX 里禁止加载 $DLL.dll"
wine reg add "$KEY" /v "$DLL" /t REG_SZ /d '' /f >/dev/null 2>&1

if wine reg query "$KEY" 2>/dev/null | grep -qi "$DLL"; then
    echo "    已设置"
    echo
    echo "    生效前请先关掉正在运行的 Office："
    echo "        for p in \$(pgrep -x WINWORD.EXE); do kill \$p; done"
    echo
    echo "    撤销：$0 --undo"
else
    echo "    写入失败" >&2
    exit 1
fi
