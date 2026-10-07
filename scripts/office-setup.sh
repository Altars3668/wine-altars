#!/usr/bin/env bash
# Set up Microsoft 365 (Word, Excel, PowerPoint) under wine-altars, one step at a time.
#
#   scripts/office-setup.sh prefix      create the Wine prefix: 64-bit, Windows 11, Gecko
#   scripts/office-setup.sh webview2    install the Edge WebView2 runtime (Office's sign-in window is a WebView2)
#   scripts/office-setup.sh install     install the apps with Microsoft's Office Deployment setup.exe
#   scripts/office-setup.sh signin      turn on the sign-in path Office already ships with
#   scripts/office-setup.sh all         the four above, in that order
#   scripts/office-setup.sh status      what is in place and what is missing
#   scripts/office-setup.sh launch [word|excel|powerpoint]
#
# Signing in is not a step here: it is you, in Office's own window, with your own Microsoft account
# and your own subscription.  Nothing in this repository fakes, copies or bypasses a licence.
#
# Environment (all optional):
#   WINE            the wine to use                                  default: wine (from PATH)
#   WINEPREFIX      where Office goes                                default: ~/.wine-office
#   OFFICE_PRODUCT  O365HomePremRetail  Microsoft 365 Family/Personal   (the one this was measured with)
#                   O365ProPlusRetail   Microsoft 365 Apps for enterprise (not measured)
#                   The product must match your subscription: the wrong one installs fine and then
#                   cannot be licensed.                              default: O365HomePremRetail
#   OFFICE_LANG     Office language(s), e.g. en-us or zh-cn          default: en-us
#   OFFICE_APPS     comma list from word,excel,powerpoint,outlook,onenote,access,publisher
#                                                                    default: word,excel,powerpoint
#   OFFICE_CHANNEL  update channel                                   default: Current
#
# "install" downloads about 2-3 GB from Microsoft's CDN, needs roughly 10 GB free, and runs
# with the licence terms accepted on your behalf (AcceptEULA="TRUE"): by running it you accept
# https://www.microsoft.com/useterms.  It takes a while and prints nothing from setup.exe itself;
# progress lines below show the install directory growing.
set -euo pipefail

WINE="${WINE:-wine}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-office}"
PRODUCT="${OFFICE_PRODUCT:-O365HomePremRetail}"
LANGS="${OFFICE_LANG:-en-us}"
APPS="${OFFICE_APPS:-word,excel,powerpoint}"
CHANNEL="${OFFICE_CHANNEL:-Current}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/wine-altars"
ADDONS="${XDG_CACHE_HOME:-$HOME/.cache}/wine"           # where Wine itself looks for Gecko/Mono installers

ODT_URL='https://officecdn.microsoft.com/pr/wsus/setup.exe'
WEBVIEW2_URL='https://go.microsoft.com/fwlink/p/?LinkId=2124703'
GECKO_VERSION=2.47.4
GECKO_URL="https://dl.winehq.org/wine/wine-gecko/$GECKO_VERSION"
OFFICE_DIR="$WINEPREFIX/drive_c/Program Files/Microsoft Office"
WEBVIEW2_GLOB="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application"

