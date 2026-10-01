#!/usr/bin/env bash
# Make this Wine the one the system runs, without touching the packaged one.
#
# A desktop entry names a Wine; a machine may have several.  Here the WineHQ
# package owns /usr/bin/wine -> /opt/wine-stable, and a prefix built against
# this tree cannot run under it: Office dies before it paints anything and the
# next launch offers safe mode, with nothing in between to say why.
#
# So this installs a copy under /opt/wine-altars and puts it ahead of the
# package on PATH through /usr/local/bin, which precedes /usr/bin.  Every tool
# in its bin/ is linked, not only wine and wineserver: a winecfg or wineboot
# left to the package runs another Wine against the same prefix, and its
# wineserver protocol does not match.  No file belonging to winehq-stable is
# touched, apt has nothing to fight over, and undoing it is removing the
# symlinks (--uninstall).
#
# A copy, not a symlink into the build tree: pointing the whole system at a git
# working directory means a rebuild or a stray clean takes every Wine
# application down with it.  Re-run this after rebuilding to refresh the copy.
# The copy it replaces is kept whole as /opt/wine-altars.prev; --rollback puts
# it back.
#
# What it installs is the altars-up build (dist-up): Wine master with this
# project's commits on top, no CrossOver changes.  Every Wine application on
# the machine, not just Office, starts using it, and each prefix is updated on
# its next start.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-up}"
TARGET="${TARGET:-/opt/wine-altars}"
PREV="$TARGET.prev"
LINKDIR="${LINKDIR:-/usr/local/bin}"

say() { printf '==> %s\n' "$*"; }

# point LINKDIR at every tool in TARGET/bin, and drop links into TARGET that lead nowhere now
link_tools() {
    local n=0 f l
    for f in "$TARGET"/bin/*; do
        [ -x "$f" ] || continue
        sudo ln -sfn "$f" "$LINKDIR/$(basename "$f")" && n=$((n+1))
    done
    for l in "$LINKDIR"/*; do
        [ -L "$l" ] && [ ! -e "$l" ] || continue
        case "$(readlink "$l")" in "$TARGET"/*) sudo rm -f "$l" ;; esac
    done
    echo "    $n 个链接"
}

# a wineserver of the installed Wine still running would refuse every client of the new one
check_idle() {
    local p exe busy=0
    for p in $(pgrep -x wineserver); do
        exe=$(readlink -f "/proc/$p/exe" 2>/dev/null)
        case "$exe" in "$TARGET"/*|"$PREV"/*)
            echo "    还在用：pid $p $(tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -m1 '^WINEPREFIX=')" >&2
            busy=1 ;;
        esac
    done
    if [ $busy = 1 ]; then
        echo "    先关掉这些前缀里的程序，再 WINEPREFIX=<前缀> $TARGET/bin/wineserver -k" >&2
        exit 1
    fi
}

if [ "${1:-}" = --uninstall ]; then
    say "移除覆盖（包自带的 /usr/bin/wine 一直没动过）"
    n=0
    for l in "$LINKDIR"/*; do
        [ -L "$l" ] || continue
        case "$(readlink -f "$l")" in "$TARGET"/*) sudo rm -f "$l" && n=$((n+1)) ;; esac
    done
    echo "    移除 $n 个链接；$TARGET 保留，删不删随你"
    command -v wine >/dev/null && echo "    现在 wine -> $(readlink -f "$(command -v wine)")"
    exit 0
fi

if [ "${1:-}" = --rollback ]; then
    [ -x "$PREV/bin/wine" ] || { echo "没有 $PREV 可回滚" >&2; exit 1; }
    check_idle
    say "换回 $PREV（$("$PREV/bin/wine" --version 2>/dev/null)）"
    sudo rm -rf "$TARGET.new"
    sudo mv "$TARGET" "$TARGET.new" && sudo mv "$PREV" "$TARGET" && sudo mv "$TARGET.new" "$PREV"
    link_tools
    echo "    刚才那份留在 $PREV"
    exit 0
fi

[ -x "$DIST/bin/wine" ] || { echo "先构建并安装到 $DIST" >&2; exit 1; }
check_idle

say "安装 $("$DIST/bin/wine" --version 2>/dev/null) 到 $TARGET"
if [ -d "$TARGET" ]; then
    echo "    原来的 $("$TARGET/bin/wine" --version 2>/dev/null) 留作 $PREV"
    sudo rm -rf "$PREV"
    sudo mv "$TARGET" "$PREV"
fi
sudo mkdir -p "$TARGET"
sudo chown "$(id -u):$(id -g)" "$TARGET"
cp -a "$DIST/." "$TARGET/"
echo "    $(du -sh "$TARGET" | cut -f1)"

# Wine finds its own libraries relative to the loader, so a copy runs from
# anywhere -- but check rather than assume, before anything points at it.
say "先验证再切换"
ver=$("$TARGET/bin/wine" --version 2>/dev/null | head -1)
[ -n "$ver" ] || { echo "    $TARGET/bin/wine 跑不起来，不做切换；--rollback 换回原来的" >&2; exit 1; }
echo "    $ver"

say "把 $LINKDIR 指过去"
link_tools

hash -r 2>/dev/null || true
say "结果"
for t in wine wineserver winecfg wineboot; do
    printf '    %-14s %s\n' "$t" "$(readlink -f "$(command -v $t 2>/dev/null)" 2>/dev/null)"
done
printf '    %-14s %s\n' "包自带的" "$(readlink -f /usr/bin/wine 2>/dev/null)（未改动）"
echo
echo "    菜单条目里记的是绝对路径（winemenubuilder 用 WINELOADER 写的），"
echo "    所以即使 PATH 变了也不会指错；重建 Wine 之后重跑本脚本刷新 $TARGET。"
