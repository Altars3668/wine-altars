#!/usr/bin/env bash
# Make this Wine the one the system runs, without touching the packaged one.
#
# A desktop entry names a Wine; a machine may have several.  Here the WineHQ
# package owns /usr/bin/wine -> /opt/wine-stable, and a prefix built against
# this tree cannot run under it: Office dies before it paints anything and the
# next launch offers safe mode, with nothing in between to say why.
#
# So this installs a copy under /opt/wine-altars and puts it ahead of the
# package on PATH through /usr/local/bin, which precedes /usr/bin.  No file
# belonging to winehq-stable is touched, apt has nothing to fight over, and
# undoing it is removing the symlinks:
#
#     sudo rm -f /usr/local/bin/wine /usr/local/bin/wineserver ...   (--uninstall)
#
# A copy, not a symlink into the build tree: pointing the whole system at a git
# working directory means a rebuild or a stray clean takes every Wine
# application down with it.  Re-run this after rebuilding to refresh the copy.
#
# Note what this changes: every Wine application on the machine, not just
# Office, will start using this build.  It is CrossOver-derived Wine with the
# patches in patches/wine on top, so that is usually an improvement or a
# no-op -- but it is a system-wide change and it is yours to decide.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-wow64}"
TARGET="${TARGET:-/opt/wine-altars}"
LINKDIR="${LINKDIR:-/usr/local/bin}"

say() { printf '==> %s\n' "$*"; }

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

[ -x "$DIST/bin/wine" ] || { echo "先构建：scripts/build-wine.sh（ARCHS=i386,x86_64）" >&2; exit 1; }

say "安装到 $TARGET"
sudo mkdir -p "$TARGET"
if command -v rsync >/dev/null; then
    sudo rsync -a --delete "$DIST/" "$TARGET/"
else
    sudo rm -rf "$TARGET"; sudo mkdir -p "$TARGET"; sudo cp -a "$DIST/." "$TARGET/"
fi
echo "    $(du -sh "$TARGET" | cut -f1)"

# Wine finds its own libraries relative to the loader, so a copy runs from
# anywhere -- but check rather than assume, before anything points at it.
say "先验证再切换"
ver=$("$TARGET/bin/wine" --version 2>/dev/null | head -1)
[ -n "$ver" ] || { echo "    $TARGET/bin/wine 跑不起来，不做切换" >&2; exit 1; }
echo "    $ver"

say "把 $LINKDIR 指过去"
n=0
for f in "$TARGET"/bin/*; do
    [ -x "$f" ] || continue
    sudo ln -sf "$f" "$LINKDIR/$(basename "$f")" && n=$((n+1))
done
echo "    $n 个链接"

hash -r 2>/dev/null || true
say "结果"
printf '    %-14s %s\n' "wine" "$(readlink -f "$(command -v wine 2>/dev/null)" 2>/dev/null)"
printf '    %-14s %s\n' "包自带的" "$(readlink -f /usr/bin/wine 2>/dev/null)（未改动）"
echo
echo "    菜单条目里记的是绝对路径（winemenubuilder 用 WINELOADER 写的），"
echo "    所以即使 PATH 变了也不会指错；重建 Wine 之后重跑本脚本刷新 $TARGET。"