say()  { printf '\n==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
# No menu entries or file associations on your desktop while setting up (scripts/install-start-menu.sh
# makes the menu entries afterwards, on purpose).
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winemenubuilder.exe=d"
quiet_wine() { WINEDEBUG="${WINEDEBUG:--all}" "$WINE" "$@"; }
need_prefix() { [ -f "$WINEPREFIX/system.reg" ] || die "no prefix at $WINEPREFIX -- run: $0 prefix"; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || die "need $1 ($2)"; }
fetch() {   # fetch <url> <file>   (resumes; a finished file is kept)
    [ -s "$2" ] && { note "have $(basename "$2")"; return 0; }
    mkdir -p "$(dirname "$2")"
    local quiet=(-sS); [ -t 2 ] && quiet=(--progress-bar)
    note "downloading $(basename "$2")"
    if curl -fL --retry 3 -C - "${quiet[@]}" -o "$2.part" "$1"; then mv "$2.part" "$2"; else die "download failed: $1"; fi
}
winpath() { quiet_wine winepath -w "$1" 2>/dev/null | tr -d '\r'; }
# the wineserver that belongs to $WINE (a separate executable next to it, not a Windows program)
if [ -z "${WINESERVER:-}" ]; then
    wb=$(command -v "$WINE" 2>/dev/null || true)
    if [ -n "$wb" ] && [ -x "$(dirname "$(readlink -f "$wb")")/wineserver" ]; then
        WINESERVER="$(dirname "$(readlink -f "$wb")")/wineserver"
    else
        WINESERVER=wineserver
    fi
fi
ws() { "$WINESERVER" "$@"; }
# wineserver -w waits until every process of the prefix has ended, and Office leaves its
# Click-to-Run service running for ever: wait for the prefix to go quiet for a while, then stop it.
settle() {
    timeout "${1:-60}" "$WINESERVER" -w 2>/dev/null && return 0
    "$WINESERVER" -k 2>/dev/null || true
    "$WINESERVER" -w 2>/dev/null || true
}

webview2_dir() { find "$WEBVIEW2_GLOB" -mindepth 1 -maxdepth 1 -type d -name '[0-9]*' 2>/dev/null | head -1 || true; }

office_installed() {
    local e
    for e in WINWORD.EXE EXCEL.EXE POWERPNT.EXE OUTLOOK.EXE; do
        [ -f "$OFFICE_DIR/root/Office16/$e" ] && return 0
    done
    return 1
}

# ---------------------------------------------------------------------------------------------
cmd_prefix() {
    need_cmd "$WINE" "set WINE= to the wine from the release tarball or your own build"
    need_cmd curl curl
    say "Wine: $("$WINE" --version)"
    say "prefix: $WINEPREFIX"
    if [ -f "$WINEPREFIX/system.reg" ]; then
        note "already exists; leaving it alone (delete the directory to start over)"
    else
        # Mono and Gecko prompts have nothing to do with creating a prefix and would wait for a click
        WINEARCH=win64 WINEDLLOVERRIDES="$WINEDLLOVERRIDES;mscoree=d;mshtml=d" quiet_wine wineboot -u
        settle 120
    fi
    say "Windows 11 mode"
    quiet_wine winecfg -v win11
    settle 120

    # Without Gecko, anything that falls back to the built-in browser engine stops on Wine's own
    # "install Gecko?" prompt.  Install the two files here, from Wine's download site.
    say "Wine Gecko $GECKO_VERSION"
    if [ -d "$WINEPREFIX/drive_c/windows/system32/gecko/$GECKO_VERSION" ]; then
        note "already installed"
    else
        for arch in x86_64 x86; do
            f="wine-gecko-$GECKO_VERSION-$arch.msi"
            fetch "$GECKO_URL/$f" "$ADDONS/$f"
            quiet_wine msiexec /i "$(winpath "$ADDONS/$f")" /qn
        done
    fi
    # services.exe and everything it started were launched before the prefix was finished;
    # start the next thing from a clean wineserver
    ws -k 2>/dev/null || true
    note "done"
}

# ---------------------------------------------------------------------------------------------
cmd_webview2() {
    need_prefix; need_cmd curl curl
    say "Microsoft Edge WebView2 runtime"
    if [ -n "$(webview2_dir)" ]; then
        note "already installed: $(basename "$(webview2_dir)")"
        return 0
    fi
    # Same as `winetricks webview2` in current winetricks (older packages do not have the verb):
    # Microsoft's installer shell is a 32-bit program, which is why this build has a 32-bit half.
    fetch "$WEBVIEW2_URL" "$CACHE/MicrosoftEdgeWebview2Setup.exe"
    quiet_wine "$CACHE/MicrosoftEdgeWebview2Setup.exe" /silent /install || note "installer exited $? (checking the result below)"
    # Wine bug 53925: the installer leaves its update service on automatic start, where it hangs a prefix
    quiet_wine reg add 'HKLM\System\CurrentControlSet\Services\edgeupdate' /v Start /t REG_DWORD /d 3 /f >/dev/null
    sleep 5
    pkill -f '[M]icrosoftEdgeUpdate.exe /c' 2>/dev/null || true
    # Wine bug 58921: the runtime's renderer process only starts if it is told to see Windows 7
    quiet_wine reg add 'HKCU\Software\Wine\AppDefaults\msedgewebview2.exe' /v Version /t REG_SZ /d win7 /f >/dev/null
    settle 120
    [ -n "$(webview2_dir)" ] || die "WebView2 runtime did not install (no $WEBVIEW2_GLOB/<version>)"
    note "installed: $(basename "$(webview2_dir)")"
}

# ---------------------------------------------------------------------------------------------
write_odt_config() {   # write_odt_config <file>
    local all=(Access Bing Excel Groove Lync OneDrive OneNote Outlook OutlookForWindows PowerPoint Publisher Teams Word)
    local want=",${APPS,,}," exclude=() langs a l
    for a in "${all[@]}"; do
        case "$a" in
            OutlookForWindows|Bing|Groove|Lync|OneDrive|Teams) exclude+=("$a"); continue ;;   # never wanted here
        esac
        case "$want" in *",${a,,},"*) ;; *) exclude+=("$a") ;; esac
    done
    {
        echo '<Configuration>'
        echo "  <Add OfficeClientEdition=\"64\" Channel=\"$CHANNEL\">"
        echo "    <Product ID=\"$PRODUCT\">"
        IFS=, read -r -a langs <<<"$LANGS"
        for l in "${langs[@]}"; do echo "      <Language ID=\"$l\" />"; done
        for a in "${exclude[@]}"; do echo "      <ExcludeApp ID=\"$a\" />"; done
        echo '    </Product>'
        echo '  </Add>'
        echo '  <Updates Enabled="FALSE" />'
        echo '  <Display Level="None" AcceptEULA="TRUE" />'
        echo '  <Logging Level="Standard" Path="C:\c2rlog" />'
        echo '</Configuration>'
    } > "$1"
}

