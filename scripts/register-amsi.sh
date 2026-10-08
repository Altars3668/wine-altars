#!/usr/bin/env bash
# Register the AMSI scan interface so Office can reach the host's scanner.
#
# Office asks for CLSID {fdb00e52-...} before it runs a macro.  If the class
# cannot be created it treats that as a scan it could not perform, and the
# macro is refused whatever the user answers -- the application has no way to
# tell "scanned and clean" from "never scanned".
#
# Wine registers builtin COM classes when a prefix is created, so a fresh
# prefix already has this.  A prefix that predates the class does not, and
# CoCreateInstance fails with REGDB_E_CLASSNOTREG until it is registered here.
#
# What backs the interface is the host's own scanner (clamd), reached over a
# unix socket.  With no scanner running, AmsiInitialize and the class factory
# both fail instead of answering "nothing detected": an application that is
# told a scan succeeded is entitled to believe it, so a scan that never
# happened must be reported as a failure.
set -euo pipefail

WINE="${WINE:-$(dirname "$0")/../dist-up/bin/wine}"
: "${WINEPREFIX:?set WINEPREFIX}"
export WINEPREFIX

CLSID='{fdb00e52-a214-4aa1-8fba-4357bb0072ec}'

# regsvr32 does not always return promptly under Wine; the registration itself
# is done well before that, so cap the wait and verify the result instead.
WINEDEBUG=-all timeout 60 "$WINE" regsvr32 amsi.dll >/dev/null 2>&1 || true

if WINEDEBUG=-all "$WINE" reg query "HKCR\\CLSID\\$CLSID\\InprocServer32" >/dev/null 2>&1; then
    echo "amsi: COM 类已注册"
else
    echo "amsi: 注册失败，Office 运行宏时会认为扫描无法进行" >&2
    exit 1
fi

# Report what is actually behind it, rather than leaving the impression that
# registering the class is by itself enough.
socket=${WINE_AMSI_CLAMD_SOCKET:-}
if [ -z "$socket" ]; then
    socket=$(sed -n 's/^LocalSocket[[:space:]]\+//p' /etc/clamav/clamd.conf 2>/dev/null | head -1)
fi
: "${socket:=/var/run/clamav/clamd.ctl}"

if [ -S "$socket" ]; then
    echo "amsi: 扫描引擎在 $socket"
else
    echo "amsi: 没有扫描引擎（找不到 $socket）；AMSI 会如实报告无法扫描，"
    echo "      Office 因此会拒绝运行宏。装上并启动 clamav-daemon 即可。"
fi
