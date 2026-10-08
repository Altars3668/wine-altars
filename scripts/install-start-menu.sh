#!/usr/bin/env bash
# Give Office the Start Menu shortcuts an installer would have left, and let
# Wine build the desktop integration from them.
#
# A prefix made by copying an installed Office has no .lnk files, because no
# installer ever ran in it.  That single absence is why such a prefix has
# file-type handlers but no applications: Wine's menu support is driven
# entirely by shortcuts.  winemenubuilder is invoked per link, reads the
# target, extracts its icon, derives the window class from the executable name
# and writes the .desktop file itself -- name, Icon, Path, StartupWMClass, all
# of it.  With no link there is nothing for it to do.
#
# So this creates links, not .desktop files.  Everything a launcher needs then
# comes from Wine and stays in step with it, and nothing of ours sits in the
# path between the menu and Word.
#
# It also sets the prefix's own UI language.  Wine takes that from
# HKCU\Software\Wine\LC_MESSAGES, so it belongs to the prefix rather than to
# whatever shell happens to start Office -- which is what a Chinese Office
# wrapped in English Wine dialogs was really telling us.  (That value only
# began working with the ntdll fix in this tree; before it, only LC_ALL did,
# and it moved the whole locale rather than the language.)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-up}"
WINE="${WINE:-$DIST/bin/wine}"
: "${WINEPREFIX:?set WINEPREFIX}"
export WINEPREFIX WINEDEBUG=-all

MK="$ROOT/tools/mkshortcut/mkshortcut.exe"
OFFICE_WIN='C:\Program Files\Microsoft Office\root\Office16'
OFFICE_UNIX="$WINEPREFIX/drive_c/Program Files/Microsoft Office/root/Office16"
SM='C:\ProgramData\Microsoft\Windows\Start Menu\Programs'
UI_LANG="${WINE_UI_LANG:-zh_CN.UTF-8}"

say() { printf '==> %s\n' "$*"; }

[ -d "$OFFICE_UNIX" ] || { echo "找不到 Office: $OFFICE_UNIX" >&2; exit 1; }
[ -x "$MK" ] || { echo "缺少 $MK（见 tools/mkshortcut）" >&2; exit 1; }

say "界面语言 -> $UI_LANG（写进 prefix，不依赖启动环境）"
"$WINE" reg add 'HKCU\Software\Wine' /v LC_MESSAGES /t REG_SZ /d "$UI_LANG" /f >/dev/null 2>&1 &&
    echo "    HKCU\\Software\\Wine\\LC_MESSAGES"

say "开始菜单快捷方式"
# 名字用 Windows 上的样子；winemenubuilder 会照抄成菜单项的 Name。
while IFS='|' read -r name exe descr; do
    [ -n "${name:-}" ] || continue
    [ -f "$OFFICE_UNIX/$exe" ] || { printf '    %-12s 未安装\n' "$name"; continue; }
    if "$WINE" "$MK" "$SM\\$name.lnk" "$OFFICE_WIN\\$exe" "$descr" >/dev/null 2>&1; then
        printf '    %-12s -> %s\n' "$name" "$exe"
    else
        printf '    %-12s 创建失败\n' "$name"
    fi
done <<'APPS'
Word|WINWORD.EXE|Microsoft Word
Excel|EXCEL.EXE|Microsoft Excel
PowerPoint|POWERPNT.EXE|Microsoft PowerPoint
Outlook|OUTLOOK.EXE|Microsoft Outlook
OneNote|ONENOTE.EXE|Microsoft OneNote
Access|MSACCESS.EXE|Microsoft Access
Publisher|MSPUB.EXE|Microsoft Publisher
Visio|VISIO.EXE|Microsoft Visio
APPS

say "让 Wine 生成桌面条目"
# Saving a link through the shell already asks winemenubuilder to do this; the
# explicit pass is for links that were put there some other way.
n=0
for f in "$WINEPREFIX/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/"*.lnk; do
    [ -e "$f" ] || continue
    b=$(basename "$f")
    "$WINE" winemenubuilder "$SM\\$b" >/dev/null 2>&1 && n=$((n+1))
done
echo "    处理了 $n 个快捷方式"

made=$(ls "$HOME/.local/share/applications/wine/Programs/"*.desktop 2>/dev/null | wc -l)
say "Wine 写出的条目：$made 个"
for f in "$HOME/.local/share/applications/wine/Programs/"*.desktop; do
    [ -e "$f" ] || continue
    printf '    %-16s Icon=%-18s StartupWMClass=%s\n' "$(basename "$f")" \
        "$(grep -oP '(?<=^Icon=).*' "$f")" "$(grep -oP '(?<=^StartupWMClass=).*' "$f")"
done

say "done"
echo "    文件关联由 Wine 自己的 wine-extension-*.desktop 处理（wine start /ProgIDOpen 会做路径转换）"