cmd_install() {
    need_prefix; need_cmd curl curl
    say "Microsoft 365: $PRODUCT, $LANGS, apps: $APPS"
    if office_installed && [ "${FORCE:-0}" != 1 ]; then
        note "Office is already installed in $WINEPREFIX (FORCE=1 to run setup again)"
        return 0
    fi
    # download.microsoft.com is not used: its certificate chain is missing from many Linux CA bundles
    fetch "$ODT_URL" "$CACHE/setup.exe"
    write_odt_config "$CACHE/configuration.xml"
    note "configuration:"; sed 's/^/        /' "$CACHE/configuration.xml"
    [ "${DRY_RUN:-0}" = 1 ] && { note "DRY_RUN=1: stopping before setup.exe runs"; return 0; }
    free_gb=$(df --output=avail -BG "$WINEPREFIX" | tail -1 | tr -dc 0-9)
    [ "${free_gb:-0}" -ge 10 ] || die "only ${free_gb:-?} GB free next to $WINEPREFIX; Office needs about 10 GB while it installs"
    # Start from a clean wineserver so every service sees the prefix's final environment.
    ws -k 2>/dev/null || true
    (
        while sleep 60; do
            [ -d "$OFFICE_DIR" ] && printf '    ... %s so far in Program Files\\Microsoft Office\n' "$(du -sh "$OFFICE_DIR" 2>/dev/null | cut -f1)"
        done
    ) &
    progress=$!
    trap 'kill $progress 2>/dev/null || true' EXIT
    rc=0
    quiet_wine "$CACHE/setup.exe" /configure "$(winpath "$CACHE/configuration.xml")" || rc=$?
    kill "$progress" 2>/dev/null || true; trap - EXIT
    settle 30    # the Click-to-Run service stays up after the install; this stops it and saves the registry
    if [ "$rc" != 0 ] || ! office_installed; then
        note "setup.exe exited $rc"
        note "its log is in $WINEPREFIX/drive_c/c2rlog/ -- the last 'Error' line there is the cause"
        die "the installation did not complete"
    fi
    note "installed: $(du -sh "$OFFICE_DIR" | cut -f1) in Program Files\\Microsoft Office"
}

# ---------------------------------------------------------------------------------------------
cmd_signin() {
    need_prefix
    say "Office's own sign-in window"
    WINE="$WINE" "$ROOT/scripts/enable-native-signin.sh"
    note "Now start an app ('$0 launch word') and sign in -- see docs/getting-started.md."
}

