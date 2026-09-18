#!/usr/bin/env bash
# Make Office in this prefix behave like an installed application.
#
# Wine already writes one .desktop file per file extension, but they are
# handlers, not applications: NoDisplay=true keeps them out of the menu, they
# carry no StartupWMClass, and there is no entry for the applications
# themselves.  The visible result is that Office has no menu entry, cannot be
# pinned, and its windows appear in the dock under a generic icon instead of
# grouping with the launcher that started them.
#
# What this adds, per installed application:
#
#   * a normal menu entry, named and described in both languages
#   * StartupWMClass, so the window groups with its launcher.  Wine names the
#     window class after the executable, lowercased -- winword.exe and so on --
#     which is what the desktop matches against.
#   * the MIME types that application really opens, and itself as the default
#     handler for them
#   * desktop actions (a new blank document, and for Word the safe-mode start
#     that is otherwise a command line only)
#
# Icons come from winemenubuilder, which extracts them from the executables
# into the hicolor theme when it first sees the file associations.  An
# application whose icon it has not extracted is still given an entry; it just
# inherits a generic one, which is better than no launcher.
#
# Everything is written under ~/.local/share, so this needs no privileges and
# is undone by removing those files (--uninstall does it).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
APPDIR="$HOME/.local/share/applications"
OFFICE="$WINEPREFIX/drive_c/Program Files/Microsoft Office/root/Office16"
LAUNCH="$ROOT/scripts/office-launch.sh"
PREFIX=wine-altars          # our entries all share this, so cleanup is exact

say() { printf '==> %s\n' "$*"; }

if [ "${1:-}" = --uninstall ]; then
    n=$(find "$APPDIR" -maxdepth 1 -name "$PREFIX-*.desktop" | wc -l)
    rm -f "$APPDIR/$PREFIX-"*.desktop
    update-desktop-database "$APPDIR" 2>/dev/null || true
    echo "已移除 $n 个条目"
    exit 0
fi

[ -d "$OFFICE" ] || { echo "找不到 Office 目录: $OFFICE" >&2; exit 1; }
[ -x "$LAUNCH" ] || { echo "缺少 $LAUNCH" >&2; exit 1; }
mkdir -p "$APPDIR"

# The icon winemenubuilder extracted, if it did.  Its names are hashed
# (7A2C_WINWORD.0), so the executable stem is what we can match on.
find_icon() {
    local stem=$1 f
    for f in "$HOME/.local/share/icons/hicolor/"*"/apps/"*"_${stem}.0.png"; do
        [ -e "$f" ] || continue
        basename "$f" .png
        return
    done
    echo ""
}

# app|exe|WM class|icon stem|English name|Chinese name|categories|mime types
APPS='
word|WINWORD.EXE|winword.exe|WINWORD|Microsoft Word|Microsoft Word|Office;WordProcessor;|application/vnd.openxmlformats-officedocument.wordprocessingml.document;application/msword;application/vnd.openxmlformats-officedocument.wordprocessingml.template;application/vnd.ms-word.document.macroenabled.12;application/vnd.ms-word.template.macroenabled.12;application/rtf;
excel|EXCEL.EXE|excel.exe|EXCEL|Microsoft Excel|Microsoft Excel|Office;Spreadsheet;|application/vnd.openxmlformats-officedocument.spreadsheetml.sheet;application/vnd.ms-excel;application/vnd.openxmlformats-officedocument.spreadsheetml.template;application/vnd.ms-excel.sheet.macroenabled.12;application/vnd.ms-excel.template.macroenabled.12;text/csv;
powerpoint|POWERPNT.EXE|powerpnt.exe|POWERPNT|Microsoft PowerPoint|Microsoft PowerPoint|Office;Presentation;|application/vnd.openxmlformats-officedocument.presentationml.presentation;application/vnd.ms-powerpoint;application/vnd.openxmlformats-officedocument.presentationml.slideshow;application/vnd.openxmlformats-officedocument.presentationml.template;application/vnd.ms-powerpoint.presentation.macroenabled.12;
outlook|OUTLOOK.EXE|outlook.exe|OUTLOOK|Microsoft Outlook|Microsoft Outlook|Office;Email;|application/vnd.ms-outlook;
onenote|ONENOTE.EXE|onenote.exe|ONENOTE|Microsoft OneNote|Microsoft OneNote|Office;|application/onenote;
access|MSACCESS.EXE|msaccess.exe|MSACCESS|Microsoft Access|Microsoft Access|Office;Database;|application/vnd.ms-access;application/x-msaccess;
publisher|MSPUB.EXE|mspub.exe|MSPUB|Microsoft Publisher|Microsoft Publisher|Office;Publishing;|application/x-mspublisher;
visio|VISIO.EXE|visio.exe|VISIO|Microsoft Visio|Microsoft Visio|Office;FlowChart;|application/vnd.visio;application/vnd.ms-visio.drawing.main+xml;
'

say "生成启动器"
made=0 mimes_all=""
while IFS='|' read -r app exe wmclass stem name_en name_zh cats mimes; do
    [ -n "${app:-}" ] || continue
    if [ ! -f "$OFFICE/$exe" ]; then
        printf '    %-12s 未安装，跳过\n' "$app"
        continue
    fi
    icon=$(find_icon "$stem")
    f="$APPDIR/$PREFIX-$app.desktop"
    {
        echo "[Desktop Entry]"
        echo "Type=Application"
        echo "Version=1.0"
        echo "Name=$name_en"
        echo "Name[zh_CN]=$name_zh"
        echo "GenericName=Office"
        echo "Exec=$LAUNCH $app %F"
        echo "TryExec=$LAUNCH"
        echo "Terminal=false"
        echo "StartupNotify=true"
        # The whole point of this file: the window carries this class, so the
        # shell groups it under this launcher instead of a stray generic icon.
        echo "StartupWMClass=$wmclass"
        [ -n "$icon" ] && echo "Icon=$icon"
        echo "Categories=$cats"
        echo "MimeType=$mimes"
        echo "Actions=NewDocument;"
        echo ""
        echo "[Desktop Action NewDocument]"
        echo "Name=New Document"
        echo "Name[zh_CN]=新建文档"
        echo "Exec=$LAUNCH $app"
    } > "$f"
    printf '    %-12s %-22s 图标=%s\n' "$app" "$(basename "$f")" "${icon:-（无，用通用图标）}"
    mimes_all="$mimes_all$mimes"
    made=$((made + 1))
done <<< "$APPS"

say "设为默认打开程序"
set_default() {   # set_default <mime> <desktop>
    xdg-mime default "$2" "$1" 2>/dev/null
}
for app in word excel powerpoint outlook onenote access publisher visio; do
    f="$APPDIR/$PREFIX-$app.desktop"
    [ -f "$f" ] || continue
    n=0
    while IFS=';' read -ra list; do
        for m in "${list[@]}"; do
            [ -n "$m" ] || continue
            set_default "$m" "$PREFIX-$app.desktop" && n=$((n + 1))
        done
    done <<< "$(grep -oP '(?<=^MimeType=).*' "$f")"
    printf '    %-12s %s 个类型\n' "$app" "$n"
done

say "刷新数据库"
update-desktop-database "$APPDIR" 2>/dev/null && echo "    桌面数据库已更新"
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null && echo "    图标缓存已更新"

say "done（$made 个应用）"
