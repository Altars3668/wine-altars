#!/usr/bin/env bash
# Check that Office's own sign-in is the path that will run, and that its
# result will still be there tomorrow.
#
# There are two ways for Office to sign in here and only one of them lasts.
#
#   The broker.  windows.security.authentication.onlineid answers token
#   requests out of a file per scope under Z:\tmp\office-wam-tokens, minted
#   from a Windows machine by scripts/mint-wam-tokens.sh.  Those are live
#   credentials with a life measured in hours, they sit in /tmp, which systemd
#   ages out after ten days, and nothing refreshes them.  It is a way to get
#   signed in for an afternoon, not a way to be signed in.
#
#   Office's own window.  With the two feature gates on, Office renders the
#   real sign-in page in WebView2 and stores what comes back in the profile,
#   sealed with DPAPI.  That survives restarts.
#
# The broker now stands aside whenever it holds no tokens -- claiming a
# provider it cannot serve is what kept Office from ever reaching its own
# window -- so an empty store *is* the native configuration.  This script
# checks each link of that chain and says which one is missing.
#
# --move-foreign-caches moves the identity caches aside (moved, not deleted).
# It is the remedy for one symptom and only that one: Office saying your
# account cannot be accessed.  Measured both ways.
#
# With a broken account record in the caches, File > Account shows "帐户错误"
# and "很抱歉，当前无法访问您的帐户" -- and every sign-in entry point leads
# back to the same dead account, because the account is remembered in the
# OneAuth cache, not in the registry.  Moving the caches aside clears that and
# File > Account > Sign in then opens a blank Microsoft sign-in page.
#
# On a prefix *not* showing that message, moving them is a step backwards:
# Office then greets you with its first-run "sign in to set up Office" dialog,
# whose button reaches no authentication API at all (measured: not one call on
# either the webauth or onlineid channel after the click).  The working entry
# point in that state is File > Account > Sign in, which does open the page --
# so if you move the caches, use that door, not the first-run one.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-wow64}"
WINE="${WINE:-$DIST/bin/wine}"
: "${WINEPREFIX:?set WINEPREFIX}"
export WINEPREFIX WINEDEBUG=-all

ok=0 bad=0
check() { # check <label> <ok?> <detail>
    if [ "$2" = 1 ]; then printf '  [ 好 ] %-34s %s\n' "$1" "${3:-}"; ok=$((ok+1))
    else printf '  [ 缺 ] %-34s %s\n' "$1" "${3:-}"; bad=$((bad+1)); fi
}

echo "== Office 自己的登录通道 =="

# 1. 两个功能门
KEY='HKCU\Software\Microsoft\Office\16.0\Common\ExperimentConfigs\ExternalFeatureOverrides\word'
gates=$("$WINE" reg query "$KEY" /reg:64 2>/dev/null | grep -ciE 'Microsoft\.Office\.Identity\.(FG\.IsWebView2ForOneAuthEnabled|TestGate\.DisableBrokerForOneAuth)')
check "WebView2 / 绕过 broker 两个门" "$([ "${gates:-0}" -ge 2 ] && echo 1 || echo 0)" \
      "$([ "${gates:-0}" -ge 2 ] && echo "已开" || echo "跑 scripts/enable-native-signin.sh")"

# 2. WebView2 运行时：没有它登录页会退回 mshtml，渲染成一个空 div
wv=$(ls -d "$WINEPREFIX/drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application/"[0-9]* 2>/dev/null | head -1)
check "WebView2 运行时" "$([ -n "$wv" ] && echo 1 || echo 0)" "${wv##*/}"

# 3. broker 手上有没有令牌。有 = 走短命的那条路；没有 = 走原生
store=0
[ -n "$(find /tmp/office-wam-tokens -maxdepth 1 -name '*.txt' 2>/dev/null | head -1)" ] && store=1
[ -f /tmp/office-wam-token.txt ] && store=1
if [ "$store" = 1 ]; then
    check "broker 让位" 0 "令牌库非空，会走短命的 WAM 路；清空它才是原生登录"
else
    check "broker 让位" 1 "无令牌，broker 不认领提供者"
fi

# 4. DPAPI：登录结果能不能跨进程解开。这一条不成立，登录当场有效、重启即失
probe="$ROOT/tools/dpapiprobe/dpapiprobe.exe"
if [ -x "$probe" ]; then
    blob="$WINEPREFIX/drive_c/windows/temp/signin-dpapi-check.bin"
    w=$("$WINE" winepath -w "$blob" 2>/dev/null)
    "$WINE" "$probe" seal "$w" >/dev/null 2>&1
    "$DIST/bin/wineserver" -k >/dev/null 2>&1; sleep 2
    if "$WINE" "$probe" unseal "$w" 2>/dev/null | grep -q '内容一致'; then
        check "DPAPI 跨进程" 1 "登录结果可持久"
    else
        check "DPAPI 跨进程" 0 "解不开，登录不会跨重启存活"
    fi
    rm -f "$blob"
else
    check "DPAPI 跨进程" 0 "tools/dpapiprobe 未编译，无法验证"
fi

# 5. 别的机器留下的身份缓存：它们的 DPAPI blob 在这里打不开，Office 会说
#    “无法访问你的账户”，连登录表单都不给。
foreign=0
for u in "$WINEPREFIX/drive_c/users/"*; do
    for d in OneAuth IdentityCache; do
        p="$u/AppData/Local/Microsoft/$d"
        [ -d "$p" ] || continue
        # 早于本 prefix 建立时间的，基本可以断定是搬过来的
        [ "$p" -ot "$WINEPREFIX/system.reg" ] && foreign=$((foreign+1))
    done
done
# 这一条只是提示，不算缺陷：留着它们通常是对的（见脚本开头的实测）。
if [ "$foreign" = 0 ]; then
    printf '  [ -- ] %-34s %s\n' "身份缓存" "无历史缓存"
else
    printf '  [ -- ] %-34s %s\n' "身份缓存" "$foreign 处早于本 prefix；保持原样，只有在 Office 报「无法访问你的账户」时才用 --move-foreign-caches"
fi

if [ "${1:-}" = --move-foreign-caches ]; then
    echo
    echo "== 移走跨机身份缓存（移动，不删除） =="
    stash="$WINEPREFIX/identity-caches-from-another-machine"
    mkdir -p "$stash"
    moved=0
    for u in "$WINEPREFIX/drive_c/users/"*; do
        [ -d "$u" ] || continue
        for d in OneAuth IdentityCache TokenBroker; do
            p="$u/AppData/Local/Microsoft/$d"
            [ -d "$p" ] || continue
            [ "$p" -ot "$WINEPREFIX/system.reg" ] || continue
            t="$stash/$(basename "$u")-$d"
            [ -e "$t" ] && t="$t.$(date +%s)"
            mv "$p" "$t" && { printf '  %-28s -> %s\n' "$(basename "$u")/$d" "${t##*/}"; moved=$((moved+1)); }
        done
    done
    echo "  共 $moved 处，放在 $stash"
    echo "  （Office 下次启动会从空身份开始，直接给登录页）"
fi

echo
if [ "$bad" = 0 ]; then
    echo "== $ok 项全部就绪：启动 Office 时会弹出它自己的登录页（OneAuthWebView2Browser） =="
    echo "   登录一次即可，结果写进 prefix 并跨重启保留。"
else
    echo "== $ok 项就绪，$bad 项待处理（见上） =="
fi