# ---------------------------------------------------------------------------------------------
cmd_status() {
    set +e    # purely informational: a missing key or file is an answer, not a reason to stop
    local ok=0 bad=0
    check() { if [ "$2" = 1 ]; then printf '  [ ok ] %-30s %s\n' "$1" "${3:-}"; ok=$((ok+1)); else printf '  [ -- ] %-30s %s\n' "$1" "${3:-}"; bad=$((bad+1)); fi; }
    echo "wine-altars / Microsoft 365 status for $WINEPREFIX"
    if command -v "$WINE" >/dev/null 2>&1; then check "Wine" 1 "$("$WINE" --version 2>/dev/null)"; else check "Wine" 0 "not found: $WINE"; fi
    if [ -f "$WINEPREFIX/system.reg" ]; then
        check "prefix" 1 "$WINEPREFIX"
        build=$(quiet_wine reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion' /v CurrentBuildNumber 2>/dev/null | tr -d '\r' | awk '/CurrentBuildNumber/ {print $NF}')
        check "Windows version" "$([ "${build:-0}" -ge 22000 ] && echo 1 || echo 0)" "build ${build:-?} (Windows 11 is 22000 and up)"
        check "Gecko" "$([ -d "$WINEPREFIX/drive_c/windows/system32/gecko/$GECKO_VERSION" ] && echo 1 || echo 0)" "$GECKO_VERSION"
        wv=$(webview2_dir)
        check "WebView2 runtime" "$([ -n "$wv" ] && echo 1 || echo 0)" "${wv##*/}"
        office=$(office_installed && echo 1 || echo 0)
        ver=$(quiet_wine reg query 'HKLM\Software\Microsoft\Office\ClickToRun\Configuration' /v VersionToReport 2>/dev/null | tr -d '\r' | awk '/VersionToReport/ {print $NF}')
        pid=$(quiet_wine reg query 'HKLM\Software\Microsoft\Office\ClickToRun\Configuration' /v ProductReleaseIds 2>/dev/null | tr -d '\r' | awk '/ProductReleaseIds/ {print $NF}')
        check "Office apps" "$office" "${ver:-} ${pid:-}"
        g=$(quiet_wine reg query 'HKCU\Software\Microsoft\Office\16.0\Common\ExperimentConfigs\ExternalFeatureOverrides\word' /reg:64 2>/dev/null | /usr/bin/grep -ciE 'IsWebView2ForOneAuthEnabled|DisableBrokerForOneAuth' || true)
        check "sign-in gates (Word)" "$([ "${g:-0}" -ge 2 ] && echo 1 || echo 0)" "$([ "${g:-0}" -ge 2 ] && echo on || echo "run: $0 signin")"
        if [ -d /tmp/office-wam-tokens ] && [ -n "$(find /tmp/office-wam-tokens -maxdepth 1 -type f 2>/dev/null | head -1)" ]; then
            check "no borrowed tokens" 0 "/tmp/office-wam-tokens is not empty (left over from the old token route; remove it)"
        fi
    else
        check "prefix" 0 "none at $WINEPREFIX -- run: $0 prefix"
    fi
    echo
    echo "$ok ok, $bad missing.  Whether Office is signed in and licensed is shown in the app: File > Account."
}

# ---------------------------------------------------------------------------------------------
cmd_launch() {
    need_prefix
    local app="${1:-word}" exe
    case "${app,,}" in
        word) exe=WINWORD.EXE ;;
        excel) exe=EXCEL.EXE ;;
        powerpoint) exe=POWERPNT.EXE ;;
        outlook) exe=OUTLOOK.EXE ;;
        *) die "unknown app '$app' (word, excel, powerpoint, outlook)" ;;
    esac
    f="$OFFICE_DIR/root/Office16/$exe"
    [ -f "$f" ] || die "$exe not found; is Office installed?  ($0 status)"
    say "starting $exe"
    note "output goes to ${TMPDIR:-/tmp}/office-$exe.log; close the app normally (an unclean exit makes Office offer Safe Mode next time)"
    nohup env WINEDEBUG="${WINEDEBUG:--all}" "$WINE" "$f" "${@:2}" >"${TMPDIR:-/tmp}/office-$exe.log" 2>&1 &
}

# ---------------------------------------------------------------------------------------------
case "${1:-}" in
    prefix)   cmd_prefix ;;
    webview2) cmd_webview2 ;;
    install)  cmd_install ;;
    signin)   cmd_signin ;;
    all)      cmd_prefix; cmd_webview2; cmd_install; cmd_signin
              say "Everything is in place. Start Word and sign in:  $0 launch word" ;;
    status)   cmd_status ;;
    launch)   shift; cmd_launch "$@" ;;
    ''|-h|--help|help) sed -n '2,/^set -euo/{/^#/s/^# \{0,1\}//p}' "$0" ;;
    *) echo "unknown command: $1 (try --help)" >&2; exit 2 ;;
esac
